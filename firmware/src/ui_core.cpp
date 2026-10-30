#ifdef UI_HOST_TEST
#include "host_display.h" // offline test build: framebuffer stand-in for the LovyanGFX panel
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

extern "C" void ui_free(void *ptr)
{
    heap_caps_free(ptr);
}

extern "C" void *ui_realloc(void *ptr, size_t size)
{
    void *p = heap_caps_realloc(ptr, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return p ? p : heap_caps_realloc(ptr, size, MALLOC_CAP_8BIT);
}

// ---------------------------------------------------------------- panel (see HARDWARE.md)

#ifndef UI_HOST_TEST
namespace pins
{
// 8-bit parallel bus
const int WR = 47, RD = -1, RS = 0;
const int D0 = 9, D1 = 46, D2 = 3, D3 = 8, D4 = 18, D5 = 17, D6 = 16, D7 = 15;
const int RST = 4;
const int BACKLIGHT = 45;
// capacitive touch (I2C)
const int TOUCH_SDA = 6, TOUCH_SCL = 5, TOUCH_INT = 7;
} // namespace pins

class Display : public lgfx::LGFX_Device
{
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
            c.pin_wr = pins::WR;
            c.pin_rd = pins::RD;
            c.pin_rs = pins::RS;
            c.pin_d0 = pins::D0;
            c.pin_d1 = pins::D1;
            c.pin_d2 = pins::D2;
            c.pin_d3 = pins::D3;
            c.pin_d4 = pins::D4;
            c.pin_d5 = pins::D5;
            c.pin_d6 = pins::D6;
            c.pin_d7 = pins::D7;
            bus_.config(c);
            panel_.setBus(&bus_);
        }
        {
            auto c = panel_.config();
            c.pin_cs = -1;
            c.pin_rst = pins::RST;
            c.pin_busy = -1;
            c.memory_width = 320;
            c.memory_height = 480;
            c.panel_width = 320;
            c.panel_height = 480;
            c.offset_x = 0;
            c.offset_y = 0;
            c.offset_rotation = 0;
            c.dummy_read_pixel = 8;
            c.dummy_read_bits = 1;
            c.readable = true;
            c.invert = true;
            c.rgb_order = false;
            c.dlen_16bit = false;
            c.bus_shared = true;
            panel_.config(c);
        }
        {
            auto c = light_.config();
            c.pin_bl = pins::BACKLIGHT;
            c.invert = false;
            c.freq = 44100;
            c.pwm_channel = 0;
            light_.config(c);
            panel_.setLight(&light_);
        }
        {
            auto c = touch_.config();
            c.x_min = 0;
            c.x_max = 319;
            c.y_min = 0;
            c.y_max = 479;
            c.pin_int = pins::TOUCH_INT;
            c.bus_shared = false;
            c.offset_rotation = 0;
            c.i2c_port = 0;
            c.i2c_addr = 0x38;
            c.pin_sda = pins::TOUCH_SDA;
            c.pin_scl = pins::TOUCH_SCL;
            c.freq = 400000;
            touch_.config(c);
            panel_.setTouch(&touch_);
        }
        setPanel(&panel_);
    }
};
#endif // UI_HOST_TEST

static Display lcd;
static bool s_ready = false;
static TaskHandle_t s_ui_task = nullptr;

bool ui_is_ready()
{
    return s_ready;
}

void ui_stack_report(String &out)
{
    if (s_ui_task) {
        out += String("  lvgl: ") + (unsigned)uxTaskGetStackHighWaterMark(s_ui_task) + " bytes never used\n";
    }
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
static volatile bool s_sleeping = false; // display dimmed by night mode
static bool s_swallow_touch = false;     // the touch that woke the display does not click

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
        last_x = x;
        last_y = y;
        buzzer_click();
    } else if (abs((int)x - last_x) >= 2 || abs((int)y - last_y) >= 2) {
        last_x = x;
        last_y = y; // ignore sub-2px jitter
    }
    d->point.x = last_x;
    d->point.y = last_y;
    d->state = LV_INDEV_STATE_PR;
}

void ui_wake()
{
    s_last_touch_ms = millis();
}

// ---------------------------------------------------------------- queue to the lvgl task

static std::vector<std::function<void()>> s_queue;
static SemaphoreHandle_t s_queue_mutex;

