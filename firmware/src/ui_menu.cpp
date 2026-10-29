// The configuration menu opened from the centre of any page: quick settings, a QR code for the
// web configuration page, a brightness slider and a page overview.

#include <WiFi.h>
#include "ui_internal.h"

static lv_obj_t *w_menu;                  // overlay, null while closed
static lv_obj_t *w_settings_view, *w_pages_view;
static lv_obj_t *w_qr, *w_ip_label, *w_brightness;
static lv_obj_t *w_currency_btn[3];
static lv_obj_t *w_alert_sw, *w_night_sw, *w_stream_sw;
static lv_obj_t *w_auto_value, *w_chart_value;
static lv_obj_t *w_page_tile[PAGE_COUNT];
static lv_obj_t *w_mode_label;
static String s_shown_ip;
static bool s_pages_mode = false;

static const int AUTO_PAGE_STEPS[4] = { 0, 5, 15, 30 };           // seconds, 0 = off
static const int ROW1_Y = 98, ROW2_Y = 185, CELL_W = 156, CELL_H = 79;

bool ui_menu_is_open() { return valid(w_menu); }

// ---------------------------------------------------------------- small widgets

static lv_obj_t *mk_cell(lv_obj_t *parent, int col, int y, const char *caption)
{
    lv_obj_t *c = mk_box(parent, CELL_W, CELL_H);
    lv_obj_set_pos(c, col * CELL_W + 6, y);
    lv_obj_t *l = mk_label(c, caption, FONT_XS);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 6, 4);
    return c;
}

static lv_obj_t *mk_switch_row(lv_obj_t *cell, bool on, lv_event_cb_t cb)
{
    lv_obj_t *row = mk_row(cell, CELL_W - 8, 34);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_pad_column(row, 8, 0);
    mk_label(row, "AUS", FONT_XS, COL_GRAY);
    lv_obj_t *sw = lv_switch_create(row);
    lv_obj_set_size(sw, 40, 20);
    lv_obj_set_style_bg_color(sw, COL_ORANGE, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, nullptr);
    mk_label(row, "EIN", FONT_XS, COL_GRAY);
    return sw;
}

// "-  value  +" stepper
static void mk_stepper(lv_obj_t *cell, lv_obj_t **value, lv_event_cb_t minus, lv_event_cb_t plus)
{
    lv_obj_t *row = mk_row(cell, CELL_W - 8, 38);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -4);
    lv_obj_set_style_pad_column(row, 4, 0);

    lv_obj_t *less = lv_btn_create(row);
    lv_obj_set_size(less, 38, 34);
    lv_obj_set_style_bg_color(less, COL_BTN, 0);
    lv_obj_set_style_radius(less, 4, 0);
    lv_obj_center(mk_label(less, "-", FONT_S));
    lv_obj_add_event_cb(less, minus, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *box = mk_box(row, 60, 34);
    lv_obj_set_style_bg_color(box, COL_BTN, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 4, 0);
    *value = mk_label(box, "", FONT_XS);
    lv_obj_center(*value);

    lv_obj_t *more = lv_btn_create(row);
    lv_obj_set_size(more, 38, 34);
    lv_obj_set_style_bg_color(more, COL_BTN, 0);
    lv_obj_set_style_radius(more, 4, 0);
    lv_obj_center(mk_label(more, "+", FONT_S));
    lv_obj_add_event_cb(more, plus, LV_EVENT_CLICKED, nullptr);
}

// ---------------------------------------------------------------- setting handlers

static void refresh_settings()
{
    if (!valid(w_menu)) return;
    int cur = currency_index(cfg.currency);
    for (int i = 0; i < 3; i++) {
        bool active = (i == 0 ? CUR_EUR : i == 1 ? CUR_USD : CUR_CHF) == cur;
        if (valid(w_currency_btn[i])) lv_obj_set_style_bg_color(w_currency_btn[i], active ? COL_ORANGE : COL_BTN, 0);
    }
    auto sync = [](lv_obj_t *sw, bool on) {
        if (!valid(sw)) return;
        if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
        else lv_obj_clear_state(sw, LV_STATE_CHECKED);
    };
    sync(w_alert_sw, cfg.alertEnabled);
    sync(w_night_sw, cfg.nightActive);
    sync(w_stream_sw, cfg.showYoutubeBar);
    set_text(w_auto_value, cfg.autoPage == 0 ? String("AUS") : String(cfg.autoPage) + " s");
    set_text(w_chart_value, cfg.chartTf == 3 ? "5d" : cfg.chartTf == 2 ? "3d" : "24h");
}

