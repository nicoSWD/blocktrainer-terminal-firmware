#ifdef UI_HOST_TEST
#include "host_display.h"      // offline test build: framebuffer stand-in for the LovyanGFX panel
#else
#include <LovyanGFX.hpp>
#endif
#include <WiFi.h>
#include <time.h>
#include <vector>
#include "ui.h"
#include "ui_internal.h"
#include "hw.h"
#include "ota.h"
#include "wifi_link.h"
#include "wifi_scan.h"

// ---------------------------------------------------------------- LVGL heap (see include/lv_alloc.h)

extern "C" void *ui_alloc(size_t size)
{
    void *p = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_malloc(size, MALLOC_CAP_8BIT);
}

extern "C" void ui_free(void *ptr) { heap_caps_free(ptr); }

extern "C" void *ui_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
}

// ---------------------------------------------------------------- panel (see HARDWARE.md)

#ifndef UI_HOST_TEST
namespace pins {
    // 8-bit parallel bus
    const int WR = 47, RD = -1, RS = 0;
    const int D0 = 9, D1 = 46, D2 = 3, D3 = 8, D4 = 18, D5 = 17, D6 = 16, D7 = 15;
    const int RST = 4;
    const int BACKLIGHT = 45;
    // capacitive touch (I2C)
    const int TOUCH_SDA = 6, TOUCH_SCL = 5, TOUCH_INT = 7;
}

class Display : public lgfx::LGFX_Device {
    lgfx::Panel_ST7796 panel_;
    lgfx::Bus_Parallel8 bus_;
    lgfx::Light_PWM light_;
    lgfx::Touch_FT5x06 touch_;

public:
    Display()
    {
        {
            auto c = bus_.config();
            c.freq_write = 48000000;
            c.pin_wr = pins::WR; c.pin_rd = pins::RD; c.pin_rs = pins::RS;
            c.pin_d0 = pins::D0; c.pin_d1 = pins::D1; c.pin_d2 = pins::D2; c.pin_d3 = pins::D3;
            c.pin_d4 = pins::D4; c.pin_d5 = pins::D5; c.pin_d6 = pins::D6; c.pin_d7 = pins::D7;
            bus_.config(c);
            panel_.setBus(&bus_);
        }
        {
            auto c = panel_.config();
            c.pin_cs = -1; c.pin_rst = pins::RST; c.pin_busy = -1;
            c.memory_width = 320; c.memory_height = 480;
            c.panel_width = 320; c.panel_height = 480;
            c.offset_x = 0; c.offset_y = 0; c.offset_rotation = 0;
            c.dummy_read_pixel = 8; c.dummy_read_bits = 1;
            c.readable = true; c.invert = true; c.rgb_order = false; c.dlen_16bit = false;
            c.bus_shared = true;
            panel_.config(c);
        }
        {
            auto c = light_.config();
            c.pin_bl = pins::BACKLIGHT; c.invert = false; c.freq = 44100; c.pwm_channel = 0;
            light_.config(c);
            panel_.setLight(&light_);
        }
        {
            auto c = touch_.config();
            c.x_min = 0; c.x_max = 319; c.y_min = 0; c.y_max = 479;
            c.pin_int = pins::TOUCH_INT; c.bus_shared = false; c.offset_rotation = 0;
            c.i2c_port = 0; c.i2c_addr = 0x38; c.pin_sda = pins::TOUCH_SDA; c.pin_scl = pins::TOUCH_SCL;
            c.freq = 400000;
            touch_.config(c);
            panel_.setTouch(&touch_);
        }
        setPanel(&panel_);
    }
};
#endif   // UI_HOST_TEST

static Display lcd;
static bool s_ready = false;
static TaskHandle_t s_ui_task = nullptr;

bool ui_is_ready() { return s_ready; }

void ui_stack_report(String &out)
{
    if (s_ui_task) out += String("  lvgl: ") + (unsigned)uxTaskGetStackHighWaterMark(s_ui_task) + " bytes never used\n";
}

static lv_disp_draw_buf_t s_draw_buf;
static lv_color_t *s_buf;
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

// ---------------------------------------------------------------- night mode / touch

static volatile uint32_t s_last_touch_ms = 0;
static volatile bool s_sleeping = false;       // display dimmed by night mode
static bool s_swallow_touch = false;           // the touch that woke the display does not click

static void touch_read_cb(lv_indev_drv_t *, lv_indev_data_t *d)
{
    static bool pressed = false;
    static int16_t last_x, last_y;

    uint16_t x = 0, y = 0;
    bool touched = lcd.getTouch(&x, &y);
    if (!touched) {
        pressed = false;
        s_swallow_touch = false;
        d->state = LV_INDEV_STATE_REL;
        return;
    }
    s_last_touch_ms = millis();
    if (s_sleeping) {
        s_swallow_touch = true;
        d->state = LV_INDEV_STATE_REL;
        return;
    }
    if (s_swallow_touch) {
        d->state = LV_INDEV_STATE_REL;
        return;
    }
    if (!pressed) {
        pressed = true;
        last_x = x; last_y = y;
        buzzer_click();
    } else if (abs((int)x - last_x) >= 2 || abs((int)y - last_y) >= 2) {
        last_x = x; last_y = y;      // ignore sub-2px jitter
    }
    d->point.x = last_x;
    d->point.y = last_y;
    d->state = LV_INDEV_STATE_PR;
}

