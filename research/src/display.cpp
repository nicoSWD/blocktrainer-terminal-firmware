// display.cpp: LovyanGFX panel driver + LVGL 8 UI.
//
// Status of the page builders (see STATUS.md for the full function table):
//   ported by hand from the decompile : BLOCK, UHR (clock), FEES, OTA screens,
//                                       Wi-Fi status screens, UI coordinator task
//   NOT ported yet (placeholder label) : PREIS, CHART, BITAXE, RECHNER, NEWS, DIFFICULTY,
//                                       config menu, stream bar. A mechanical LVGL
//                                       translation of all of them is in ../ui_translated.txt.

#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <time.h>
#include <vector>
#include <functional>
#include "display.h"
#include "assets.h"
#include "config.h"
#include "data.h"
#include "wifi_mgr.h"
#include "ota.h"
#include "hw.h"

const lv_img_dsc_t *IMG_LOGO = nullptr;
const lv_img_dsc_t *IMG_BLOCK = nullptr;

// ------------------------------------------------------------------ panel (LovyanGFX)
// Bus_Parallel8 is confirmed by a string in the firmware. The pins below are the public
// WT32-SC01 Plus configuration and are UNVERIFIED against the firmware's own copy.
class LGFX : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 _panel;
    lgfx::Bus_Parallel8 _bus;
    lgfx::Light_PWM _light;
    lgfx::Touch_FT5x06 _touch;
public:
    LGFX() {
        { auto c = _bus.config();
          c.freq_write = 40000000;
          c.pin_wr = 47; c.pin_rd = -1; c.pin_rs = 0;
          c.pin_d0 = 9; c.pin_d1 = 46; c.pin_d2 = 3; c.pin_d3 = 8;
          c.pin_d4 = 18; c.pin_d5 = 17; c.pin_d6 = 16; c.pin_d7 = 15;
          _bus.config(c);
          _panel.setBus(&_bus); }
        { auto c = _panel.config();
          c.pin_cs = -1; c.pin_rst = 4; c.pin_busy = -1;
          c.panel_width = 320; c.panel_height = 480;
          c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 0;
          c.readable = true; c.invert = true; c.rgb_order = false; c.dlen_16bit = false; c.bus_shared = true;
          _panel.config(c); }
        { auto c = _light.config();
          c.pin_bl = 45; c.invert = false; c.freq = 44100; c.pwm_channel = 7;
          _light.config(c);
          _panel.setLight(&_light); }
        { auto c = _touch.config();
          c.x_min = 0; c.x_max = 319; c.y_min = 0; c.y_max = 479;
          c.pin_int = 7; c.bus_shared = true; c.offset_rotation = 0;
          c.i2c_port = 1; c.i2c_addr = 0x38; c.pin_sda = 6; c.pin_scl = 5; c.freq = 400000;
          _touch.config(c);
          _panel.setTouch(&_touch); }
        setPanel(&_panel);
    }
};
static LGFX lcd;                       // 0x3fc9bca8

static const int SCREEN_W = 480, SCREEN_H = 320;
static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf1;
static lv_disp_drv_t s_disp_drv;
static lv_indev_drv_t s_indev_drv;

static void flush_cb(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px)
{
    uint32_t w = a->x2 - a->x1 + 1, h = a->y2 - a->y1 + 1;
    lcd.startWrite();
    lcd.setAddrWindow(a->x1, a->y1, w, h);
    lcd.writePixels((lgfx::rgb565_t *)&px->full, w * h);
    lcd.endWrite();
    lv_disp_flush_ready(drv);
}

// FUN_4200b4b4: LVGL pointer read callback. A press is reported once, then the
// reported position only follows the finger when it moved by 2 px or more; the
// first touch plays the click sound (FUN_42032fcc, "buzzer_click").
static bool s_touch_enabled = true;                    // DAT_3fc9baac
static bool s_pressed = false;
static int16_t s_last_x, s_last_y;
static void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *d)
{
    if (!s_touch_enabled) { d->state = LV_INDEV_STATE_REL; return; }
    uint16_t x = 0, y = 0;
    bool t = lcd.getTouch(&x, &y);
    if (!t) { s_pressed = false; d->state = LV_INDEV_STATE_REL; return; }
    if (!s_pressed) {
        s_pressed = true;
        s_last_x = x; s_last_y = y;
        buzzer_click();
    } else if (abs((int)x - s_last_x) >= 2 || abs((int)y - s_last_y) >= 2) {
        s_last_x = x; s_last_y = y;
    }
    d->point.x = s_last_x;
    d->point.y = s_last_y;
    d->state = LV_INDEV_STATE_PR;
}