static void on_currency(lv_event_t *e)
{
    static const char *CODES[3] = { "EUR", "USD", "CHF" };
    cfg.currency = CODES[(int)(intptr_t)lv_event_get_user_data(e)];
    prefs.putString("currency", cfg.currency);
    refresh_settings();
}

static void on_alert(lv_event_t *e)
{
    cfg.alertEnabled = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    prefs.putBool("alertEnabled", cfg.alertEnabled);
}

static void on_night(lv_event_t *e)
{
    cfg.nightActive = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    prefs.putBool("nightActive", cfg.nightActive);
}

static void on_stream(lv_event_t *e)
{
    cfg.showYoutubeBar = lv_obj_has_state(lv_event_get_target(e), LV_STATE_CHECKED);
    prefs.putBool("showYoutubeBar", cfg.showYoutubeBar);
    net_apply_page_flags();
}

static int auto_step_index()
{
    for (int i = 3; i >= 0; i--) if (cfg.autoPage >= AUTO_PAGE_STEPS[i]) return i;
    return 0;
}

static void step_auto(int dir)
{
    int idx = constrain(auto_step_index() + dir, 0, 3);
    cfg.autoPage = AUTO_PAGE_STEPS[idx];
    prefs.putInt("autoPage", cfg.autoPage);
    refresh_settings();
}

static void step_chart(int dir)
{
    int tf = constrain((cfg.chartTf < 1 ? 1 : cfg.chartTf) + dir, 1, 3);
    cfg.chartTf = tf;
    prefs.putInt("chartTf", cfg.chartTf);
    net_update_chart_change(0);
    refresh_settings();
}

// ---------------------------------------------------------------- page overview

static lv_color_t tile_color(int page)
{
    if (ui_page_usable(page)) return COL_BLUE;          // shown in the rotation
    if (cfg.pg[page]) return COL_ORANGE;                // enabled but has nothing to show
    return COL_BTN;                                     // switched off
}

static void refresh_tiles()
{
    for (int i = 0; i < PAGE_COUNT; i++)
        if (valid(w_page_tile[i])) lv_obj_set_style_bg_color(w_page_tile[i], tile_color(i), 0);
}

static void set_page_enabled(int page, bool on)
{
    cfg.pg[page] = on;
    prefs.putBool(PAGE_INFO[page].prefKey, on);
    net_apply_page_flags();
    refresh_tiles();
}

static void on_tile(lv_event_t *e)
{
    int page = (int)(intptr_t)lv_event_get_user_data(e);
    if (!cfg.pg[page]) { set_page_enabled(page, true); return; }       // a grey tile switches the page on
    if (!ui_page_usable(page)) return;
    ui_menu_close();
    ui_goto_page(page);
}

static void on_tile_long(lv_event_t *e)
{
    int page = (int)(intptr_t)lv_event_get_user_data(e);
    set_page_enabled(page, !cfg.pg[page]);
}

static void build_pages_view(lv_obj_t *parent)
{
    w_pages_view = mk_box(parent, SCREEN_W, SCREEN_H - NAV_BAR_H);
    lv_obj_set_pos(w_pages_view, 0, 0);
    lv_obj_t *title = mk_label(w_pages_view, "SEITEN", FONT_S);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_t *hint = mk_label(w_pages_view, "Tippen: oeffnen / einschalten    Halten: ausschalten", FONT_XS, COL_GRAY);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -12, 12);
    for (int i = 0; i < PAGE_COUNT; i++) {
        lv_obj_t *t = lv_btn_create(w_pages_view);
        lv_obj_set_size(t, CELL_W, 62);
        lv_obj_set_pos(t, (i % 3) * (CELL_W + 4) + 4, 40 + (i / 3) * 70);
        lv_obj_set_style_radius(t, 6, 0);
        lv_obj_set_style_bg_color(t, tile_color(i), 0);
        lv_obj_center(mk_label(t, PAGE_INFO[i].name, FONT_S));
        lv_obj_add_event_cb(t, on_tile, LV_EVENT_SHORT_CLICKED, (void *)(intptr_t)i);
        lv_obj_add_event_cb(t, on_tile_long, LV_EVENT_LONG_PRESSED, (void *)(intptr_t)i);
        w_page_tile[i] = t;
    }
}