void ui_wake() { s_last_touch_ms = millis(); }

// ---------------------------------------------------------------- queue to the lvgl task

static std::vector<std::function<void()>> s_queue;
static SemaphoreHandle_t s_queue_mutex;

void ui_post(std::function<void()> fn)
{
    if (!s_ready) return;
    xSemaphoreTake(s_queue_mutex, portMAX_DELAY);
    s_queue.push_back(std::move(fn));
    xSemaphoreGive(s_queue_mutex);
}

static void drain_queue()
{
    std::vector<std::function<void()>> work;
    xSemaphoreTake(s_queue_mutex, portMAX_DELAY);
    work.swap(s_queue);
    xSemaphoreGive(s_queue_mutex);
    for (auto &fn : work) fn();
}

// ---------------------------------------------------------------- text and widget helpers

// Transliterates what the built-in fonts lack. Upper-case umlauts become "AE" next to other
// capitals ("AENDERUNG") and "Ae" otherwise.
String ui_tr(const char *utf8)
{
#ifdef UI_CUSTOM_FONTS
    return String(utf8);
#else
    String out;
    const uint8_t *s = (const uint8_t *)utf8;
    auto upper_at = [&](const uint8_t *p, bool forward) {
        // looks one character away, skipping over a two-byte umlaut
        if (!p) return false;
        uint8_t c = *p;
        if (c >= 'A' && c <= 'Z') return true;
        if (c == 0xC3 && forward) return p[1] == 0x84 || p[1] == 0x96 || p[1] == 0x9C;
        return false;
    };
    for (const uint8_t *p = s; *p; p++) {
        uint8_t c = *p;
        if (c < 0x80) { out += (char)c; continue; }
        if (c == 0xEF && p[1] && p[2]) {                    // LV_SYMBOL_* glyphs (U+F000 range)
            out += (char)c; out += (char)p[1]; out += (char)p[2];
            p += 2;
            continue;
        }
        if (c == 0xC2 && p[1] == 0xB0) { out += "\xC2\xB0"; p++; continue; }   // degree sign is in the fonts
        if (c == 0xC3 && p[1]) {
            uint8_t d = *++p;
            const char *lower = nullptr, *upper = nullptr;
            switch (d) {
                case 0x84: upper = "AE"; lower = "Ae"; break;   // Ä
                case 0x96: upper = "OE"; lower = "Oe"; break;   // Ö
                case 0x9C: upper = "UE"; lower = "Ue"; break;   // Ü
                case 0xA4: out += "ae"; continue;
                case 0xB6: out += "oe"; continue;
                case 0xBC: out += "ue"; continue;
                case 0x9F: out += "ss"; continue;
                default: {
                    // other Latin-1 letters (e acute, c cedilla, ...) lose their accent instead of becoming '?'
                    static const char *const LATIN1[64] = {
                        "A", "A", "A", "A", nullptr, "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
                        "D", "N", "O", "O", "O", "O", nullptr, "x", "O", "U", "U", "U", nullptr, "Y", "Th", nullptr,
                        "a", "a", "a", "a", nullptr, "a", "ae", "c", "e", "e", "e", "e", "i", "i", "i", "i",
                        "d", "n", "o", "o", "o", "o", nullptr, "/", "o", "u", "u", "u", nullptr, "y", "th", "y" };
                    const char *plain = d >= 0x80 && d <= 0xBF ? LATIN1[d - 0x80] : nullptr;
                    out += plain ? plain : "?";
                    continue; }
            }
            const uint8_t *prev = (p - 2 >= s) ? p - 2 : nullptr;
            bool caps = upper_at(p + 1, true) || (prev && *prev >= 'A' && *prev <= 'Z');
            out += caps ? upper : lower;
            continue;
        }
        if (c == 0xE2 && p[1] == 0x82 && p[2] == 0xAC) { out += "EUR"; p += 2; continue; }   // euro sign
        if (c == 0xE2 && p[1] == 0x80 && p[2]) {                                             // punctuation
            uint8_t d = p[2];
            p += 2;
            if (d == 0x93 || d == 0x94) out += '-';
            else if (d == 0x98 || d == 0x99) out += '\'';
            else if (d == 0x9C || d == 0x9D || d == 0x9E) out += '"';
            else if (d == 0xA6) out += "...";
            else out += '?';
            continue;
        }
        while ((p[1] & 0xC0) == 0x80) p++;      // skip any other multi-byte character
        out += '?';
    }
    return out;
#endif
}