// ------------------------------------------------------------------ UI actions from other tasks
// The original uses lv_async_call; this queue is drained by ui_task so every LVGL call
// happens on one task.
static std::vector<std::function<void()>> s_queue;
static SemaphoreHandle_t s_queue_mutex;
static void ui_post(std::function<void()> fn)
{
    xSemaphoreTake(s_queue_mutex, portMAX_DELAY);
    s_queue.push_back(std::move(fn));
    xSemaphoreGive(s_queue_mutex);
}
static void ui_drain()
{
    std::vector<std::function<void()>> q;
    xSemaphoreTake(s_queue_mutex, portMAX_DELAY);
    q.swap(s_queue);
    xSemaphoreGive(s_queue_mutex);
    for (auto &f : q) f();
}

// ------------------------------------------------------------------ layers
static lv_obj_t *s_container;     // 0x3fc9bc88: full-screen container that holds the page
static lv_obj_t *s_statusbar;     // 0x3fc9bc84: 30 px stream/status bar, always last child
static int s_current_page = -1;   // 0x3fc9b860
static int s_pending_page = -1;
static bool s_pending_animate = false;
static bool s_switching = false;  // 0x3fc9b868
static uint32_t s_switch_ms = 0;  // 0x3fc9c0b0
static const uint32_t SWITCH_COOLDOWN_MS = 500;   // 0x3fc9b874
static uint32_t s_alert_arm_ms = 0;               // 0x3fc9b86c: first page shown, alerts start 10 s later
static uint32_t s_last_switch_ms = 0;             // 0x3fc9b870

void display_set_brightness(int percent)
{
    lcd.setBrightness((uint8_t)constrain(percent, 0, 100) * 255 / 100);
}

// ------------------------------------------------------------------ pages
static lv_obj_t *mk_label(lv_obj_t *p, const char *txt, const lv_font_t *f, lv_color_t c = COL_WHITE)
{
    lv_obj_t *l = lv_label_create(p);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    return l;
}
static lv_obj_t *mk_panel(lv_obj_t *p, int w, int h, int x, int y)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_pos(o, x, y);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    return o;
}
static bool valid(lv_obj_t *o) { return o && lv_obj_is_valid(o); }
static lv_obj_t *page_root(lv_obj_t *container)
{
    lv_obj_t *r = lv_obj_create(container);
    lv_obj_remove_style_all(r);
    lv_obj_set_size(r, LV_PCT(100), LV_PCT(100));
    lv_obj_clear_flag(r, LV_OBJ_FLAG_SCROLLABLE);
    return r;
}

// ---- BLOCK (builder FUN_4202631c, update FUN_42026480) ----
static lv_obj_t *w_block_root, *w_block_label;     // 0x3fc9e77c root, 0x3fc9e780 label
static void block_build(lv_obj_t *c)
{
    w_block_root = page_root(c);
    lv_obj_t *col = lv_obj_create(w_block_root);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, 5, 0);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, 0);
    if (IMG_BLOCK) {
        lv_obj_t *img = lv_img_create(col);
        lv_img_set_src(img, IMG_BLOCK);
        lv_obj_set_style_pad_left(img, 5, 0);
    }
    w_block_label = mk_label(col, "000000", FONT_LH126);
    if (live.blockHeight >= 1) lv_label_set_text_fmt(w_block_label, "%d", live.blockHeight);
}
static void block_update()
{
    if (valid(w_block_label)) lv_label_set_text_fmt(w_block_label, "%d", live.blockHeight);
}
static void block_destroy() { if (valid(w_block_root)) lv_obj_del(w_block_root); w_block_root = w_block_label = nullptr; }

// ---- UHR / clock (builder FUN_42027dcc, update FUN_42028024 + FUN_4200bb40) ----
static lv_obj_t *w_clock_root, *w_clock_time, *w_clock_weekday, *w_clock_date;   // 3fc9f30c, f318, f310, f314
static const char *DE_WEEKDAY[7] = { "SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH", "DONNERSTAG", "FREITAG", "SAMSTAG" };