// ---------------------------------------------------------------- settings view

static void build_settings_view(lv_obj_t *parent)
{
    w_settings_view = mk_box(parent, SCREEN_W, SCREEN_H - NAV_BAR_H);
    lv_obj_set_pos(w_settings_view, 0, 0);

    lv_obj_t *title = mk_label(w_settings_view, "KONFIGURATION", FONT_S);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 105, 5);
    String version = String("v. ") + FIRMWARE_VERSION;
    lv_obj_t *ver = mk_label(w_settings_view, version.c_str(), FONT_XS, COL_GRAY);
    lv_obj_align(ver, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_t *scan = mk_label(w_settings_view, "QR-Code scannen, um zu konfigurieren.", FONT_XS);
    lv_obj_align(scan, LV_ALIGN_TOP_LEFT, 105, 36);
    w_ip_label = mk_label(w_settings_view, "#FFFFFF Oder IP im Browser aufrufen:# #FFA500 --#", FONT_XS);
    lv_label_set_recolor(w_ip_label, true);
    lv_obj_align(w_ip_label, LV_ALIGN_TOP_LEFT, 105, 60);

    w_qr = lv_qrcode_create(w_settings_view, 86, lv_color_black(), lv_color_white());
    lv_obj_align(w_qr, LV_ALIGN_TOP_LEFT, 6, 6);
    lv_obj_set_style_border_color(w_qr, lv_color_white(), 0);
    lv_obj_set_style_border_width(w_qr, 3, 0);
    s_shown_ip = "";

    // row 1: currency, price alert, page rotation
    lv_obj_t *cell = mk_cell(w_settings_view, 0, ROW1_Y, "WAEHRUNG");
    lv_obj_t *row = mk_row(cell, CELL_W - 8, 34);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -6);
    lv_obj_set_style_pad_column(row, 4, 0);
    static const char *CODES[3] = { "EUR", "USD", "CHF" };
    for (int i = 0; i < 3; i++) {
        lv_obj_t *b = lv_btn_create(row);
        lv_obj_set_size(b, 44, 28);
        lv_obj_set_style_radius(b, 4, 0);
        lv_obj_center(mk_label(b, CODES[i], FONT_XS));
        lv_obj_add_event_cb(b, on_currency, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        w_currency_btn[i] = b;
    }
    cell = mk_cell(w_settings_view, 1, ROW1_Y, "PREISALARM");
    w_alert_sw = mk_switch_row(cell, cfg.alertEnabled, on_alert);
    cell = mk_cell(w_settings_view, 2, ROW1_Y, "SEITENWECHSEL");
    mk_stepper(cell, &w_auto_value,
               [](lv_event_t *) { step_auto(-1); }, [](lv_event_t *) { step_auto(+1); });

    // row 2: night mode, livestream, chart timeframe
    cell = mk_cell(w_settings_view, 0, ROW2_Y, "NIGHTMODE");
    w_night_sw = mk_switch_row(cell, cfg.nightActive, on_night);
    cell = mk_cell(w_settings_view, 1, ROW2_Y, "LIVESTREAM");
    w_stream_sw = mk_switch_row(cell, cfg.showYoutubeBar, on_stream);
    cell = mk_cell(w_settings_view, 2, ROW2_Y, "CHART");
    mk_stepper(cell, &w_chart_value,
               [](lv_event_t *) { step_chart(-1); }, [](lv_event_t *) { step_chart(+1); });
    refresh_settings();
}

// ---------------------------------------------------------------- bottom bar and overlay