// Shortens `s` (whole UTF-8 characters) and appends "..." until it is at most `max_w` pixels wide.
// A long single word cannot be handled by LV_LABEL_LONG_DOT (it wraps at the previous space).
static String fit_width(String s, const lv_font_t *font, int max_w)
{
    auto width = [&](const String &t) { return (int)lv_txt_get_width(t.c_str(), t.length(), font, 0, 0); };
    if (width(s) <= max_w) return s;
    while (s.length()) {
        int cut = s.length() - 1;
        while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;      // back to the start of the character
        s.remove(cut);
        if (width(s + "...") <= max_w) break;
    }
    return s + "...";
}

// '#' starts a colour command in recolor labels; user-provided names must not carry one.
static String no_hash(String s)
{
    s.replace('#', ' ');
    return s;
}

lv_obj_t *mk_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, ui_tr(text).c_str());
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}

lv_obj_t *mk_box(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *mk_row(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = mk_box(parent, w, h);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return o;
}

lv_obj_t *mk_col(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = mk_box(parent, w, h);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return o;
}

lv_obj_t *mk_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    lv_obj_t *o = mk_box(parent, w, h);
    lv_obj_set_style_bg_color(o, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(o, 6, 0);
    return o;
}

void set_text(lv_obj_t *label, const String &text)
{
    if (valid(label)) lv_label_set_text(label, ui_tr(text.c_str()).c_str());
}

String fmt_de(float value, int decimals)
{
    String s = String(value, decimals);
    s.replace('.', ',');
    return s;
}

String fmt_int_sep(long value, bool separator)
{
    String s = String(value);
    if (!separator) return s;
    String out;
    int digits = 0;
    for (int i = s.length() - 1; i >= 0; i--) {
        if (s[i] != '-' && digits && digits % 3 == 0) out = "." + out;
        out = String(s[i]) + out;
        if (s[i] != '-') digits++;
    }
    return out;
}

const char *currency_code(int cur) { return cur == CUR_EUR ? "EUR" : cur == CUR_CHF ? "CHF" : "USD"; }
const char *currency_symbol(int cur) { return cur == CUR_USD ? "$" : cur == CUR_EUR ? "EUR" : "CHF"; }
int currency_index(const String &code) { return code == "EUR" ? CUR_EUR : code == "CHF" ? CUR_CHF : CUR_USD; }

// ---------------------------------------------------------------- layers

static lv_obj_t *s_container;      // pages live here
static lv_obj_t *s_nav;            // invisible touch zones above the page
static lv_obj_t *s_stream_bar;     // 30 px bar above the page while a stream is live or scheduled
static lv_obj_t *s_stream_label;
static lv_obj_t *s_sys;            // full-screen status screens (Wi-Fi, OTA)
static lv_obj_t *s_wifi_banner;    // slim "Wi-Fi lost" strip, never blocks touch

static int s_current_page = -1;
static int s_pending_page = -1;
static bool s_switching = false;
static uint32_t s_switch_ms = 0;
static uint32_t s_last_switch_ms = 0;
static uint32_t s_first_page_ms = 0;          // price alerts arm 10 s after the first page
static bool s_bar_shown = false;
static const uint32_t SWITCH_COOLDOWN_MS = 500;

lv_obj_t *ui_screen() { return lv_scr_act(); }
int ui_current_page() { return s_current_page; }
bool ui_stream_bar_visible() { return s_bar_shown; }

static void layout_content()
{
    int y = s_bar_shown ? STREAM_BAR_H : 0;
    lv_obj_set_pos(s_container, 0, y);
    lv_obj_set_size(s_container, SCREEN_W, SCREEN_H - y);
    lv_obj_set_pos(s_nav, 0, y);
    lv_obj_set_size(s_nav, SCREEN_W, SCREEN_H - y);
}

// ---------------------------------------------------------------- page switching

bool ui_page_usable(int page)
{
    if (page < 0 || page >= PAGE_COUNT || !cfg.pg[page]) return false;
    return !PAGE_OPS[page].available || PAGE_OPS[page].available();
}

int ui_next_page(int from, int step)
{
    std::vector<int> order = page_order();
    if (order.empty()) return from;
    int n = (int)order.size();
    int pos = -1;
    for (int i = 0; i < n; i++) if (order[i] == from) pos = i;
    if (pos < 0) pos = step > 0 ? -1 : 0;           // current page is menu-only or unknown
    for (int i = 1; i <= n; i++) {
        int candidate = order[((pos + step * i) % n + n) % n];
        if (ui_page_usable(candidate)) return candidate;
    }
    return from;
}

static int default_page()
{
    for (int p : page_order()) if (ui_page_usable(p)) return p;
    return PAGE_PRICE;
}

static void show_page(int page)
{
    if (page < 0 || page >= PAGE_COUNT) page = default_page();
    if (s_current_page >= 0 && PAGE_OPS[s_current_page].destroy) PAGE_OPS[s_current_page].destroy();
    ui_page_reset_led();
    PAGE_OPS[page].build(s_container);
    if (PAGE_OPS[page].update) PAGE_OPS[page].update();
    s_current_page = page;
    s_last_switch_ms = millis();
    lv_obj_move_foreground(s_nav);
    lv_obj_move_foreground(s_stream_bar);
    // menu-only pages (RECHNER) have their own controls and no touch zones for page changes
    if (PAGE_INFO[page].menuOnly) lv_obj_add_flag(s_nav, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_HIDDEN);
    diag_stage((String("page ") + PAGE_INFO[page].name).c_str());
}

void ui_goto_page(int page) { ui_post([page] { s_pending_page = page; }); }

static void nav_cb(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    s_pending_page = ui_next_page(s_current_page, step);
}

static void nav_menu_cb(lv_event_t *) { ui_menu_open(); }

static void build_nav()
{
    s_nav = mk_box(lv_scr_act(), SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_nav, 0, 0);
    lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_CLICKABLE);
    struct Zone { lv_coord_t x, w; lv_event_cb_t cb; intptr_t step; };
    const Zone zones[3] = {
        { 0,   SCREEN_W / 4,     nav_cb,      -1 },      // left quarter: previous page
        { SCREEN_W - SCREEN_W / 4, SCREEN_W / 4, nav_cb, +1 },   // right quarter: next page
        { (SCREEN_W - 180) / 2, 180, nav_menu_cb, 0 },   // centre: menu
    };
    for (const Zone &z : zones) {
        lv_obj_t *b = lv_btn_create(s_nav);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, z.w, LV_PCT(100));
        lv_obj_set_pos(b, z.x, 0);
        lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(b, z.cb, LV_EVENT_CLICKED, (void *)z.step);
    }
}