void ui_post(std::function<void()> fn)
{
    if (!s_ready) {
        return;
    }
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
    for (auto &fn : work) {
        fn();
    }
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
        if (!p) {
            return false;
        }
        uint8_t c = *p;
        if (c >= 'A' && c <= 'Z') {
            return true;
        }
        if (c == 0xC3 && forward) {
            return p[1] == 0x84 || p[1] == 0x96 || p[1] == 0x9C;
        }
        return false;
    };
    for (const uint8_t *p = s; *p; p++) {
        uint8_t c = *p;
        if (c < 0x80) {
            out += (char)c;
            continue;
        }
        if (c == 0xEF && p[1] && p[2]) { // LV_SYMBOL_* glyphs (U+F000 range)
            out += (char)c;
            out += (char)p[1];
            out += (char)p[2];
            p += 2;
            continue;
        }
        if (c == 0xC2 && p[1] == 0xB0) {
            out += "\xC2\xB0";
            p++;
            continue;
        } // degree sign is in the fonts
        if (c == 0xC3 && p[1]) {
            uint8_t d = *++p;
            const char *lower = nullptr, *upper = nullptr;
            switch (d) {
            case 0x84:
                upper = "AE";
                lower = "Ae";
                break; // Ä
            case 0x96:
                upper = "OE";
                lower = "Oe";
                break; // Ö
            case 0x9C:
                upper = "UE";
                lower = "Ue";
                break; // Ü
            case 0xA4:
                out += "ae";
                continue;
            case 0xB6:
                out += "oe";
                continue;
            case 0xBC:
                out += "ue";
                continue;
            case 0x9F:
                out += "ss";
                continue;
            default: {
                // other Latin-1 letters (e acute, c cedilla, ...) lose their accent instead of becoming '?'
                static const char *const LATIN1[64] = {
                    "A", "A", "A",     "A", nullptr, "A",     "AE", "C", "E",     "E",     "E",     "E", "I",
                    "I", "I", "I",     "D", "N",     "O",     "O",  "O", "O",     nullptr, "x",     "O", "U",
                    "U", "U", nullptr, "Y", "Th",    nullptr, "a",  "a", "a",     "a",     nullptr, "a", "ae",
                    "c", "e", "e",     "e", "e",     "i",     "i",  "i", "i",     "d",     "n",     "o", "o",
                    "o", "o", nullptr, "/", "o",     "u",     "u",  "u", nullptr, "y",     "th",    "y"};
                const char *plain = d >= 0x80 && d <= 0xBF ? LATIN1[d - 0x80] : nullptr;
                out += plain ? plain : "?";
                continue;
            }
            }
            const uint8_t *prev = (p - 2 >= s) ? p - 2 : nullptr;
            bool caps = upper_at(p + 1, true) || (prev && *prev >= 'A' && *prev <= 'Z');
            out += caps ? upper : lower;
            continue;
        }
        if (c == 0xE2 && p[1] == 0x82 && p[2] == 0xAC) {
            out += "EUR";
            p += 2;
            continue;
        } // euro sign
        if (c == 0xE2 && p[1] == 0x80 && p[2]) { // punctuation
            uint8_t d = p[2];
            p += 2;
            if (d == 0x93 || d == 0x94) {
                out += '-';
            } else if (d == 0x98 || d == 0x99) {
                out += '\'';
            } else if (d == 0x9C || d == 0x9D || d == 0x9E) {
                out += '"';
            } else if (d == 0xA6) {
                out += "...";
            } else {
                out += '?';
            }
            continue;
        }
        while ((p[1] & 0xC0) == 0x80) {
            p++; // skip any other multi-byte character
        }
        out += '?';
    }
    return out;
#endif
}

// Shortens `s` (whole UTF-8 characters) and appends "..." until it is at most `max_w` pixels wide.
// A long single word cannot be handled by LV_LABEL_LONG_DOT (it wraps at the previous space).
static String fit_width(String s, const lv_font_t *font, int max_w)
{
    auto width = [&](const String &t) {
        return (int)lv_txt_get_width(t.c_str(), t.length(), font, 0, 0);
    };
    if (width(s) <= max_w) {
        return s;
    }
    while (s.length()) {
        int cut = s.length() - 1;
        while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) {
            cut--; // back to the start of the character
        }
        s.remove(cut);
        if (width(s + "...") <= max_w) {
            break;
        }
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
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE |
                             LV_OBJ_FLAG_CLICKABLE); // plain layout box: taps reach what is behind it
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
    if (valid(label)) {
        lv_label_set_text(label, ui_tr(text.c_str()).c_str());
    }
}

String fmt_num(float value, int decimals)
{
    return String(value, decimals);
}

String fmt_int_sep(long value, bool separator)
{
    String s = String(value);
    if (!separator) {
        return s;
    }
    String out;
    int digits = 0;
    for (int i = s.length() - 1; i >= 0; i--) {
        if (s[i] != '-' && digits && digits % 3 == 0) {
            out = "," + out;
        }
        out = String(s[i]) + out;
        if (s[i] != '-') {
            digits++;
        }
    }
    return out;
}

const char *currency_code(int cur)
{
    return cur == CUR_EUR ? "EUR" : cur == CUR_CHF ? "CHF" : "USD";
}
const char *currency_symbol(int cur)
{
    return cur == CUR_USD ? "$" : cur == CUR_EUR ? "EUR" : "CHF";
}
int currency_index(const String &code)
{
    return code == "EUR" ? CUR_EUR : code == "CHF" ? CUR_CHF : CUR_USD;
}

// ---------------------------------------------------------------- design system (see ui_internal.h)

static lv_style_t s_st_panel, s_st_pill;
static bool s_styles_ready = false;

static void init_styles()
{
    if (s_styles_ready) {
        return;
    }
    s_styles_ready = true;
    lv_style_init(&s_st_panel);
    lv_style_set_bg_color(&s_st_panel, PX_CARD);
    lv_style_set_bg_opa(&s_st_panel, LV_OPA_COVER);
    lv_style_set_border_width(&s_st_panel, 1);
    lv_style_set_border_color(&s_st_panel, PX_EDGE);
    lv_style_set_radius(&s_st_panel, 14);
    lv_style_set_pad_all(&s_st_panel, 10);
    lv_style_init(&s_st_pill);
    lv_style_set_radius(&s_st_pill, LV_RADIUS_CIRCLE);
    lv_style_set_pad_column(&s_st_pill, 6);
}

lv_obj_t *mk_panel(lv_obj_t *parent, lv_coord_t w, lv_coord_t h)
{
    init_styles();
    lv_obj_t *o = mk_box(parent, w, h);
    lv_obj_add_style(o, &s_st_panel, 0);
    return o;
}