static void show_view(bool pages)
{
    s_pages_mode = pages;
    if (pages) { lv_obj_add_flag(w_settings_view, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(w_pages_view, LV_OBJ_FLAG_HIDDEN); }
    else       { lv_obj_add_flag(w_pages_view, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(w_settings_view, LV_OBJ_FLAG_HIDDEN); }
    lv_label_set_text(w_mode_label, pages ? LV_SYMBOL_SETTINGS : LV_SYMBOL_LIST);
    refresh_tiles();
}

static void build_bottom_bar(lv_obj_t *parent)
{
    lv_obj_t *bar = mk_box(parent, SCREEN_W, NAV_BAR_H);
    lv_obj_set_pos(bar, 0, SCREEN_H - NAV_BAR_H);
    lv_obj_set_style_bg_color(bar, COL_PANEL, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

    lv_obj_t *close = lv_btn_create(bar);
    lv_obj_set_size(close, 90, NAV_BAR_H - 6);
    lv_obj_align(close, LV_ALIGN_LEFT_MID, 3, 0);
    lv_obj_set_style_bg_color(close, COL_BTN, 0);
    lv_obj_set_style_radius(close, 6, 0);
    lv_obj_center(mk_label(close, LV_SYMBOL_CLOSE, FONT_M));
    lv_obj_add_event_cb(close, [](lv_event_t *) { ui_menu_close(); }, LV_EVENT_CLICKED, nullptr);

    w_brightness = lv_slider_create(bar);
    lv_obj_set_size(w_brightness, 220, 6);
    lv_obj_align(w_brightness, LV_ALIGN_CENTER, 0, 0);
    lv_slider_set_range(w_brightness, 3, 100);
    lv_slider_set_value(w_brightness, constrain(cfg.brightness, 3, 100), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(w_brightness, COL_ORANGE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(w_brightness, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_bg_color(w_brightness, COL_WHITE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(w_brightness, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(w_brightness, [](lv_event_t *e) {
        cfg.brightness = lv_slider_get_value(lv_event_get_target(e));
        ui_set_brightness(cfg.brightness);
    }, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(w_brightness, [](lv_event_t *) { prefs.putInt("brightness", cfg.brightness); },
                        LV_EVENT_RELEASED, nullptr);

    lv_obj_t *mode = lv_btn_create(bar);
    lv_obj_set_size(mode, 90, NAV_BAR_H - 6);
    lv_obj_align(mode, LV_ALIGN_RIGHT_MID, -3, 0);
    lv_obj_set_style_bg_color(mode, COL_BTN, 0);
    lv_obj_set_style_radius(mode, 6, 0);
    w_mode_label = mk_label(mode, LV_SYMBOL_LIST, FONT_M);
    lv_obj_center(w_mode_label);
    lv_obj_add_event_cb(mode, [](lv_event_t *) { show_view(!s_pages_mode); }, LV_EVENT_CLICKED, nullptr);
}

void ui_menu_open()
{
    if (valid(w_menu)) return;
    w_menu = mk_box(lv_layer_top(), SCREEN_W, SCREEN_H);
    lv_obj_set_pos(w_menu, 0, 0);
    lv_obj_set_style_bg_color(w_menu, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(w_menu, LV_OPA_COVER, 0);
    lv_obj_add_flag(w_menu, LV_OBJ_FLAG_CLICKABLE);
    for (int i = 0; i < PAGE_COUNT; i++) w_page_tile[i] = nullptr;
    build_settings_view(w_menu);
    build_pages_view(w_menu);
    build_bottom_bar(w_menu);
    show_view(false);
    ui_menu_tick();
}

static void menu_close_now()
{
    if (!valid(w_menu)) return;
    lv_obj_del(w_menu);
    w_menu = nullptr;
    // the children are gone: forget the pointers so a recycled address cannot pass valid()
    w_settings_view = w_pages_view = w_qr = w_ip_label = w_brightness = w_mode_label = nullptr;
    w_alert_sw = w_night_sw = w_stream_sw = w_auto_value = w_chart_value = nullptr;
    for (int i = 0; i < 3; i++) w_currency_btn[i] = nullptr;
    for (int i = 0; i < PAGE_COUNT; i++) w_page_tile[i] = nullptr;
}

// Called from button handlers inside the menu: delete once the event has been handled.
void ui_menu_close() { ui_post(menu_close_now); }

// Keeps the address, the QR code and the switches current while the menu is open.
void ui_menu_tick()
{
    if (!valid(w_menu)) return;
    String ip = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("--");
    if (ip != s_shown_ip) {
        s_shown_ip = ip;
        set_text(w_ip_label, "#FFFFFF Oder IP im Browser aufrufen:# #FFA500 " + ip + "#");
        String url = "http://" + ip;
        if (valid(w_qr)) lv_qrcode_update(w_qr, url.c_str(), url.length());
    }
    refresh_settings();
    if (s_pages_mode) refresh_tiles();
}