// ---------------------------------------------------------------- price alert

static bool s_alert_in_zone = false;
static bool s_alert_fired = false;
static uint32_t s_alert_last_ms = 0;

static void show_alert_overlay(bool above, int level, const String &currency)
{
    lv_obj_t *o = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *t = mk_label(o, "PREISZIEL ERREICHT", FONT_L);
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -30);
    String line = String(above ? "ueber " : "unter ") + fmt_int_sep(level, cfg.priceSeparator) + " " + currency;
    lv_obj_t *s = mk_label(o, line.c_str(), FONT_M, above ? COL_GREEN : COL_RED);
    lv_obj_align(s, LV_ALIGN_CENTER, 0, 20);
    lv_obj_t *hint = mk_label(o, "Tippen zum Schliessen", FONT_XS, COL_GRAY);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -12);
    // never delete an object inside its own event: LVGL does it after the handler returns
    lv_obj_add_event_cb(o, [](lv_event_t *e) { lv_obj_del_async((lv_obj_t *)lv_event_get_target(e)); },
                        LV_EVENT_CLICKED, nullptr);
    lv_obj_del_delayed(o, 30000);
}

// Fires once per crossing of the configured level, re-arms when the price leaves the zone,
// and stays quiet for 60 s after a trigger.
static void price_alert_check()
{
    if (s_first_page_ms == 0 || millis() - s_first_page_ms < 10000) return;
    if (!cfg.alertEnabled || (cfg.alertAbove <= 0 && cfg.alertBelow <= 0)) return;
    float price = live.price[currency_index(cfg.alertCurrency)];
    if (price <= 0) return;

    bool above = cfg.alertAbove > 0 && price >= cfg.alertAbove;
    bool below = cfg.alertBelow > 0 && price <= cfg.alertBelow;
    if (above || below) {
        s_alert_in_zone = true;
        if (s_alert_fired) return;
        if (s_alert_last_ms != 0 && millis() - s_alert_last_ms < 60000) return;
        s_alert_fired = true;
        s_alert_last_ms = millis();
        buzzer_alert();
        show_alert_overlay(above, above ? cfg.alertAbove : cfg.alertBelow, cfg.alertCurrency);
    } else if (s_alert_in_zone) {
        s_alert_in_zone = false;
        s_alert_fired = false;
    }
}

// ---------------------------------------------------------------- stream bar

static bool stream_active()
{
    return cfg.showYoutubeBar && live.streamKnown &&
           (live.streamStatus == "live" || live.streamStatus == "scheduled");
}