lv_obj_t *mk_pill(lv_obj_t *parent, lv_color_t bg, lv_opa_t opa, int pad_h, int pad_v)
{
    init_styles();
    lv_obj_t *p = mk_row(parent, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_add_style(p, &s_st_pill, 0);
    lv_obj_set_style_bg_color(p, bg, 0);
    lv_obj_set_style_bg_opa(p, opa, 0);
    lv_obj_set_style_pad_hor(p, pad_h, 0);
    lv_obj_set_style_pad_ver(p, pad_v, 0);
    return p;
}

lv_obj_t *mk_button(lv_obj_t *parent, const char *text, lv_color_t bg, lv_color_t fg, lv_coord_t w,
                    lv_coord_t h)
{
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, PX_EDGE, 0);
    lv_obj_center(mk_label(b, text, h >= 44 ? FONT_S : FONT_XS, fg));
    return b;
}

// Picks the largest font that fits `max_w`, so 10-digit values shrink instead of running off the card.
void fit_text(lv_obj_t *label, const String &text, const lv_font_t *const *fonts, int n, lv_coord_t max_w)
{
    String t = ui_tr(text.c_str());
    const lv_font_t *f = fonts[n - 1];
    for (int i = 0; i < n; i++) {
        if (lv_txt_get_width(t.c_str(), t.length(), fonts[i], 0, LV_TEXT_FLAG_NONE) <= max_w) {
            f = fonts[i];
            break;
        }
    }
    lv_obj_set_style_text_font(label, f, 0);
    lv_label_set_text(label, t.c_str());
}

void mk_chip(lv_obj_t *parent, Chip &c, const lv_font_t *font, int pad_h, int pad_v)
{
    c.box = mk_pill(parent, PX_UP, LV_OPA_20, pad_h, pad_v);
    c.arrow = lv_label_create(c.box);
    lv_obj_set_style_text_font(c.arrow, font, 0);
    c.pct = mk_label(c.box, "--", font, PX_UP);
    lv_obj_set_style_max_width(c.pct, 150, 0);
    lv_label_set_long_mode(c.pct, LV_LABEL_LONG_DOT);
}