// FUN_4200bb40: "00:00" / "01.01.1970" until NTP delivered a time (5 s wait in getLocalTime)
static void clock_strings(char *tbuf, size_t tn, char *dbuf, size_t dn, const char **wd)
{
    struct tm t;
    if (!getLocalTime(&t, 5000)) {
        DBG("getLocalTime fehlgeschlagen\n");
        snprintf(tbuf, tn, "00:00");
        snprintf(dbuf, dn, "01.01.1970");
        *wd = "UNBEKANNT";
    } else {
        strftime(tbuf, tn, "%H:%M", &t);
        strftime(dbuf, dn, "%d.%m.%Y", &t);
        *wd = DE_WEEKDAY[t.tm_wday];                // FUN_4200c83c, "UNBEKANNT" on failure
    }
}
static void clock_build(lv_obj_t *c)
{
    w_clock_root = page_root(c);
    w_clock_time = mk_label(w_clock_root, "", FONT_LH126);
    lv_obj_align(w_clock_time, LV_ALIGN_TOP_MID, 0, 75);

    lv_obj_t *row = lv_obj_create(w_clock_root);
    lv_obj_set_size(row, 0x1d6, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 20, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_align_to(row, w_clock_time, LV_ALIGN_OUT_BOTTOM_MID, 0, -10);

    w_clock_weekday = mk_label(row, "", FONT_LH36);
    lv_obj_t *sep = lv_obj_create(row);                    // 3x30 white pill between weekday and date
    lv_obj_set_size(sep, 3, 30);
    lv_obj_set_style_radius(sep, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(sep, COL_WHITE, 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
    w_clock_date = mk_label(row, "", FONT_LH36);

    char tb[10], db[16]; const char *wd;
    clock_strings(tb, sizeof(tb), db, sizeof(db), &wd);
    lv_label_set_text(w_clock_time, tb);
    lv_label_set_text(w_clock_date, db);
    lv_label_set_text(w_clock_weekday, wd);
}
static void clock_update()
{
    if (!valid(w_clock_time)) return;
    char tb[10], db[16]; const char *wd;
    clock_strings(tb, sizeof(tb), db, sizeof(db), &wd);
    lv_label_set_text(w_clock_time, tb);
    lv_label_set_text(w_clock_date, db);
    lv_label_set_text(w_clock_weekday, wd);
}
static void clock_destroy() { if (valid(w_clock_root)) lv_obj_del(w_clock_root); w_clock_root = nullptr; }

// ---- FEES (builder FUN_4202a33c + FUN_42029c48, update FUN_4202a6b4) ----
static lv_obj_t *w_fees_root, *w_fees_top, *w_fees_left, *w_fees_right, *w_fees_bottom;
static lv_obj_t *w_fees_height, *w_fees_sats, *w_fees_sats_cur;
static lv_obj_t *w_fee_val[3], *w_fee_cost[3];          // 0=low 1=mid 2=high
static bool s_fees_compact = false;                     // DAT_3fc9f38c: layout shrinks while the stream bar is visible

static void fees_layout(bool compact)
{
    // FUN_42029abc
    int top = compact ? 0x24 : 0, h = compact ? 0x7f : 0x9d;
    int rest = (0x140 - top) - h;
    if (valid(w_fees_top))    { lv_obj_set_y(w_fees_top, top); lv_obj_set_height(w_fees_top, h); }
    if (valid(w_fees_bottom)) { lv_obj_set_y(w_fees_bottom, top + h + 7); lv_obj_set_height(w_fees_bottom, rest - 9); }
    if (valid(w_fees_left))   lv_obj_set_height(w_fees_left, h);
    if (valid(w_fees_right))  lv_obj_set_height(w_fees_right, h);
}

static void fees_build(lv_obj_t *c)
{
    w_fees_root = page_root(c);
    s_fees_compact = false;

    // top row: two cards
    w_fees_top = mk_panel(w_fees_root, LV_PCT(100), 0x9d, 0, 0);
    w_fees_left = mk_panel(w_fees_top, 0x118, 0x9d, 0, 0);
    lv_obj_t *l = mk_label(w_fees_left, "BLOCKZEIT", FONT_LH21);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0xf, 0xc);
    lv_obj_t *hbox = mk_panel(w_fees_left, LV_PCT(100), 0x9d - 0x30, 0, 0x28);
    w_fees_height = mk_label(hbox, "", FONT_LH61);
    lv_label_set_text_fmt(w_fees_height, "%d", live.blockHeight);
    lv_obj_align(w_fees_height, LV_ALIGN_CENTER, 0, 5);

    w_fees_right = mk_panel(w_fees_top, 0xc3, 0x9d, 0x11d, 0);
    lv_obj_t *s1 = mk_label(w_fees_right, "SATS / 1", FONT_LH21);
    lv_obj_align(s1, LV_ALIGN_TOP_LEFT, 0xc, 0xc);
    w_fees_sats_cur = mk_label(w_fees_right, cfg.currency.c_str(), FONT_LH21);
    lv_obj_align_to(w_fees_sats_cur, s1, LV_ALIGN_OUT_RIGHT_MID, 5, 0);
    lv_obj_t *sbox = mk_panel(w_fees_right, LV_PCT(100), 0x9d - 0x30, 0, 0x28);
    w_fees_sats = mk_label(sbox, "", FONT_LH61);
    lv_label_set_text_fmt(w_fees_sats, "%d", live.satsPerUnit);
    lv_obj_align(w_fees_sats, LV_ALIGN_CENTER, 0, 5);

    // bottom card: TRANSAKTIONSGEBÜHREN, three priority columns
    w_fees_bottom = mk_panel(w_fees_root, LV_PCT(100), 0x140 - 0x9d - 9 - 7, 0, 0x9d + 7);
    lv_obj_t *title = mk_label(w_fees_bottom, "TRANSAKTIONSGEBÜHREN", FONT_LH21);
    lv_obj_set_style_text_letter_space(title, 1, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0xf, 2);
    lv_obj_t *grid = mk_panel(w_fees_bottom, LV_PCT(100), 0x6e + 0x20, 0, 0x2d);

    static const char *NAMES[3] = { "NIEDRIGE PRIORITÄT", "MITTLERE PRIORITÄT", "HOHE PRIORITÄT" };
    for (int i = 0; i < 3; i++) {
        int x = i * 0xa0;
        lv_obj_t *n = mk_label(grid, NAMES[i], FONT_LH17);
        lv_obj_set_size(n, 0xa0, LV_SIZE_CONTENT);
        lv_obj_set_pos(n, x, 7);
        lv_obj_set_style_text_align(n, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_t *cell = mk_panel(grid, 0xa0, 0x6e, x, 0x20);
        lv_obj_t *col = lv_obj_create(cell);
        lv_obj_remove_style_all(col);
        lv_obj_set_size(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(col, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_row(col, 8, 0);
        lv_obj_set_style_pad_column(col, 8, 0);
        lv_obj_align(col, LV_ALIGN_TOP_MID, 0, 0x14);
        w_fee_val[i] = mk_label(col, "", FONT_LH30);
        lv_obj_t *unit = mk_label(col, "sat/vB", FONT_LH21);
        w_fee_cost[i] = mk_label(unit, "0,00 $", FONT_LH21, COL_GREEN);
        lv_obj_align_to(w_fee_cost[i], unit, LV_ALIGN_OUT_BOTTOM_MID, 0, 2);
    }
}

// FUN_4202a14c: numbers use a decimal comma ("%.2f %s" then ',' for '.', FUN_4211405c)
static void de_float(char *buf, size_t n, const char *fmt, float v, const char *unit = "")
{
    snprintf(buf, n, fmt, v, unit);
    for (char *p = buf; *p; p++) if (*p == '.') *p = ',';
}
static void fees_update()
{
    if (!valid(w_fees_root)) return;
    bool bar = cfg.showYoutubeBar && (live.streamStatus == "live" || live.streamStatus == "scheduled");   // as FUN_4202a6b4
    if (s_fees_compact != bar) { s_fees_compact = bar; fees_layout(bar); }

    int cur = cfg.currency == "EUR" ? 1 : cfg.currency == "CHF" ? 2 : 0;
    const char *sym = cur == 0 ? "$" : cur == 1 ? "€" : "CHF";
    float price = live.price[cur];
    int sats = price > 0 ? (int)(1e8f / price) : 0;              // FUN_420298e8
    if (valid(w_fees_height)) lv_label_set_text_fmt(w_fees_height, "%d", live.blockHeight);
    if (valid(w_fees_sats_cur)) lv_label_set_text(w_fees_sats_cur, cfg.currency.c_str());
    if (valid(w_fees_sats)) lv_label_set_text_fmt(w_fees_sats, "%d", sats);

    float fee[3] = { live.feeLow, live.feeMid, live.feeHigh };
    char b[24];
    for (int i = 0; i < 3; i++) {
        if (valid(w_fee_val[i]))  { de_float(b, sizeof(b), "%.1f%s", fee[i]); lv_label_set_text(w_fee_val[i], b); }
        if (valid(w_fee_cost[i])) { de_float(b, sizeof(b), "%.2f %s", live.feeCost[cur][i], sym); lv_label_set_text(w_fee_cost[i], b); }
    }
}
static void fees_destroy() { if (valid(w_fees_root)) lv_obj_del(w_fees_root); w_fees_root = nullptr; }

// ---- placeholder for pages that are not ported yet ----
static lv_obj_t *w_ph_root[PAGE_COUNT];
static void ph_build_idx(lv_obj_t *c, int idx, const char *name)
{
    w_ph_root[idx] = page_root(c);
    lv_obj_t *l = mk_label(w_ph_root[idx], name, FONT_LH36);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
}
#define PH(i, n) \
    static void ph_build_##i(lv_obj_t *c) { ph_build_idx(c, i, n); } \
    static void ph_destroy_##i() { if (valid(w_ph_root[i])) lv_obj_del(w_ph_root[i]); w_ph_root[i] = nullptr; }
PH(0, "PREIS") PH(1, "CHART") PH(4, "BITAXE") PH(6, "RECHNER") PH(7, "NEWS AUF BLOCKTRAINER.DE") PH(8, "DIFFICULTY-ANPASSUNG")

static bool pred_true() { return true; }
static bool pred_bitaxe() { return !g_bitaxe.empty(); }          // FUN_4202c5c0

// Row order = PageId. 'key_code' values are the 0x70.. codes in the table.
const PageDef PAGES[PAGE_COUNT] = {
    { PAGE_PRICE,  false, "PREIS",      "pg_price",  0x70, pred_true,   ph_build_0,   ph_destroy_0 },
    { PAGE_CHART,  false, "CHART",      "pg_chart",  0x71, pred_true,   ph_build_1,   ph_destroy_1 },
    { PAGE_BLOCK,  false, "BLOCK",      "pg_block",  0x72, pred_true,   block_build,  block_destroy },
    { PAGE_CLOCK,  false, "UHR",        "pg_clock",  0x73, pred_true,   clock_build,  clock_destroy },
    { PAGE_BITAXE, false, "BITAXE",     "pg_bitaxe", 0x75, pred_bitaxe, ph_build_4,   ph_destroy_4 },
    { PAGE_FEES,   false, "FEES",       "pg_fees",   0x74, pred_true,   fees_build,   fees_destroy },
    { PAGE_CONV,   true,  "RECHNER",    "pg_conv",   0x76, pred_true,   ph_build_6,   ph_destroy_6 },
    { PAGE_NEWS,   false, "NEWS",       "pg_news",   0x77, pred_true,   ph_build_7,   ph_destroy_7 },
    { PAGE_DIFF,   false, "DIFFICULTY", "pg_diff",   0x78, pred_true,   ph_build_8,   ph_destroy_8 },
};

static bool page_usable(int idx)       // FUN_4202c6d0: enabled and available
{
    return idx >= 0 && idx < PAGE_COUNT && cfg.pg[idx] && PAGES[idx].available();
}
static int next_page(int cur)          // FUN_4200c39c: next usable page, wrapping 0..8
{
    for (int i = 1; i <= PAGE_COUNT; i++) {
        int p = (cur + i) % PAGE_COUNT;
        if (page_usable(p) && !PAGES[p].menu_only) return p;
    }
    return cur;
}
int ui_default_page()
{
    for (int i = 0; i < PAGE_COUNT; i++) if (page_usable(i) && !PAGES[i].menu_only) return i;
    return PAGE_PRICE;
}
void ui_set_current_page(int p) { s_current_page = p; }
int ui_current_page() { return s_current_page; }

// FUN_4200c210: wraps the index, destroys every page, builds the new one, keeps the
// status bar on top.
static bool s_built[PAGE_COUNT];
static void show_page(int page)
{
    if (page < 0) page = PAGE_COUNT - 1;
    else if (page >= PAGE_COUNT) page = 0;
    for (int i = 0; i < PAGE_COUNT; i++) { if (s_built[i]) PAGES[i].destroy(); s_built[i] = false; }
    PAGES[page].build(s_container);
    s_built[page] = true;
    if (valid(s_statusbar)) lv_obj_move_foreground(s_statusbar);
    s_current_page = page;
    s_last_switch_ms = millis();
    DBG("Seite gewechselt: %d (%s)\n", page, PAGES[page].name);
}
void ui_request_page(int page, bool animate)
{
    s_pending_page = page;
    s_pending_animate = animate;
}

// ------------------------------------------------------------------ price alert (FUN_4200b61c)
// Fires once per crossing: price >= alertAbove or price <= alertBelow for the chosen
// currency; re-armed when the price leaves the zone; 60 s lockout after a trigger.
static bool s_alert_zone = false;                 // DAT_3fc9ba08
static bool s_alert_fired = false;                // 0x3fc9ba04
static uint32_t s_alert_last_ms = 0;              // 0x3fc9ba0c
static void price_alert_check()
{
    if (s_alert_arm_ms == 0 || millis() - s_alert_arm_ms <= 9999) return;
    if (!cfg.alertEnabled || (cfg.alertAbove <= 0 && cfg.alertBelow <= 0)) return;
    float price = cfg.alertCurrency == "USD" ? live.price[0] : cfg.alertCurrency == "EUR" ? live.price[1] : live.price[2];
    if (price <= 0) return;
    bool above = cfg.alertAbove > 0 && price >= cfg.alertAbove;
    bool below = cfg.alertBelow > 0 && price <= cfg.alertBelow;
    if (above || below) {
        s_alert_zone = true;
        if (!s_alert_fired) {
            if (s_alert_last_ms != 0 && millis() - s_alert_last_ms < 60000) return;
            s_alert_fired = true;
            s_alert_last_ms = millis();
            buzzer_alert();                                    // FUN_42033000
            ui_show_price_alert(above, above ? cfg.alertAbove : cfg.alertBelow, cfg.alertCurrency);
        }
    } else if (s_alert_zone) {
        s_alert_fired = false;
        s_alert_zone = false;
    }
}

// FUN_4202ce74: overlay "PREISZIEL ERREICHT". Layout UNVERIFIED (translation in ui_translated.txt not read).
void ui_show_price_alert(bool above, int level, const String &currency)
{
    ui_post([=] {
        lv_obj_t *o = lv_obj_create(lv_layer_top());
        lv_obj_set_size(o, SCREEN_W, SCREEN_H);
        lv_obj_set_style_bg_color(o, lv_color_black(), 0);
        lv_obj_t *t = mk_label(o, "PREISZIEL ERREICHT", FONT_LH36);
        lv_obj_align(t, LV_ALIGN_CENTER, 0, -20);
        char b[48];
        snprintf(b, sizeof(b), "%s %d %s", above ? "▲" : "▼", level, currency.c_str());
        lv_obj_t *s = mk_label(o, b, FONT_LH30, above ? COL_GREEN : COL_RED);
        lv_obj_align(s, LV_ALIGN_CENTER, 0, 30);
        lv_obj_add_event_cb(o, [](lv_event_t *e) { lv_obj_del((lv_obj_t *)lv_event_get_target(e)); }, LV_EVENT_CLICKED, nullptr);
    });
}

// ------------------------------------------------------------------ system screens
static lv_obj_t *w_sys;                           // full-screen overlay for Wi-Fi/OTA screens
static void sys_clear()
{
    if (valid(w_sys)) lv_obj_del(w_sys);
    w_sys = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(w_sys);
    lv_obj_set_size(w_sys, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(w_sys, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(w_sys, LV_OPA_COVER, 0);
}
static void sys_close() { if (valid(w_sys)) lv_obj_del(w_sys); w_sys = nullptr; }

static lv_obj_t *w_ota_dl_lbl, *w_ota_dl_bar, *w_ota_in_lbl, *w_ota_in_bar;
static lv_obj_t *mk_ota_bar(lv_obj_t *p, int x)
{
    lv_obj_t *b = lv_bar_create(p);
    lv_obj_set_size(b, 0x96, 7);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, x, 0x23);
    lv_bar_set_range(b, 0, 100);
    lv_bar_set_value(b, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b, COL_BAR_BG, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, COL_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_grad_dir(b, LV_GRAD_DIR_HOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    return b;
}

static void build_ota_available()      // FUN_42031224
{
    sys_clear();
    lv_obj_t *t = mk_label(w_sys, "NEUE FIRMWARE VERFÜGBAR", FONT_LH30);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0x46, -45);
    lv_obj_t *v = lv_label_create(w_sys);
    lv_label_set_text_fmt(v, "AKTUELL:   %s       NEU:   %s", FIRMWARE_VERSION, ota.remoteVersion.c_str());
    lv_obj_set_style_text_font(v, FONT_LH17, 0);
    lv_obj_align(v, LV_ALIGN_LEFT_MID, 0x48, -10);

    lv_obj_t *yes = lv_btn_create(w_sys);
    lv_obj_set_size(yes, 0x96, 0x32);
    lv_obj_align(yes, LV_ALIGN_LEFT_MID, 0x46, 0x2d);
    lv_obj_set_style_radius(yes, 5, 0);
    lv_obj_set_style_bg_color(yes, COL_ORANGE, 0);
    lv_obj_t *yl = lv_label_create(yes);
    lv_label_set_text(yl, "INSTALLIEREN");
    lv_obj_center(yl);
    lv_obj_add_event_cb(yes, [](lv_event_t *) {
        ota.stage = OTA_CONFIRMED;
        ui_show(UI_OTA_PROGRESS);
        ota_start_task();
    }, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *no = lv_btn_create(w_sys);
    lv_obj_set_size(no, 0x96, 0x32);
    lv_obj_align(no, LV_ALIGN_LEFT_MID, 0xf0, 0x2d);
    lv_obj_set_style_bg_color(no, COL_GRAY, 0);
    lv_obj_set_style_radius(no, 5, 0);
    lv_obj_t *nl = lv_label_create(no);
    lv_label_set_text(nl, "ÜBERSPRINGEN");
    lv_obj_center(nl);
    lv_obj_add_event_cb(no, [](lv_event_t *) {
        ota.stage = OTA_IDLE;
        ota.active = false;
        sys_close();
    }, LV_EVENT_CLICKED, nullptr);
}

static void build_ota_progress()       // FUN_42031500
{
    sys_clear();
    lv_obj_t *t = mk_label(w_sys, "UPDATE WIRD INSTALLIERT", FONT_LH30);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0x46, -45);
    w_ota_dl_lbl = mk_label(w_sys, "Download:  0%", FONT_LH21);
    lv_obj_align(w_ota_dl_lbl, LV_ALIGN_LEFT_MID, 0x46, 0);
    w_ota_dl_bar = mk_ota_bar(w_sys, 0x46);
    w_ota_in_lbl = mk_label(w_sys, "Installation:  0%", FONT_LH21);
    lv_obj_align(w_ota_in_lbl, LV_ALIGN_LEFT_MID, 0x109, 0);
    w_ota_in_bar = mk_ota_bar(w_sys, 0x109);
}

static void build_ota_done()           // FUN_420316e0
{
    sys_clear();
    lv_obj_t *t = mk_label(w_sys, "UPDATE ERFOLGREICH", FONT_LH30);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 0x46, -35);
    lv_obj_t *v = lv_label_create(w_sys);
    lv_label_set_text_fmt(v, "VERSION:   %s", ota.remoteVersion.c_str());
    lv_obj_set_style_text_color(v, COL_AMBER, 0);
    lv_obj_set_style_text_font(v, FONT_LH17, 0);
    lv_obj_align(v, LV_ALIGN_LEFT_MID, 0x46, -5);
    lv_obj_t *r = mk_label(w_sys, "Bitte neu starten.", FONT_LH21);
    lv_obj_align(r, LV_ALIGN_LEFT_MID, 0x46, 0x23);
}

// FUN_42020a3c: refreshes the two bars while an update runs
static void ota_ui_tick()
{
    if (ota.stage == OTA_DOWNLOADING && valid(w_ota_dl_bar)) {
        int pct = ota_progress_percent();
        lv_bar_set_value(w_ota_dl_bar, pct, LV_ANIM_OFF);
        lv_label_set_text_fmt(w_ota_dl_lbl, "Download:  %d%%", pct);
    } else if (ota.stage == OTA_INSTALLING && valid(w_ota_in_bar)) {
        lv_bar_set_value(w_ota_in_bar, 100, LV_ANIM_OFF);
        lv_label_set_text(w_ota_in_lbl, "Installation:  100%");
    } else if (ota.stage == OTA_DONE && w_ota_in_lbl) {
        w_ota_in_lbl = w_ota_dl_lbl = nullptr;
        build_ota_done();
    }
}

static void wifi_msg(const char *fmt)    // "...\nSSID: #FFA500 %s#" uses LVGL label recolor
{
    sys_clear();
    lv_obj_t *l = lv_label_create(w_sys);
    lv_label_set_recolor(l, true);
    lv_label_set_text_fmt(l, fmt, g_ssid.c_str());
    lv_obj_set_style_text_font(l, FONT_LH30, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(l);
}

// Wi-Fi setup wizard. Strings are verified ("WLAN Setup wird gestartet...", "SUCHE
// NETZWERKE", "Passwort eingeben", "VERBINDEN"); the widget layout and timings are NOT
// (FUN_42030994, WiFiScanTask, FUN_42031d88 were not translated).
static lv_obj_t *w_wiz_ta;
static void wiz_scan_task(void *)
{
    int n = WiFi.scanNetworks();
    ui_post([n] {
        sys_clear();
        mk_label(w_sys, "SUCHE NETZWERKE", FONT_LH21);
        lv_obj_t *list = lv_list_create(w_sys);
        lv_obj_set_size(list, SCREEN_W - 20, SCREEN_H - 50);
        lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);
        for (int i = 0; i < n && i < 12; i++) {
            String ssid = WiFi.SSID(i);
            lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_WIFI, ssid.c_str());
            lv_obj_add_event_cb(b, [](lv_event_t *e) {
                g_ssid = lv_list_get_btn_text(lv_obj_get_parent(lv_event_get_target(e)), (lv_obj_t *)lv_event_get_target(e));
                sys_clear();
                mk_label(w_sys, "Passwort eingeben", FONT_LH21);
                w_wiz_ta = lv_textarea_create(w_sys);
                lv_textarea_set_one_line(w_wiz_ta, true);
                lv_textarea_set_password_mode(w_wiz_ta, true);
                lv_obj_set_width(w_wiz_ta, 300);
                lv_obj_align(w_wiz_ta, LV_ALIGN_TOP_LEFT, 10, 35);
                lv_obj_t *go = lv_btn_create(w_sys);
                lv_obj_align(go, LV_ALIGN_TOP_RIGHT, -10, 35);
                lv_obj_t *gl = lv_label_create(go);
                lv_label_set_text(gl, "VERBINDEN");
                lv_obj_add_event_cb(go, [](lv_event_t *) {
                    g_pass = lv_textarea_get_text(w_wiz_ta);
                    ui_show(UI_WIFI_CONNECTING);
                    g_wifi_ok = false;
                    g_wifi_connecting = true;
                    wifi_start_reconnect_task(true);   // clears g_wifi_connecting when done
                }, LV_EVENT_CLICKED, nullptr);
                lv_obj_t *kb = lv_keyboard_create(w_sys);
                lv_keyboard_set_textarea(kb, w_wiz_ta);
            }, LV_EVENT_CLICKED, nullptr);
        }
    });
    vTaskDelete(nullptr);
}

void ui_show(UiScreen s)
{
    ui_post([s] {
        switch (s) {
            case UI_WELCOME: {
                sys_clear();
                lv_obj_t *l = mk_label(w_sys, "WLAN Setup wird gestartet...", FONT_LH30);
                lv_obj_center(l);
                break; }
            case UI_WIFI_CONNECTING: wifi_msg("Verbindung wird hergestellt. \nSSID: #FFA500 %s#"); break;
            case UI_WIFI_CONNECTED:  wifi_msg("Verbindung erfolgreich hergestellt.\nSSID: #FFA500 %s#"); break;
            case UI_WIFI_FAILED:     wifi_msg("Verbindung fehlgeschlagen!\nSSID: #FFA500 %s#"); break;
            case UI_WIFI_LOST: {
                sys_clear();
                mk_label(w_sys, "Verbindung unterbrochen!", FONT_LH30);
                break; }
            case UI_WIFI_WIZARD:
                sys_clear();
                mk_label(w_sys, "SUCHE NETZWERKE", FONT_LH21);
                xTaskCreatePinnedToCore(wiz_scan_task, "WiFiScanTask", 0x2000, nullptr, 1, nullptr, 1);
                break;
            case UI_OTA_AVAILABLE: build_ota_available(); break;
            case UI_OTA_PROGRESS:  build_ota_progress(); break;
            case UI_OTA_DONE:      build_ota_done(); break;
        }
    });
}

void ui_async_notify_wifi_lost()     { ui_show(UI_WIFI_LOST); }
void ui_async_notify_wifi_restored() { ui_post([] { sys_close(); }); }
void ui_async_notify_wifi_retry()    {}
void ui_async_setup_done()           { ui_post([] { sys_close(); }); }
void ui_async_show_first_page()      { ui_request_page(s_current_page < 0 ? ui_default_page() : s_current_page, false); }
void ui_reset_chart_cache()          {}
static lv_timer_t *s_clock_timer;
void ui_start_clock_timer(uint32_t ms)
{
    ui_post([ms] { s_clock_timer = lv_timer_create([](lv_timer_t *) { if (s_current_page == PAGE_CLOCK) clock_update(); }, ms, nullptr); });
}

// ------------------------------------------------------------------ init (FUN_4200b9c8)
void display_init()
{
    s_queue_mutex = xSemaphoreCreateMutex();
    lcd.init();
    lcd.setRotation(cfg.rotation);               // FUN_42002f00(&lcd, rotation): 1 or 3
    display_set_brightness(60);                  // DAT_3fc9bda8 = 0x3c

    lv_init();
    s_buf1 = (lv_color_t *)heap_caps_malloc(SCREEN_W * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    lv_disp_draw_buf_init(&s_draw_buf, s_buf1, nullptr, SCREEN_W * 40);
    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res = SCREEN_W;
    s_disp_drv.ver_res = SCREEN_H;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    lv_disp_drv_register(&s_disp_drv);
    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;
    lv_indev_drv_register(&s_indev_drv);

    lv_obj_set_style_bg_color(lv_scr_act(), lv_color_black(), 0);
    s_container = lv_obj_create(lv_scr_act());              // full-screen page container
    lv_obj_remove_style_all(s_container);
    lv_obj_set_size(s_container, SCREEN_W, SCREEN_H);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(s_container, LV_OPA_TRANSP, 0);

    s_statusbar = lv_obj_create(lv_scr_act());              // 30 px stream bar, hidden until the stream is live/scheduled
    lv_obj_remove_style_all(s_statusbar);
    lv_obj_set_size(s_statusbar, SCREEN_W, 30);
    lv_obj_add_flag(s_statusbar, LV_OBJ_FLAG_HIDDEN);
}

// ------------------------------------------------------------------ UI coordinator (FUN_4200c404)
void ui_task(void *)
{
    uint32_t last_tick = 0, last_version = 0;
    uint32_t last_auto = 0;
    for (;;) {
        uint32_t now = millis();

        // page switch request: executed here, then locked out for SWITCH_COOLDOWN_MS
        if (s_pending_page >= 0 && !s_switching) {
            int p = s_pending_page;
            s_pending_page = -1;
            s_switching = true;
            if (s_alert_arm_ms == 0) s_alert_arm_ms = now;
            show_page(p);
            s_switch_ms = millis();
        }
        if (s_switching && now - s_switch_ms > SWITCH_COOLDOWN_MS) s_switching = false;

        ui_drain();

        // once per second: refresh the visible page when new data arrived
        if (now - last_tick >= 1000) {
            if (last_version != live.dataVersion) {
                switch (s_current_page) {
                    case PAGE_BLOCK: block_update(); break;
                    case PAGE_FEES:  fees_update(); break;
                    default: break;       // other pages: updaters not ported yet
                }
                price_alert_check();
                last_version = live.dataVersion;
            }
            if (s_current_page == PAGE_CLOCK) clock_update();
            last_tick = now;
        }

        // auto page switch every cfg.autoPage seconds (stops while a Wi-Fi/OTA screen is up)
        if (cfg.autoPage > 0 && !s_switching && !valid(w_sys) && s_current_page >= 0) {
            if (last_auto < s_last_switch_ms) last_auto = s_last_switch_ms;
            if (now - last_auto >= (uint32_t)cfg.autoPage * 1000) {
                ui_request_page(next_page(s_current_page), false);
                last_auto = now;
            }
        } else {
            last_auto = 0;
        }

        ota_ui_tick();

        uint32_t wait = lv_timer_handler();
        vTaskDelay(wait > 5 ? 5 : wait);
    }
}