static void build_stream_bar()
{
    s_stream_bar = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(s_stream_bar);
    lv_obj_set_size(s_stream_bar, SCREEN_W, STREAM_BAR_H);
    lv_obj_set_pos(s_stream_bar, 0, 0);
    lv_obj_set_style_bg_color(s_stream_bar, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(s_stream_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_stream_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_stream_bar, LV_OBJ_FLAG_HIDDEN);
    s_stream_label = mk_label(s_stream_bar, "Stream laedt...", FONT_S);
    lv_label_set_recolor(s_stream_label, true);
    lv_obj_align(s_stream_label, LV_ALIGN_CENTER, 0, 0);
}

static void update_stream_bar()
{
    bool active = stream_active();
    if (active != s_bar_shown) {
        s_bar_shown = active;
        if (active) lv_obj_clear_flag(s_stream_bar, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(s_stream_bar, LV_OBJ_FLAG_HIDDEN);
        layout_content();
        if (s_current_page >= 0) s_pending_page = s_current_page;     // pages rebuild for the new height
    }
    if (!active) return;

    String text;
    if (live.streamStatus == "live") {
        String title = live.streamTitle.isEmpty() ? String("Blocktrainer") : no_hash(live.streamTitle);
        title = fit_width(ui_tr(title.c_str()), FONT_S, SCREEN_W - 16 - (int)lv_txt_get_width("LIVE  ", 6, FONT_S, 0, 0));
        text = "#FF0000 LIVE#  " + title;
    } else {
        int64_t remaining = live.streamStart - (int64_t)time(nullptr);
        if (live.streamStart == 0 || time(nullptr) < 1700000000) {
            text = "Stream laedt...";
        } else if (remaining <= 0) {
            text = "#FFA500 Stream startet gleich#";
        } else {
            char b[48];
            snprintf(b, sizeof(b), "LIVESTREAM IN #FFA500 %02d:%02d:%02d#", (int)(remaining / 3600),
                     (int)(remaining / 60 % 60), (int)(remaining % 60));
            text = b;
        }
    }
    set_text(s_stream_label, text);
    lv_obj_align(s_stream_label, LV_ALIGN_CENTER, 0, 0);
}

// ---------------------------------------------------------------- brightness, rotation, night mode

void ui_set_brightness(int percent)
{
    lcd.setBrightness((uint8_t)(constrain(percent, 0, 100) * 255 / 100));
}

void ui_apply_rotation()
{
    lcd.setRotation(cfg.rotation);
    lv_obj_invalidate(lv_scr_act());
}

static bool parse_hhmm(const String &s, int &minutes)
{
    int h, m;
    if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return false;
    minutes = h * 60 + m;
    return true;
}

static bool in_night_window()
{
    time_t now = time(nullptr);
    if (now < 1700000000) return false;                 // clock not set yet
    struct tm t;
    localtime_r(&now, &t);
    int cur = t.tm_hour * 60 + t.tm_min, start, stop;
    if (!parse_hhmm(cfg.nightStart, start) || !parse_hhmm(cfg.nightStop, stop)) return false;
    if (start == stop) return false;
    return start < stop ? (cur >= start && cur < stop) : (cur >= start || cur < stop);
}

// During the night window the display goes dark after `nightDelay` seconds without a touch
// and wakes on touch.
static void night_mode_tick()
{
    bool want_dark = cfg.nightActive && in_night_window() &&
                     millis() - s_last_touch_ms > (uint32_t)cfg.nightDelay * 1000UL;
    if (want_dark && !s_sleeping) {
        s_sleeping = true;
        ui_set_brightness(0);
        led_save();
        led_off();
    } else if (!want_dark && s_sleeping) {
        s_sleeping = false;
        ui_set_brightness(cfg.brightness);
        led_restore();
    }
}

void ui_settings_changed()
{
    ui_post([] {
        if (!s_sleeping) ui_set_brightness(cfg.brightness);
        if (lcd.getRotation() != cfg.rotation) ui_apply_rotation();
        if (s_current_page >= 0) s_pending_page = s_current_page;     // currency, split screen, ... changed
    });
}

// ---------------------------------------------------------------- status screens (Wi-Fi, OTA)

static lv_obj_t *w_ota_dl_label, *w_ota_dl_bar, *w_ota_in_label, *w_ota_in_bar;
static lv_obj_t *w_wizard_pass;

// Child widgets die with their screen: forget the pointers, a recycled address could otherwise
// pass lv_obj_is_valid() and make the next update write into an unrelated object.
static void forget_sys_widgets()
{
    w_ota_dl_label = w_ota_dl_bar = w_ota_in_label = w_ota_in_bar = nullptr;
    w_wizard_pass = nullptr;
}

static void sys_clear()
{
    forget_sys_widgets();
    if (valid(s_sys)) lv_obj_del(s_sys);
    s_sys = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_sys);
    lv_obj_set_size(s_sys, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_sys, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sys, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_sys, LV_OBJ_FLAG_CLICKABLE);       // blocks touches to the page below
}

static void sys_close()
{
    forget_sys_widgets();
    if (valid(s_sys)) lv_obj_del(s_sys);
    s_sys = nullptr;
}

static void wifi_message(const char *format)             // "%s" is the SSID, shown in orange
{
    sys_clear();
    lv_obj_t *l = lv_label_create(s_sys);
    lv_label_set_recolor(l, true);
    char buf[160];
    // one line, cut with "...": LVGL 8.3 drops the recolor state at a line break, a wrapped SSID would turn white
    String ssid = fit_width(ui_tr(no_hash(g_ssid).c_str()), FONT_M,
                            SCREEN_W - 24 - (int)lv_txt_get_width("SSID: ", 6, FONT_M, 0, 0));
    snprintf(buf, sizeof(buf), format, ssid.c_str());
    lv_label_set_text(l, ui_tr(buf).c_str());
    lv_obj_set_style_text_font(l, FONT_M, 0);
    lv_obj_set_style_text_color(l, COL_WHITE, 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, SCREEN_W - 20);                  // the first line must wrap, never leave the screen
    lv_obj_center(l);
}

static lv_obj_t *mk_ota_bar(lv_obj_t *parent, lv_coord_t x)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, 150, 7);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, x, 35);
    lv_bar_set_range(b, 0, 100);
    lv_bar_set_value(b, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(b, COL_TRACK, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, COL_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    return b;
}

static lv_obj_t *mk_button(lv_obj_t *parent, const char *text, lv_color_t color, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 150, 50);
    lv_obj_align(b, LV_ALIGN_LEFT_MID, x, y);
    lv_obj_set_style_radius(b, 5, 0);
    lv_obj_set_style_bg_color(b, color, 0);
    lv_obj_t *l = mk_label(b, text, FONT_XS);
    lv_obj_center(l);
    return b;
}