void chip_set(Chip &c, float pct, bool visible)
{
    if (!c.box) {
        return;
    }
    bool down = pct < 0;
    lv_color_t col = down ? PX_DOWN : PX_UP;
    if (visible) {
        lv_obj_clear_flag(c.box, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(c.box, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_style_bg_color(c.box, col, 0);
    lv_obj_set_style_text_color(c.pct, col, 0);
    lv_obj_set_style_text_color(c.arrow, col, 0);
    lv_label_set_text(c.arrow, down ? LV_SYMBOL_DOWN : LV_SYMBOL_UP); // symbols bypass ui_tr
    String t = String(pct >= 0 ? "+" : "") + fmt_num(pct, 2) + "%";
    lv_label_set_text(c.pct, ui_tr(t.c_str()).c_str());
}

lv_obj_t *mk_info_card(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, const char *caption, lv_obj_t **cap,
                       lv_obj_t **val, lv_obj_t **extra)
{
    lv_obj_t *c = mk_panel(parent, w, h);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(c, 3, 0);
    lv_obj_t *top = mk_row(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(top, extra ? LV_FLEX_ALIGN_SPACE_BETWEEN : LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    *cap = mk_label(top, caption, FONT_XS, PX_DIM);
    if (extra) {
        *extra = mk_label(top, "", FONT_XS, PX_UP);
    }
    *val = mk_label(c, "--", FONT_S, PX_TEXT);
    return c;
}

lv_obj_t *mk_row_of_cards(lv_obj_t *parent, lv_coord_t h)
{
    lv_obj_t *r = mk_row(parent, PX_INNER_W, h);
    lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    return r;
}

Header mk_header(lv_obj_t *parent, const lv_img_dsc_t *icon, const char *title, const char *sub,
                 lv_coord_t name_w)
{
    Header h = {};
    h.row = mk_row(parent, PX_INNER_W, 40);
    lv_obj_set_flex_align(h.row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *id = mk_row(h.row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(id, 10, 0);
    lv_img_set_src(lv_img_create(id), icon);
    lv_obj_t *names = mk_col(id, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(names, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    h.title = mk_label(names, title, FONT_S, PX_TEXT);
    h.sub = mk_label(names, sub, FONT_XS, PX_DIM);
    for (lv_obj_t *l : {h.title, h.sub}) { // a long host name must not push the pills off the screen
        lv_obj_set_style_max_width(l, name_w, 0);
        lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    }
    h.right = mk_row(h.row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(h.right, 12, 0);
    return h;
}

lv_obj_t *mk_text_pill(lv_obj_t *right, const char *text)
{
    lv_obj_t *pill = mk_pill(right, PX_CARD, LV_OPA_COVER, 12, 5);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_set_style_border_color(pill, PX_EDGE, 0);
    return mk_label(pill, text, FONT_XS, PX_TEXT);
}

lv_obj_t *mk_live_pill(lv_obj_t *right, lv_obj_t **dot)
{
    lv_obj_t *label = mk_text_pill(right, "");
    lv_obj_t *pill = lv_obj_get_parent(label);
    lv_obj_t *d = lv_obj_create(pill);
    lv_obj_remove_style_all(d);
    lv_obj_move_to_index(d, 0);
    lv_obj_set_size(d, 8, 8);
    lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(d, PX_UP, 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_anim_t a; // slow pulse; the var is the dot, so deleting it stops the animation
    lv_anim_init(&a);
    lv_anim_set_var(&a, d);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_30);
    lv_anim_set_time(&a, 1100);
    lv_anim_set_playback_time(&a, 1100);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
        lv_obj_set_style_bg_opa((lv_obj_t *)o, (lv_opa_t)v, 0);
    });
    lv_anim_start(&a);
    if (dot) {
        *dot = d;
    }
    return label;
}

// ---------------------------------------------------------------- layers

static lv_obj_t *s_container;  // pages live here
static lv_obj_t *s_nav;        // invisible touch zones above the page
static lv_obj_t *s_stream_bar; // 30 px bar above the page while a stream is live or scheduled
static lv_obj_t *s_stream_label;
static lv_obj_t *s_sys;         // full-screen status screens (Wi-Fi, OTA)
static lv_obj_t *s_wifi_banner; // slim "Wi-Fi lost" strip, never blocks touch

static int s_current_page = -1;
static int s_pending_page = -1;
static bool s_switching = false;
static uint32_t s_switch_ms = 0;
static uint32_t s_last_switch_ms = 0;
static uint32_t s_first_page_ms = 0; // price alerts arm 10 s after the first page
static bool s_bar_shown = false;
static bool s_banner_shown = false;
static const int WIFI_BANNER_H = 20;
static const uint32_t SWITCH_COOLDOWN_MS = 500;

lv_obj_t *ui_screen()
{
    return lv_scr_act();
}
int ui_current_page()
{
    return s_current_page;
}
bool ui_stream_bar_visible()
{
    return s_bar_shown;
}

// The pages are flex columns, so a change of the content area (stream bar on top, Wi-Fi banner at the
// bottom) re-flows them in place without rebuilding anything.
static void layout_content()
{
    int y = s_bar_shown ? STREAM_BAR_H : 0;
    int h = SCREEN_H - y - (s_banner_shown ? WIFI_BANNER_H : 0);
    lv_obj_set_pos(s_container, 0, y);
    lv_obj_set_size(s_container, SCREEN_W, h);
    lv_obj_set_pos(s_nav, 0, y);
    lv_obj_set_size(s_nav, SCREEN_W, SCREEN_H - y); // the touch zones keep working under the banner
}

static void set_wifi_banner(bool shown)
{
    s_banner_shown = shown;
    if (valid(s_wifi_banner)) {
        if (shown) {
            lv_obj_clear_flag(s_wifi_banner, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_wifi_banner, LV_OBJ_FLAG_HIDDEN);
        }
    }
    layout_content();
}

// ---------------------------------------------------------------- page switching

bool ui_page_usable(int page)
{
    if (page < 0 || page >= PAGE_COUNT || !cfg.pg[page]) {
        return false;
    }
    return !PAGE_OPS[page].available || PAGE_OPS[page].available();
}

int ui_next_page(int from, int step)
{
    std::vector<int> order = page_order();
    if (order.empty()) {
        return from;
    }
    int n = (int)order.size();
    int pos = -1;
    for (int i = 0; i < n; i++) {
        if (order[i] == from) {
            pos = i;
        }
    }
    if (pos < 0) {
        pos = step > 0 ? -1 : 0; // current page is menu-only or unknown
    }
    for (int i = 1; i <= n; i++) {
        int candidate = order[((pos + step * i) % n + n) % n];
        if (ui_page_usable(candidate)) {
            return candidate;
        }
    }
    return from;
}

static int default_page()
{
    for (int p : page_order()) {
        if (ui_page_usable(p)) {
            return p;
        }
    }
    return PAGE_PRICE;
}

static void show_page(int page)
{
    if (page < 0 || page >= PAGE_COUNT) {
        page = default_page();
    }
    if (s_current_page >= 0 && PAGE_OPS[s_current_page].destroy) {
        PAGE_OPS[s_current_page].destroy();
    }
    ui_page_reset_led();
    PAGE_OPS[page].build(s_container);
    if (PAGE_OPS[page].update) {
        PAGE_OPS[page].update();
    }
    s_current_page = page;
    s_last_switch_ms = millis();
    lv_obj_move_foreground(s_nav);
    lv_obj_move_foreground(s_stream_bar);
    // menu-only pages (CONVERTER) have their own controls and no touch zones for page changes
    if (PAGE_INFO[page].menuOnly) {
        lv_obj_add_flag(s_nav, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_HIDDEN);
    }
    diag_stage((String("page ") + PAGE_INFO[page].name).c_str());
}

void ui_goto_page(int page)
{
    ui_post([page] {
        s_pending_page = page;
    });
}

static void nav_cb(lv_event_t *e)
{
    int step = (int)(intptr_t)lv_event_get_user_data(e);
    s_pending_page = ui_next_page(s_current_page, step);
}

static void nav_menu_cb(lv_event_t *)
{
    ui_menu_open();
}

static void build_nav()
{
    s_nav = mk_box(lv_scr_act(), SCREEN_W, SCREEN_H);
    lv_obj_set_pos(s_nav, 0, 0);
    lv_obj_clear_flag(s_nav, LV_OBJ_FLAG_CLICKABLE);
    struct Zone {
        lv_coord_t x, w;
        lv_event_cb_t cb;
        intptr_t step;
    };
    const Zone zones[3] = {
        {0, SCREEN_W / 4, nav_cb, -1},                       // left quarter: previous page
        {SCREEN_W - SCREEN_W / 4, SCREEN_W / 4, nav_cb, +1}, // right quarter: next page
        {(SCREEN_W - 180) / 2, 180, nav_menu_cb, 0},         // centre: menu
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
    lv_obj_t *col = mk_col(o, SCREEN_W - 40, LV_SIZE_CONTENT);
    lv_obj_align(col, LV_ALIGN_CENTER, 0, -10);
    lv_obj_set_style_pad_row(col, 14, 0);
    lv_obj_t *badge = mk_box(col, 64, 64);
    lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(badge, above ? PX_UP : PX_DOWN, 0);
    lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
    lv_obj_center(mk_label(badge, LV_SYMBOL_BELL, FONT_XL, lv_color_black()));
    mk_label(col, "Price target reached", FONT_L);
    String line =
        String(above ? "Above " : "Below ") + fmt_int_sep(level, cfg.priceSeparator) + " " + currency;
    lv_obj_t *pill = mk_pill(col, above ? PX_UP : PX_DOWN, LV_OPA_20, 18, 7);
    mk_label(pill, line.c_str(), FONT_M, above ? PX_UP : PX_DOWN);
    lv_obj_t *hint = mk_label(o, "Tap to dismiss", FONT_XS, PX_IDLE);
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -14);
    // never delete an object inside its own event: LVGL does it after the handler returns
    lv_obj_add_event_cb(
        o,
        [](lv_event_t *e) {
            lv_obj_del_async((lv_obj_t *)lv_event_get_target(e));
        },
        LV_EVENT_CLICKED, nullptr);
    lv_obj_del_delayed(o, 30000);
}

// Fires once per crossing of the configured level, re-arms when the price leaves the zone,
// and stays quiet for 60 s after a trigger.
static void price_alert_check()
{
    if (s_first_page_ms == 0 || millis() - s_first_page_ms < 10000) {
        return;
    }
    if (!cfg.alertEnabled || (cfg.alertAbove <= 0 && cfg.alertBelow <= 0)) {
        return;
    }
    float price = live.price[currency_index(cfg.alertCurrency)];
    if (price <= 0) {
        return;
    }

    bool above = cfg.alertAbove > 0 && price >= cfg.alertAbove;
    bool below = cfg.alertBelow > 0 && price <= cfg.alertBelow;
    if (above || below) {
        s_alert_in_zone = true;
        if (s_alert_fired) {
            return;
        }
        if (s_alert_last_ms != 0 && millis() - s_alert_last_ms < 60000) {
            return;
        }
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
    lv_obj_set_style_bg_color(s_stream_bar, PX_CARD, 0);
    lv_obj_set_style_bg_opa(s_stream_bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_stream_bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(s_stream_bar, LV_OBJ_FLAG_HIDDEN);
    s_stream_label = mk_label(s_stream_bar, "Loading stream...", FONT_S);
    lv_label_set_recolor(s_stream_label, true);
    lv_obj_align(s_stream_label, LV_ALIGN_CENTER, 0, 0);
}

static void update_stream_bar()
{
    bool active = stream_active();
    if (active != s_bar_shown) {
        s_bar_shown = active;
        if (active) {
            lv_obj_clear_flag(s_stream_bar, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(s_stream_bar, LV_OBJ_FLAG_HIDDEN);
        }
        layout_content();
        if (s_current_page >= 0) {
            s_pending_page = s_current_page; // pages rebuild for the new height
        }
    }
    if (!active) {
        return;
    }

    String text;
    if (live.streamStatus == "live") {
        String title = live.streamTitle.isEmpty() ? String("Blocktrainer") : no_hash(live.streamTitle);
        title = fit_width(ui_tr(title.c_str()), FONT_S,
                          SCREEN_W - 16 - (int)lv_txt_get_width("LIVE  ", 6, FONT_S, 0, 0));
        text = "#FF0000 LIVE#  " + title;
    } else {
        int64_t remaining = live.streamStart - (int64_t)time(nullptr);
        if (live.streamStart == 0 || time(nullptr) < 1700000000) {
            text = "Loading stream...";
        } else if (remaining <= 0) {
            text = "#FFA500 Stream starting soon#";
        } else {
            char b[48];
            snprintf(b, sizeof(b), "STREAM STARTS IN #FFA500 %02d:%02d:%02d#", (int)(remaining / 3600),
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
    if (sscanf(s.c_str(), "%d:%d", &h, &m) != 2) {
        return false;
    }
    minutes = h * 60 + m;
    return true;
}

static bool in_night_window()
{
    time_t now = time(nullptr);
    if (now < 1700000000) {
        return false; // clock not set yet
    }
    struct tm t;
    localtime_r(&now, &t);
    int cur = t.tm_hour * 60 + t.tm_min, start, stop;
    if (!parse_hhmm(cfg.nightStart, start) || !parse_hhmm(cfg.nightStop, stop)) {
        return false;
    }
    if (start == stop) {
        return false;
    }
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
        if (!s_sleeping) {
            ui_set_brightness(cfg.brightness);
        }
        if (lcd.getRotation() != cfg.rotation) {
            ui_apply_rotation();
        }
        if (s_current_page >= 0) {
            s_pending_page = s_current_page; // currency, split screen, ... changed
        }
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
    if (valid(s_sys)) {
        lv_obj_del(s_sys);
    }
    s_sys = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_sys);
    lv_obj_set_size(s_sys, SCREEN_W, SCREEN_H);
    lv_obj_set_style_bg_color(s_sys, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_sys, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_sys, LV_OBJ_FLAG_CLICKABLE); // blocks touches to the page below
}

static void sys_close()
{
    forget_sys_widgets();
    if (valid(s_sys)) {
        lv_obj_del(s_sys);
    }
    s_sys = nullptr;
}

// Round badge with a symbol, the icon of a status screen.
static lv_obj_t *mk_badge(lv_obj_t *parent, const char *symbol, lv_color_t bg)
{
    lv_obj_t *b = mk_box(parent, 56, 56);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(b, bg, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_center(mk_label(b, symbol, FONT_L, lv_color_black()));
    return b;
}

// Centred column on the status screen: badge, title, then whatever the caller adds.
static lv_obj_t *sys_column(const char *symbol, lv_color_t badge, const char *title,
                            lv_color_t title_col = PX_TEXT)
{
    lv_obj_t *col = mk_col(s_sys, SCREEN_W - 40, LV_SIZE_CONTENT);
    lv_obj_center(col);
    lv_obj_set_style_pad_row(col, 14, 0);
    mk_badge(col, symbol, badge);
    mk_label(col, title, FONT_M, title_col);
    return col;
}

static lv_obj_t *sys_pill(lv_obj_t *parent, const char *text, lv_color_t color,
                          const lv_font_t *font = FONT_S)
{
    lv_obj_t *p = mk_pill(parent, PX_CARD, LV_OPA_COVER, 16, 7);
    lv_obj_set_style_border_width(p, 1, 0);
    lv_obj_set_style_border_color(p, PX_EDGE, 0);
    return mk_label(p, text, font, color);
}

static void wifi_message(const char *symbol, lv_color_t color, const char *title)
{
    sys_clear();
    lv_obj_t *col = sys_column(symbol, color, title);
    // one line, cut with "...": a wrapped SSID could leave the screen
    String ssid = fit_width(ui_tr(g_ssid.c_str()), FONT_S, SCREEN_W - 120);
    sys_pill(col, ssid.c_str(), PX_ORANGE);
}

static lv_obj_t *mk_ota_bar(lv_obj_t *parent)
{
    lv_obj_t *b = lv_bar_create(parent);
    lv_obj_set_size(b, 400, 8);
    lv_bar_set_range(b, 0, 100);
    lv_bar_set_value(b, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(b, PX_EDGE, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, PX_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_PART_INDICATOR);
    return b;
}

static lv_obj_t *mk_button_row(lv_obj_t *parent)
{
    lv_obj_t *r = mk_row(parent, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(r, 14, 0);
    lv_obj_set_style_pad_top(r, 6, 0);
    return r;
}

static void build_ota_available()
{
    sys_clear();
    lv_obj_t *col = sys_column(LV_SYMBOL_DOWNLOAD, PX_ORANGE, "New firmware available");
    lv_obj_t *versions = mk_row(col, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_column(versions, 10, 0);
    sys_pill(versions, (String("Current  ") + FIRMWARE_VERSION).c_str(), PX_DIM, FONT_XS);
    sys_pill(versions, (String("New  ") + ota.remoteVersion).c_str(), PX_ORANGE, FONT_XS);

    lv_obj_t *buttons = mk_button_row(col);
    lv_obj_t *yes = mk_button(buttons, "INSTALL", PX_ORANGE, lv_color_black(), 190, 48);
    lv_obj_add_event_cb(
        yes,
        [](lv_event_t *) {
            ota.stage = OTA_CONFIRMED;
            ui_show(UI_OTA_PROGRESS);
            ota_start_task();
        },
        LV_EVENT_CLICKED, nullptr);
    lv_obj_t *no = mk_button(buttons, "SKIP", PX_BTN, PX_TEXT, 190, 48);
    lv_obj_add_event_cb(
        no,
        [](lv_event_t *) {
            ota.stage = OTA_IDLE;
            ota.active = false;
            ui_post(sys_close);
        },
        LV_EVENT_CLICKED, nullptr);
}

static void build_ota_progress()
{
    sys_clear();
    lv_obj_t *col = sys_column(LV_SYMBOL_DOWNLOAD, PX_ORANGE, "Installing update");
    lv_obj_set_style_pad_row(col, 8, 0);
    w_ota_dl_label = mk_label(col, "Download:  0%", FONT_S);
    w_ota_dl_bar = mk_ota_bar(col);
    lv_obj_set_style_pad_top(w_ota_dl_label, 10, 0);
    w_ota_in_label = mk_label(col, "Installation:  0%", FONT_S, PX_DIM);
    lv_obj_set_style_pad_top(w_ota_in_label, 8, 0);
    w_ota_in_bar = mk_ota_bar(col);
}

static void build_ota_done()
{
    sys_clear();
    w_ota_dl_label = w_ota_in_label = w_ota_dl_bar = w_ota_in_bar = nullptr;
    lv_obj_t *col = sys_column(LV_SYMBOL_OK, PX_UP, "Update installed");
    sys_pill(col, (String("Version  ") + ota.remoteVersion).c_str(), PX_ORANGE, FONT_XS);
    mk_label(col, "The device is restarting...", FONT_S, PX_DIM);
}

static void build_ota_error()
{
    sys_clear();
    lv_obj_t *col = sys_column(LV_SYMBOL_WARNING, PX_DOWN, "Update failed", PX_DOWN);
    lv_obj_t *e = mk_label(col, ota.error.c_str(), FONT_XS, PX_DIM);
    lv_label_set_long_mode(
        e, LV_LABEL_LONG_SCROLL_CIRCULAR); // long HTTP errors scroll instead of leaving the screen
    lv_obj_set_width(e, SCREEN_W - 80);
    lv_obj_set_style_text_align(e, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_t *ok = mk_button(col, "CLOSE", PX_BTN, PX_TEXT, 190, 48);
    lv_obj_add_event_cb(
        ok,
        [](lv_event_t *) {
            ota.active = false;
            ui_post(sys_close);
        },
        LV_EVENT_CLICKED, nullptr);
}

// Keeps the two progress bars current while an update runs.
static void ota_ui_tick()
{
    static OtaStage shown = OTA_IDLE;
    OtaStage stage = ota.stage;
    if (stage == shown && stage != OTA_DOWNLOADING) {
        return;
    }
    shown = stage;
    if (stage == OTA_DOWNLOADING && valid(w_ota_dl_bar)) {
        int pct = ota_progress_percent();
        lv_bar_set_value(w_ota_dl_bar, pct, LV_ANIM_OFF);
        set_text(w_ota_dl_label, "Download:  " + String(pct) + "%");
        shown = OTA_IDLE; // keep refreshing while downloading
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
    lv_obj_t *title =
        mk_label(s_sys, fit_width(ui_tr(("Password for " + g_ssid).c_str()), FONT_S, SCREEN_W - 20).c_str(),
                 FONT_S, PX_ORANGE);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);
    w_wizard_pass = lv_textarea_create(s_sys);
    lv_textarea_set_one_line(w_wizard_pass, true);
    lv_textarea_set_password_mode(w_wizard_pass, true);
    lv_obj_set_width(w_wizard_pass, 300);
    lv_obj_align(w_wizard_pass, LV_ALIGN_TOP_LEFT, 10, 40);
    lv_obj_set_style_bg_color(w_wizard_pass, PX_CARD, 0);
    lv_obj_set_style_border_color(w_wizard_pass, PX_EDGE, 0);
    lv_obj_set_style_radius(w_wizard_pass, 12, 0);
    lv_obj_set_style_text_color(w_wizard_pass, PX_TEXT, 0);
    lv_obj_set_style_text_font(w_wizard_pass, FONT_S, 0);
    lv_obj_set_style_border_color(w_wizard_pass, PX_ORANGE, LV_STATE_FOCUSED);
    lv_obj_set_style_bg_color(w_wizard_pass, PX_ORANGE, LV_PART_CURSOR);
    lv_obj_t *go = mk_button(s_sys, "CONNECT", PX_ORANGE, lv_color_black(), 140, 40);
    lv_obj_align(go, LV_ALIGN_TOP_RIGHT, -10, 40);
    lv_obj_add_event_cb(
        go,
        [](lv_event_t *) {
            g_pass = lv_textarea_get_text(w_wizard_pass);
            ui_show(UI_WIFI_CONNECTING);
            g_wifi_ok = false;
            g_wifi_connecting = true;
            wifi_start_reconnect_task(true); // clears g_wifi_connecting when done
        },
        LV_EVENT_CLICKED, nullptr);
    lv_obj_t *kb = lv_keyboard_create(s_sys);
    lv_keyboard_set_textarea(kb, w_wizard_pass);
    lv_obj_set_style_bg_color(kb, lv_color_black(), 0);
    lv_obj_set_style_bg_color(kb, PX_BTN, LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, PX_TEXT, LV_PART_ITEMS);
    lv_obj_set_style_text_font(
        kb, FONT_S, LV_PART_ITEMS); // the theme font would be Montserrat; symbols still fall back to it
    lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 8, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(kb, PX_EDGE, LV_PART_ITEMS | LV_STATE_CHECKED); // control keys
    lv_obj_set_style_text_color(kb, PX_ORANGE, LV_PART_ITEMS | LV_STATE_CHECKED);
}

static std::vector<WifiAp> s_wizard_nets; // what the list shows; a row remembers its index here

static void wizard_rescan_button(lv_obj_t *parent, lv_align_t align, int x, int y)
{
    lv_obj_t *b = mk_button(parent, "SCAN AGAIN", PX_ORANGE, lv_color_black(), 150, 34);
    lv_obj_align(b, align, x, y);
    lv_obj_add_event_cb(
        b,
        [](lv_event_t *) {
            ui_show(UI_WIFI_WIZARD);
        },
        LV_EVENT_CLICKED, nullptr);
}

static void wizard_show_networks(const std::vector<WifiAp> &found)
{
    sys_clear();
    s_wizard_nets = wifi_networks(found, 15); // one row per network name, strongest first
    lv_obj_t *title = mk_label(s_sys, "Select a network", FONT_S, PX_ORANGE);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 10, 8);
    if (s_wizard_nets.empty()) { // never leave a blank list: say so and offer another scan
        lv_obj_t *msg = mk_label(s_sys, "No networks found", FONT_M);
        lv_obj_align(msg, LV_ALIGN_CENTER, 0, -40);
        lv_obj_t *hint = mk_label(s_sys, "Is the router in range?", FONT_XS, PX_DIM);
        lv_obj_align(hint, LV_ALIGN_CENTER, 0, -5);
        wizard_rescan_button(s_sys, LV_ALIGN_CENTER, 0, 45);
        return;
    }
    lv_obj_t *list = lv_list_create(s_sys);
    lv_obj_set_size(list, SCREEN_W - 20, SCREEN_H - 50);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, -5);
    lv_obj_set_style_bg_opa(list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(list, 0, 0);
    lv_obj_set_style_pad_all(list, 0, 0);
    lv_obj_set_style_pad_row(list, 6, 0);
    for (size_t i = 0; i < s_wizard_nets.size(); i++) {
        lv_obj_t *b = lv_list_add_btn(list, LV_SYMBOL_WIFI, ui_tr(s_wizard_nets[i].ssid.c_str()).c_str());
        lv_obj_set_style_text_font(b, FONT_S, 0); // the list would use the theme font, which lacks umlauts
        lv_obj_set_style_bg_color(b, PX_CARD, 0);
        lv_obj_set_style_bg_color(b, PX_EDGE, LV_STATE_PRESSED);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_style_border_color(b, PX_EDGE, 0);
        lv_obj_set_style_radius(b, 12, 0);
        lv_obj_set_style_text_color(b, PX_TEXT, 0);
        lv_obj_set_user_data(b, (void *)(intptr_t)(i + 1));
        lv_obj_add_event_cb(
            b,
            [](lv_event_t *e) {
                size_t idx = (size_t)(intptr_t)lv_obj_get_user_data((lv_obj_t *)lv_event_get_target(e)) - 1;
                if (idx >= s_wizard_nets.size()) {
                    return;
                }
                g_ssid = s_wizard_nets[idx].ssid;
                ui_post(wizard_ask_password); // replaces the screen this button lives on
            },
            LV_EVENT_CLICKED, nullptr);
    }
    wizard_rescan_button(s_sys, LV_ALIGN_TOP_RIGHT, -10, 5);
}

static void wizard_scan_task(void *)
{
    WifiScan scan = wifi_scan();
    DBG("wifi: wizard scan found %d access points (code %d, %d tries)\n", (int)scan.aps.size(), scan.code,
        scan.attempts);
    ui_post([scan] {
        wizard_show_networks(scan.aps);
    });
    vTaskDelete(nullptr);
}

static void show_screen(UiScreen s)
{
    switch (s) {
    case UI_WELCOME: {
        sys_clear();
        lv_obj_t *col = mk_col(s_sys, SCREEN_W - 40, LV_SIZE_CONTENT);
        lv_obj_center(col);
        lv_obj_set_style_pad_row(col, 14, 0);
        mk_badge(col, LV_SYMBOL_WIFI, PX_ORANGE);
        mk_label(col, "Starting Wi-Fi setup...", FONT_M);
        break;
    }
    case UI_WIFI_CONNECTING:
        wifi_message(LV_SYMBOL_WIFI, PX_ORANGE, "Connecting to Wi-Fi...");
        break;
    case UI_WIFI_CONNECTED:
        wifi_message(LV_SYMBOL_OK, PX_UP, "Connected");
        break;
    case UI_WIFI_FAILED:
        wifi_message(LV_SYMBOL_WARNING, PX_DOWN, "Connection failed");
        break;
    case UI_WIFI_LOST:
        set_wifi_banner(true);
        break;
    case UI_WIFI_WIZARD: {
        sys_clear();
        lv_obj_t *col = mk_col(s_sys, SCREEN_W - 40, LV_SIZE_CONTENT);
        lv_obj_center(col);
        lv_obj_set_style_pad_row(col, 14, 0);
        mk_badge(col, LV_SYMBOL_REFRESH, PX_ORANGE);
        mk_label(col, "Searching for networks...", FONT_S);
        xTaskCreatePinnedToCore(wizard_scan_task, "WiFiScanTask", 0x3000, nullptr, 1, nullptr, 1);
        break;
    }
    case UI_OTA_AVAILABLE:
        build_ota_available();
        break;
    case UI_OTA_PROGRESS:
        build_ota_progress();
        break;
    case UI_OTA_DONE:
        build_ota_done();
        break;
    }
}

void ui_show(UiScreen s)
{
    ui_post([s] {
        show_screen(s);
    });
}
void ui_notify_wifi_lost()
{
    ui_show(UI_WIFI_LOST);
}
void ui_notify_wifi_restored()
{
    ui_post([] {
        set_wifi_banner(false);
    });
}
void ui_notify_wifi_retry() {}
void ui_setup_done()
{
    ui_post([] {
        sys_close();
    });
}

void ui_show_first_page()
{
    ui_post([] {
        s_pending_page = s_current_page < 0 ? default_page() : s_current_page;
    });
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
    lv_obj_set_size(s_wifi_banner, SCREEN_W, WIFI_BANNER_H); // the pages shrink above it
    lv_obj_set_pos(s_wifi_banner, 0, SCREEN_H - WIFI_BANNER_H);
    lv_obj_set_style_bg_color(s_wifi_banner, PX_ORANGE, 0);
    lv_obj_set_style_bg_opa(s_wifi_banner, LV_OPA_COVER, 0);
    lv_obj_clear_flag(s_wifi_banner, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_wifi_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(mk_label(s_wifi_banner, "Wi-Fi disconnected, reconnecting...", FONT_XS, lv_color_black()));
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
            if (s_first_page_ms == 0) {
                s_first_page_ms = now;
            }
            show_page(page);
            s_switch_ms = millis();
        }
        if (s_switching && now - s_switch_ms > SWITCH_COOLDOWN_MS) {
            s_switching = false;
        }

        if (now - last_second >= 1000) {
            last_second = now;
            night_mode_tick();
            update_stream_bar();
            if (s_current_page >= 0 && PAGE_OPS[s_current_page].update) {
                PAGE_OPS[s_current_page].update();
            }
            price_alert_check();
            ui_menu_tick();
        }

        // automatic page rotation (paused while a status screen or the menu is up)
        if (cfg.autoPage > 0 && !s_switching && !valid(s_sys) && !ui_menu_is_open() && s_current_page >= 0 &&
            !s_sleeping) {
            if (last_auto < s_last_switch_ms) {
                last_auto = s_last_switch_ms;
            }
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
    if (!s_ready) {
        return;
    }
    xTaskCreatePinnedToCore(ui_task, "lvgl", 0x4000, nullptr, 2, &s_ui_task, 1);
}