static void build_ota_available()
{
    sys_clear();
    lv_obj_t *t = mk_label(s_sys, "NEUE FIRMWARE VERFUEGBAR", FONT_M);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 70, -45);
    char v[96];
    snprintf(v, sizeof(v), "AKTUELL:   %s       NEU:   %s", FIRMWARE_VERSION, ota.remoteVersion.c_str());
    lv_obj_t *vl = mk_label(s_sys, v, FONT_XS);
    lv_obj_align(vl, LV_ALIGN_LEFT_MID, 72, -10);

    lv_obj_t *yes = mk_button(s_sys, "INSTALLIEREN", COL_ORANGE, 70, 45);
    lv_obj_add_event_cb(yes, [](lv_event_t *) {
        ota.stage = OTA_CONFIRMED;
        ui_show(UI_OTA_PROGRESS);
        ota_start_task();
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *no = mk_button(s_sys, "UEBERSPRINGEN", c565(0x52AA), 240, 45);
    lv_obj_add_event_cb(no, [](lv_event_t *) {
        ota.stage = OTA_IDLE;
        ota.active = false;
        ui_post(sys_close);
    }, LV_EVENT_CLICKED, nullptr);
}

static void build_ota_progress()
{
    sys_clear();
    lv_obj_t *t = mk_label(s_sys, "UPDATE WIRD INSTALLIERT", FONT_M);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 70, -45);
    w_ota_dl_label = mk_label(s_sys, "Download:  0%", FONT_S);
    lv_obj_align(w_ota_dl_label, LV_ALIGN_LEFT_MID, 70, 0);
    w_ota_dl_bar = mk_ota_bar(s_sys, 70);
    w_ota_in_label = mk_label(s_sys, "Installation:  0%", FONT_S);
    lv_obj_align(w_ota_in_label, LV_ALIGN_LEFT_MID, 265, 0);
    w_ota_in_bar = mk_ota_bar(s_sys, 265);
}

static void build_ota_done()
{
    sys_clear();
    w_ota_dl_label = w_ota_in_label = w_ota_dl_bar = w_ota_in_bar = nullptr;
    lv_obj_t *t = mk_label(s_sys, "UPDATE ERFOLGREICH", FONT_M);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 70, -35);
    String v = "VERSION:   " + ota.remoteVersion;
    lv_obj_t *vl = mk_label(s_sys, v.c_str(), FONT_XS, COL_AMBER);
    lv_obj_align(vl, LV_ALIGN_LEFT_MID, 70, -5);
    lv_obj_t *r = mk_label(s_sys, "Das Geraet startet neu...", FONT_S);
    lv_obj_align(r, LV_ALIGN_LEFT_MID, 70, 35);
}

static void build_ota_error()
{
    sys_clear();
    lv_obj_t *t = mk_label(s_sys, "UPDATE FEHLGESCHLAGEN", FONT_M, COL_RED);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 70, -35);
    lv_obj_t *e = mk_label(s_sys, ota.error.c_str(), FONT_XS);
    lv_label_set_long_mode(e, LV_LABEL_LONG_SCROLL_CIRCULAR);        // long HTTP errors scroll instead of leaving the screen
    lv_obj_set_width(e, SCREEN_W - 80);
    lv_obj_align(e, LV_ALIGN_LEFT_MID, 70, -5);
    lv_obj_t *ok = mk_button(s_sys, "SCHLIESSEN", c565(0x52AA), 70, 45);
    lv_obj_add_event_cb(ok, [](lv_event_t *) { ota.active = false; ui_post(sys_close); }, LV_EVENT_CLICKED, nullptr);
}

// Keeps the two progress bars current while an update runs.
static void ota_ui_tick()
{
    static OtaStage shown = OTA_IDLE;
    OtaStage stage = ota.stage;
    if (stage == shown && stage != OTA_DOWNLOADING) return;
    shown = stage;
    if (stage == OTA_DOWNLOADING && valid(w_ota_dl_bar)) {
        int pct = ota_progress_percent();
        lv_bar_set_value(w_ota_dl_bar, pct, LV_ANIM_OFF);
        set_text(w_ota_dl_label, "Download:  " + String(pct) + "%");
        shown = OTA_IDLE;       // keep refreshing while downloading
    } else if (stage == OTA_INSTALLING && valid(w_ota_in_bar)) {
        lv_bar_set_value(w_ota_dl_bar, 100, LV_ANIM_OFF);
        lv_bar_set_value(w_ota_in_bar, 100, LV_ANIM_OFF);
        set_text(w_ota_dl_label, "Download:  100%");
        set_text(w_ota_in_label, "Installation:  100%");
    } else if (stage == OTA_DONE) {
        build_ota_done();
    } else if (stage == OTA_ERROR && valid(s_sys) && ota.active) {
        build_ota_error();
    }
}

// ---- Wi-Fi setup wizard: network list, then password entry

static void wizard_ask_password()
{
    sys_clear();
    lv_obj_t *title = mk_label(s_sys, fit_width(ui_tr(("Passwort fuer " + g_ssid).c_str()), FONT_S, SCREEN_W - 20).c_str(),
                               FONT_S, COL_ORANGE);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);
    w_wizard_pass = lv_textarea_create(s_sys);
    lv_textarea_set_one_line(w_wizard_pass, true);
    lv_textarea_set_password_mode(w_wizard_pass, true);
    lv_obj_set_width(w_wizard_pass, 300);
    lv_obj_align(w_wizard_pass, LV_ALIGN_TOP_LEFT, 10, 40);
    lv_obj_t *go = lv_btn_create(s_sys);
    lv_obj_set_size(go, 140, 40);
    lv_obj_align(go, LV_ALIGN_TOP_RIGHT, -10, 40);
    lv_obj_set_style_bg_color(go, COL_ORANGE, 0);
    lv_obj_center(mk_label(go, "VERBINDEN", FONT_XS));
    lv_obj_add_event_cb(go, [](lv_event_t *) {
        g_pass = lv_textarea_get_text(w_wizard_pass);
        ui_show(UI_WIFI_CONNECTING);
        g_wifi_ok = false;
        g_wifi_connecting = true;
        wifi_start_reconnect_task(true);       // clears g_wifi_connecting when done
    }, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *kb = lv_keyboard_create(s_sys);
    lv_keyboard_set_textarea(kb, w_wizard_pass);
}

static std::vector<WifiAp> s_wizard_nets;       // what the list shows; a row remembers its index here

static void wizard_rescan_button(lv_obj_t *parent, lv_align_t align, int x, int y)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, 150, 34);
    lv_obj_align(b, align, x, y);
    lv_obj_set_style_bg_color(b, COL_ORANGE, 0);
    lv_obj_center(mk_label(b, "NEU SUCHEN", FONT_XS));
    lv_obj_add_event_cb(b, [](lv_event_t *) { ui_show(UI_WIFI_WIZARD); }, LV_EVENT_CLICKED, nullptr);
}

static void wizard_show_networks(const std::vector<WifiAp> &found)
{
    sys_clear();
    s_wizard_nets = wifi_networks(found, 15);      // one row per network name, strongest first
    lv_obj_t *title = mk_label(s_sys, "WLAN AUSWAEHLEN", FONT_S, COL_ORANGE);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);
    if (s_wizard_nets.empty()) {                   // never leave a blank list: say so and offer another scan
        lv_obj_t *msg = mk_label(s_sys, "KEINE NETZWERKE GEFUNDEN", FONT_S);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, -40);
        lv_obj_t *hint = mk_label(s_sys, "Ist der Router in Reichweite?", FONT_XS);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, -5);
        wizard_rescan_button(s_sys, LV_ALIGN_CENTER, 0, 45);
        return;
    }
    lv_obj_t *list = lv_list_create(s_sys);
    lv_obj_set_size(list, SCREEN_W - 20, SCREEN_H - 50);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);
    for (size_t i = 0; i < s_wizard_nets.size(); i++) {
        lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_WIFI, ui_tr(s_wizard_nets[i].ssid.c_str()).c_str());
        lv_obj_set_user_data(b, (void *)(intptr_t)(i + 1));
        lv_obj_add_event_cb(b, [](lv_event_t *e) {
            size_t idx = (size_t)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e)) - 1;
            if (idx >= s_wizard_nets.size()) return;
            g_ssid = s_wizard_nets[idx].ssid;
            ui_post(wizard_ask_password);       // replaces the screen this button lives on
        }, LV_EVENT_CLICKED, nullptr);
    }
    wizard_rescan_button(s_sys, LV_ALIGN_TOP_RIGHT, -10, 5);
}

static void wizard_scan_task(void *)
{
    WifiScan scan = wifi_scan();
    DBG("wifi: wizard scan found %d access points (code %d, %d tries)\n", (int)scan.aps.size(), scan.code, scan.attempts);
    ui_post([scan] { wizard_show_networks(scan.aps); });
    vTaskDelete(nullptr);
}

static void show_screen(UiScreen s)
{
    switch (s) {
        case UI_WELCOME: {
            sys_clear();
            lv_obj_center(mk_label(s_sys, "WLAN Setup wird gestartet...", FONT_M));
            break; }
        case UI_WIFI_CONNECTING: wifi_message("Verbindung wird hergestellt.\nSSID: #FFA500 %s#"); break;
        case UI_WIFI_CONNECTED:  wifi_message("Verbindung erfolgreich hergestellt.\nSSID: #FFA500 %s#"); break;
        case UI_WIFI_FAILED:     wifi_message("Verbindung fehlgeschlagen!\nSSID: #FFA500 %s#"); break;
        case UI_WIFI_LOST:
            if (valid(s_wifi_banner)) lv_obj_clear_flag(s_wifi_banner, LV_OBJ_FLAG_HIDDEN);
            break;
        case UI_WIFI_WIZARD: {
            sys_clear();
            lv_obj_center(mk_label(s_sys, "SUCHE NETZWERKE", FONT_S));
            xTaskCreatePinnedToCore(wizard_scan_task, "WiFiScanTask", 0x3000, nullptr, 1, nullptr, 1);
            break; }
        case UI_OTA_AVAILABLE: build_ota_available(); break;
        case UI_OTA_PROGRESS:  build_ota_progress(); break;
        case UI_OTA_DONE:      build_ota_done(); break;
    }
}

void ui_show(UiScreen s) { ui_post([s] { show_screen(s); }); }
void ui_notify_wifi_lost() { ui_show(UI_WIFI_LOST); }
void ui_notify_wifi_restored()
{
    ui_post([] { if (valid(s_wifi_banner)) lv_obj_add_flag(s_wifi_banner, LV_OBJ_FLAG_HIDDEN); });
}
void ui_notify_wifi_retry() {}
void ui_setup_done() { ui_post([] { sys_close(); }); }

void ui_show_first_page()
{
    ui_post([] { s_pending_page = s_current_page < 0 ? default_page() : s_current_page; });
}

// ---------------------------------------------------------------- init and task

bool ui_init()
{
    s_queue_mutex = xSemaphoreCreateMutex();
    if (!lcd.init()) {
        Serial.println("ui: display init failed, running headless");
        return false;
    }
    lcd.setRotation(cfg.rotation);
    ui_set_brightness(60);

    lv_init();
    s_buf = (lv_color_t *)heap_caps_malloc(SCREEN_W * 40 * sizeof(lv_color_t), MALLOC_CAP_DMA);
    if (!s_buf) {
        Serial.println("ui: no memory for the draw buffer, running headless");
        return false;
    }
    lv_disp_draw_buf_init(&s_draw_buf, s_buf, nullptr, SCREEN_W * 40);
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
    lv_obj_clear_flag(lv_scr_act(), LV_OBJ_FLAG_SCROLLABLE);
    s_container = mk_box(lv_scr_act(), SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_container, 0, 0);
    build_nav();
    build_stream_bar();
    s_wifi_banner = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_wifi_banner);
    lv_obj_set_size(s_wifi_banner, SCREEN_W, 20);                  // slim: keeps page content readable
    lv_obj_set_pos(s_wifi_banner, 0, SCREEN_H - 20);
    lv_obj_set_style_bg_color(s_wifi_banner, c565(0xC3E1), 0);
    lv_obj_set_style_bg_opa(s_wifi_banner, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_wifi_banner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wifi_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(mk_label(s_wifi_banner, "WLAN getrennt, verbinde neu...", FONT_XS, lv_color_black()));
    s_last_touch_ms = millis();
    s_ready = true;
    return true;
}

static void ui_task(void *)
{
    uint32_t last_second = 0, last_auto = millis();
    for (;;) {
        uint32_t now = millis();
        drain_queue();

        // a page switch runs here, then locks further switches out for a moment
        if (s_pending_page >= 0 && !s_switching) {
            int page = s_pending_page;
            s_pending_page = -1;
            s_switching = true;
            if (s_first_page_ms == 0) s_first_page_ms = now;
            show_page(page);
            s_switch_ms = millis();
        }
        if (s_switching && now - s_switch_ms > SWITCH_COOLDOWN_MS) s_switching = false;

        if (now - last_second >= 1000) {
            last_second = now;
            night_mode_tick();
            update_stream_bar();
            if (s_current_page >= 0 && PAGE_OPS[s_current_page].update) PAGE_OPS[s_current_page].update();
            price_alert_check();
            ui_menu_tick();
        }

        // automatic page rotation (paused while a status screen or the menu is up)
        if (cfg.autoPage > 0 && !s_switching && !valid(s_sys) && !ui_menu_is_open() && s_current_page >= 0 &&
            !s_sleeping) {
            if (last_auto < s_last_switch_ms) last_auto = s_last_switch_ms;
            if (now - last_auto >= (uint32_t)cfg.autoPage * 1000UL) {
                s_pending_page = ui_next_page(s_current_page, +1);
                last_auto = now;
            }
        } else {
            last_auto = now;
        }

        ota_ui_tick();
        led_tick();

        uint32_t wait = lv_timer_handler();
        vTaskDelay(wait > 5 ? 5 : (wait ? wait : 1));
    }
}

void ui_start()
{
    if (!s_ready) return;
    xTaskCreatePinnedToCore(ui_task, "lvgl", 0x4000, nullptr, 2, &s_ui_task, 1);
}
