// Host test driver for the LVGL UI of the firmware.
//
//   host_test render [outdir]   renders every screen to PNG (via PPM + sips) and runs layout checks
//   host_test stress [seed]     ASAN/UBSan stress run, heap accounting through the LVGL heap hooks
//   host_test all               both (default)
//
// ui_core.cpp is included into this translation unit so the driver can use its statics
// (show_page, drain_queue, ota_ui_tick, wizard_*, ...) and run the very same ui_init() and the
// same flush callback as the device. ui_pages.cpp, ui_menu.cpp and pages.cpp are linked normally.

#include "../src/ui_core.cpp"

#include <ArduinoJson.h>
#include <math.h>
#include <limits.h>
#include <set>
#include "host_api.h"

// ---------------------------------------------------------------- loop (mirrors ui_task)

static uint32_t h_last_second = 0, h_last_auto = 0;
static unsigned g_page_builds = 0, g_menu_opens = 0;
static unsigned g_builds_of[PAGE_COUNT];

// Every page build of the driver goes through here so the stress run can show how often each page was built.
static void host_show_page(int page)
{
    if (page >= 0 && page < PAGE_COUNT) g_builds_of[page]++;
    show_page(page);
}

// One iteration of ui_task() without vTaskDelay. KEEP IN SYNC with ui_task in ui_core.cpp.
static void host_step(uint32_t dt)
{
    static int last_page = -2;
    static bool last_menu = false;
    host_clock_advance(dt);
    uint32_t now = millis();
    drain_queue();

    if (s_pending_page >= 0 && !s_switching) {
        int page = s_pending_page;
        s_pending_page = -1;
        s_switching = true;
        if (s_first_page_ms == 0) s_first_page_ms = now;
        host_show_page(page);
        s_switch_ms = millis();
    }
    if (s_switching && now - s_switch_ms > SWITCH_COOLDOWN_MS) s_switching = false;

    if (now - h_last_second >= 1000) {
        h_last_second = now;
        night_mode_tick();
        update_stream_bar();
        if (s_current_page >= 0 && PAGE_OPS[s_current_page].update) PAGE_OPS[s_current_page].update();
        price_alert_check();
        ui_menu_tick();
    }

    if (cfg.autoPage > 0 && !s_switching && !valid(s_sys) && !ui_menu_is_open() && s_current_page >= 0 &&
        !s_sleeping) {
        if (h_last_auto < s_last_switch_ms) h_last_auto = s_last_switch_ms;
        if (now - h_last_auto >= (uint32_t)cfg.autoPage * 1000UL) {
            s_pending_page = ui_next_page(s_current_page, +1);
            h_last_auto = now;
        }
    } else {
        h_last_auto = now;
    }

    ota_ui_tick();
    led_tick();
    lv_timer_handler();
    if (s_current_page != last_page) { g_page_builds++; last_page = s_current_page; }
    if (ui_menu_is_open() && !last_menu) g_menu_opens++;
    last_menu = ui_menu_is_open();
}

static void run_ms(uint32_t ms, uint32_t dt = 10)
{
    for (uint32_t t = 0; t < ms; t += dt) host_step(dt);
}

static void settle() { run_ms(1300); }

// ---------------------------------------------------------------- touch injection

static void tap(int x, int y, uint32_t hold = 70)
{
    g_host_touch_x = x; g_host_touch_y = y;
    g_host_touch_down = true;
    run_ms(hold);
    g_host_touch_down = false;
    run_ms(70);
}

static void tap_obj(lv_obj_t *o)
{
    lv_area_t a;
    lv_obj_get_coords(o, &a);
    tap((a.x1 + a.x2) / 2, (a.y1 + a.y2) / 2);
}

// ---------------------------------------------------------------- sample data

static uint32_t g_rng = 12345;
static uint32_t rnd() { g_rng = g_rng * 1664525u + 1013904223u; return g_rng >> 8; }
static uint32_t rnd(uint32_t n) { return rnd() % n; }

static String make_chart_json(int n24, int n5d, float eur, float usd, float chf)
{
    time_t now = host_time(nullptr);
    auto series = [&](int n, int step, String &out) {
        out += "{\"step\":" + String(step) + ",\"p\":[";
        for (int i = 0; i < n; i++) {
            double x = (double)i / (n > 1 ? n - 1 : 1);
            double wave = 1.0 + 0.035 * sin(x * 9.0) + 0.02 * sin(x * 31.0) - 0.03 * (1.0 - x);
            if (i) out += ",";
            out += "[" + String((long)(now - (long)(n - 1 - i) * step)) + "," + String(eur * wave, 2) + "," +
                   String(usd * wave, 2) + "," + String(chf * wave, 2) + "]";
        }
        out += "]}";
    };
    String json = "{\"tf\":{\"24h\":";
    series(n24, 1800, json);
    json += ",\"5d\":";
    series(n5d, 3600, json);
    json += "}}";
    return json;
}

static void load_sample_data()
{
    DataLock lock;
    live = LiveData();
    live.price[CUR_EUR] = 97123.4f; live.price[CUR_USD] = 105432.1f; live.price[CUR_CHF] = 91234.7f;
    live.priceChange[CUR_EUR] = 1.23f; live.priceChange[CUR_USD] = -0.87f; live.priceChange[CUR_CHF] = 0.42f;
    live.chartChange[CUR_EUR] = 1.23f; live.chartChange[CUR_USD] = -0.87f; live.chartChange[CUR_CHF] = 0.42f;
    live.satsPerUnit = (int)(1e8f / live.price[CUR_USD]);
    live.blockHeight = 912345;
    live.feeLow = 3.1f; live.feeMid = 4.2f; live.feeHigh = 9.8f;
    for (int c = 0; c < 3; c++) {
        float fee[3] = { live.feeLow, live.feeMid, live.feeHigh };
        for (int i = 0; i < 3; i++) live.feeCost[c][i] = fee[i] * 140.0f / 1e8f * live.price[c];
    }
    live.diffProgress = 63.4f; live.diffChange = -2.31f; live.diffBlocks = 738;
    live.chartJson = make_chart_json(48, 120, live.price[CUR_EUR], live.price[CUR_USD], live.price[CUR_CHF]);
    live.chartVersion = 1;
    live.dataVersion = 1;
    live.streamKnown = false;              // stream_bar(true) switches the bar on
    live.streamStatus = "live";
    live.streamTitle = "Bitcoin Kurs Analyse live: Warum der Preis heute steigt und was Anleger jetzt wissen muessen";
    live.streamScheduled = false;

    time_t now = host_time(nullptr);
    g_news.clear();
    const char *heads[6] = {
        "Bitcoin-ETFs verzeichnen Rekordzuflüsse: Anleger setzen auf den nächsten Schub nach dem Halving",
        "Bundesbank-Chef warnt vor Überhitzung am Kryptomarkt, Regulierer prüfen neue Auflagen für Börsen",
        "Mining-Difficulty sinkt erstmals seit drei Monaten",
        "Änderung der Steuerregeln: Haltefrist für Bitcoin bleibt bei einem Jahr – Experten sind uneins",
        "Lightning Network wächst weiter",
        "Strategy kauft erneut Bitcoin im Wert von 1,2 Milliarden € und hält nun über 600.000 BTC in der Bilanz"
    };
    const char *authors[6] = { "Max Müller", "Lena Schäfer", "Redaktion", "Jürgen Großmann-Kleinschmidt", "", "Blocktrainer Redaktionsteam" };
    for (int i = 0; i < 6; i++) g_news.push_back({ (long)(now - 3600L * (i + 1)), String(heads[i]), String(authors[i]) });

    g_bitaxe.clear();
    BitaxeDevice a;
    a.ip = "192.168.178.50"; a.hostname = "bitaxe-gamma-wohnzimmer"; a.hashrate = "1214.6"; a.hashGh = 1214.6f;
    a.temp = "61.5"; a.vrTemp = "58.0"; a.fan = "62"; a.bestDiff = "1.23 G"; a.sessionDiff = "345 M"; a.networkDiff = "126.41 T";
    g_bitaxe.push_back(a);
    BitaxeDevice b = a;
    b.ip = "192.168.178.51"; b.hostname = "bitaxe-supra"; b.hashrate = "504.2"; b.hashGh = 504.2f;
    b.temp = "64.0"; b.vrTemp = "60.5"; b.fan = "100";
    g_bitaxe.push_back(b);
}

static void debug_chart_json()
{
    DynamicJsonDocument doc(24576);
    DeserializationError e = deserializeJson(doc, live.chartJson);
    printf("  chart json: %u bytes, parse: %s, doc usage %u/%u overflowed=%d\n", live.chartJson.length(), e.c_str(),
           (unsigned)doc.memoryUsage(), (unsigned)doc.capacity(), (int)doc.overflowed());
}

static void default_config()
{
    config_defaults();
    cfg.currency = "EUR";
    cfg.priceSeparator = true;
    cfg.chartTf = 1;
    cfg.showYoutubeBar = true;
}

// ---------------------------------------------------------------- checks on the widget tree

static int g_warnings = 0;
static int g_shots = 0;

static bool path_hidden(lv_obj_t *o)
{
    for (; o; o = lv_obj_get_parent(o))
        if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return true;
    return false;
}

// True when some ancestor can really scroll (a long list): content outside it is not a layout problem.
static bool in_scrolled_container(lv_obj_t *o)
{
    for (lv_obj_t *p = lv_obj_get_parent(o); p; p = lv_obj_get_parent(p))
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_SCROLLABLE) &&
            (lv_obj_get_scroll_bottom(p) > 0 || lv_obj_get_scroll_top(p) > 0 || lv_obj_get_scroll_left(p) > 0 ||
             lv_obj_get_scroll_right(p) > 0))
            return true;
    return false;
}

// Label rectangle against the visible area of its ancestors (everything clips to its parent).
static bool clipped_by_ancestor(lv_obj_t *label, lv_area_t *clip)
{
    lv_area_t a;
    lv_obj_get_coords(label, &a);
    lv_area_t vis = { 0, 0, SCREEN_W - 1, SCREEN_H - 1 };
    for (lv_obj_t *p = lv_obj_get_parent(label); p; p = lv_obj_get_parent(p)) {
        if (lv_obj_has_flag(p, LV_OBJ_FLAG_OVERFLOW_VISIBLE)) continue;
        lv_area_t pa;
        lv_obj_get_coords(p, &pa);
        if (pa.x2 < pa.x1 || pa.y2 < pa.y1) continue;
        lv_area_t out;
        if (!_lv_area_intersect(&out, &vis, &pa)) return true;
        vis = out;
    }
    *clip = vis;
    return a.x1 < vis.x1 || a.x2 > vis.x2 || a.y1 < vis.y1 || a.y2 > vis.y2;
}

static void check_tree(lv_obj_t *o, const char *name, int top_limit)
{
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return;
    if (lv_obj_check_type(o, &lv_label_class)) {
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        const char *text = lv_label_get_text(o);
        const lv_font_t *font = lv_obj_get_style_text_font(o, 0);
        bool shown = a.x2 >= a.x1 && a.y2 >= a.y1 && text && *text;
        lv_area_t clip;
        if (shown && !in_scrolled_container(o) && clipped_by_ancestor(o, &clip)) {
            bool extreme = !strncmp(name, "huge_", 5);        // deliberately absurd values: report, do not count
            printf("  %s [%s] label clipped or off-screen: x=%d..%d y=%d..%d, visible x=%d..%d y=%d..%d \"%.50s\"\n",
                   extreme ? "info" : "WARN", name, a.x1, a.x2, a.y1, a.y2, clip.x1, clip.x2, clip.y1, clip.y2, text);
            if (!extreme) g_warnings++;
        }
        // glyphs the font lacks are drawn as boxes
        if (shown) {
            uint32_t i = 0;
            bool recolor = lv_label_get_recolor(o);
            while (text[i]) {
                uint32_t next = 0;
                uint32_t c = _lv_txt_encoded_next(text, &i);
                (void)next;
                if (c == '\n' || c == '\r') continue;
                lv_font_glyph_dsc_t g;
                if (!lv_font_get_glyph_dsc(font, &g, c, 0)) {
                    printf("  WARN [%s] missing glyph U+%04X in \"%.40s\"\n", name, (unsigned)c, text);
                    g_warnings++;
                    break;
                }
            }
            (void)recolor;
        }
    }
    if (lv_obj_check_type(o, &lv_chart_class)) {
        uint16_t cnt = lv_chart_get_point_count(o);
        lv_chart_series_t *ser = lv_chart_get_series_next(o, nullptr);
        lv_coord_t *y = ser ? lv_chart_get_y_array(o, ser) : nullptr;
        lv_coord_t lo = 32767, hi = -32768;
        for (uint16_t i = 0; y && i < cnt; i++) { if (y[i] == LV_CHART_POINT_NONE) continue; if (y[i] < lo) lo = y[i]; if (y[i] > hi) hi = y[i]; }
        printf("  chart [%s]: %u points, values %d..%d\n", name, (unsigned)cnt, (int)lo, (int)hi);
    }
    uint32_t n = lv_obj_get_child_cnt(o);
    for (uint32_t i = 0; i < n; i++) check_tree(lv_obj_get_child(o, i), name, top_limit);
}

static void check_layout(const char *name)
{
    lv_obj_update_layout(lv_scr_act());
    lv_obj_update_layout(lv_layer_top());
    check_tree(lv_scr_act(), name, 0);
    check_tree(lv_layer_top(), name, 0);
}

// ---------------------------------------------------------------- PNG output

static std::string g_outdir = "out";

static void shot(const char *name)
{
    lv_obj_update_layout(lv_scr_act());
    lv_obj_invalidate(lv_scr_act());
    lv_obj_invalidate(lv_layer_top());
    lv_refr_now(nullptr);
    check_layout(name);

    std::string ppm = g_outdir + "/" + name + ".ppm", png = g_outdir + "/" + name + ".png";
    FILE *f = fopen(ppm.c_str(), "wb");
    if (!f) { fprintf(stderr, "cannot write %s\n", ppm.c_str()); exit(2); }
    fprintf(f, "P6\n%d %d\n255\n", HOST_W, HOST_H);
    for (int i = 0; i < HOST_W * HOST_H; i++) {
        uint16_t v = g_host_fb[i];
        uint8_t px[3] = { (uint8_t)(((v >> 11) & 31) * 255 / 31), (uint8_t)(((v >> 5) & 63) * 255 / 63), (uint8_t)((v & 31) * 255 / 31) };
        fwrite(px, 1, 3, f);
    }
    fclose(f);
    std::string cmd = "sips -s format png '" + ppm + "' --out '" + png + "' >/dev/null 2>&1 && rm -f '" + ppm + "'";
    if (system(cmd.c_str()) != 0) fprintf(stderr, "sips failed for %s\n", ppm.c_str());
    g_shots++;
    printf("  shot %s.png\n", name);
}

// ---------------------------------------------------------------- scenario helpers

static void goto_page(int page)
{
    s_pending_page = page;
    settle();
    if (ui_current_page() != page) { printf("  FAIL could not switch to page %d (is %d)\n", page, ui_current_page()); g_warnings++; }
}

static void stream_bar(bool on)
{
    live.streamKnown = on;
    cfg.showYoutubeBar = true;
    settle();
    if (ui_stream_bar_visible() != on) { printf("  FAIL stream bar state\n"); g_warnings++; }
    settle();
}

static const char *PAGE_FILE[PAGE_COUNT] = { "price", "chart", "block", "clock", "bitaxe", "fees", "conv", "news", "diff" };

static void close_everything()
{
    ui_setup_done();
    ui_menu_close();
    settle();
}

// ---------------------------------------------------------------- render mode

static void check_transliteration()
{
    struct { const char *in, *want; } t[] = {
        { "Café Crème Élan", "Cafe Creme Elan" },
        { "Änderung öl Über Straße", "Aenderung oel Ueber Strasse" },
        { "Señor Niño Øre", "Senor Nino Ore" },
        { "€ 5 – „x“", "EUR 5 - \"x\"" },
        { "\xC3", "?" },
    };
    for (auto &c : t) {
        String got = ui_tr(c.in);
        if (got != c.want) { printf("  FAIL ui_tr(\"%s\") = \"%s\", wanted \"%s\"\n", c.in, got.c_str(), c.want); g_warnings++; }
    }
}

static void render_all()
{
    printf("== render ==\n");
    check_transliteration();
    default_config();
    load_sample_data();
    live.streamKnown = false;
    debug_chart_json();
    ui_show_first_page();
    settle();

    // every page, full height and with the stream bar
    for (int bar = 0; bar < 2; bar++) {
        stream_bar(bar == 1);
        for (int p = 0; p < PAGE_COUNT; p++) {
            goto_page(p);
            char n[64];
            snprintf(n, sizeof(n), "page_%s_%s", PAGE_FILE[p], bar ? "bar" : "full");
            shot(n);
        }
    }
    stream_bar(false);

    // price page: currencies, split screen, thousands separator off
    const char *curs[3] = { "USD", "EUR", "CHF" };
    for (int i = 0; i < 3; i++) {
        cfg.currency = curs[i];
        goto_page(PAGE_PRICE);
        ui_settings_changed();
        settle();
        char n[64];
        snprintf(n, sizeof(n), "price_%s", curs[i]);
        shot(n);
    }
    cfg.currency = "EUR";
    cfg.priceSplit = true;
    ui_settings_changed();
    settle();
    shot("price_split_full");
    stream_bar(true);
    shot("price_split_bar");
    cfg.splitSecCur = "CHF";
    ui_settings_changed();
    settle();
    shot("price_split_bar_eur_chf");
    cfg.splitSecCur = "";
    stream_bar(false);
    cfg.priceSplit = false;
    cfg.priceSeparator = false;
    ui_settings_changed();
    settle();
    shot("price_no_separator");
    cfg.priceSeparator = true;
    ui_settings_changed();
    settle();

    // every news item (long headlines), full height and with the bar
    for (int bar = 0; bar < 2; bar++) {
        stream_bar(bar == 1);
        goto_page(PAGE_NEWS);
        for (int k = 1; k < 6; k++) {
            run_ms(10100);
            char n[64];
            snprintf(n, sizeof(n), "news_item%d_%s", k + 1, bar ? "bar" : "full");
            shot(n);
        }
    }
    stream_bar(false);

    // chart timeframes
    for (int tf = 1; tf <= 3; tf++) {
        cfg.chartTf = tf;
        goto_page(PAGE_CHART);
        ui_settings_changed();
        settle();
        char n[64];
        snprintf(n, sizeof(n), "chart_tf%d", tf);
        shot(n);
    }
    cfg.chartTf = 1;

    // stream bar variants on the price page
    goto_page(PAGE_PRICE);
    stream_bar(true);
    live.streamStatus = "scheduled";
    live.streamStart = (int64_t)host_time(nullptr) + 3725;
    run_ms(1200);
    shot("stream_scheduled");
    live.streamStart = (int64_t)host_time(nullptr) - 5;
    run_ms(1200);
    shot("stream_starting");
    live.streamStatus = "live";
    live.streamTitle = "Kurz";
    run_ms(1200);
    shot("stream_live_short");
    live.streamTitle = "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW";
    run_ms(1200);
    shot("stream_live_wide");
    live.streamTitle = "Bitcoin Kurs Analyse live: Warum der Preis heute steigt und was Anleger jetzt wissen muessen";
    stream_bar(false);

    // menu: settings and pages view, plain touches like a user
    printf("-- menu\n");
    goto_page(PAGE_PRICE);
    tap(SCREEN_W / 2, SCREEN_H / 2);               // centre zone opens the menu
    settle();
    if (!ui_menu_is_open()) { printf("  FAIL centre tap did not open the menu\n"); g_warnings++; }
    shot("menu_settings");
    cfg.alertEnabled = true; cfg.nightActive = true; cfg.autoPage = 15; cfg.chartTf = 3; cfg.currency = "CHF";
    run_ms(1200);                                  // ui_menu_tick syncs the switches
    shot("menu_settings_on");
    tap(SCREEN_W - 48, SCREEN_H - NAV_BAR_H / 2);  // mode button: pages view
    settle();
    cfg.pg[PAGE_NEWS] = false;
    g_bitaxe.clear();
    run_ms(1200);
    shot("menu_pages");
    g_bitaxe.clear();
    load_sample_data();
    cfg.alertEnabled = false; cfg.nightActive = false; cfg.autoPage = 0; cfg.chartTf = 1; cfg.currency = "EUR";
    cfg.pg[PAGE_NEWS] = true;
    tap(48, SCREEN_H - NAV_BAR_H / 2);             // close button
    settle();
    if (ui_menu_is_open()) { printf("  FAIL menu did not close\n"); g_warnings++; }
    g_host_wifi_connected = false;
    ui_menu_open();
    settle();
    shot("menu_settings_offline");
    ui_menu_close();
    g_host_wifi_connected = true;
    settle();

    // navigation by touch: right quarter = next page
    int before = ui_current_page();
    tap(SCREEN_W - 40, 160);
    settle();
    printf("  nav: right tap moved page %d -> %d\n", before, ui_current_page());
    goto_page(PAGE_PRICE);

    // Wi-Fi status screens
    printf("-- wifi\n");
    g_ssid = "FRITZ!Box 7590 JK";
    ui_show(UI_WELCOME); settle(); shot("wifi_welcome");
    ui_show(UI_WIFI_CONNECTING); settle(); shot("wifi_connecting");
    ui_show(UI_WIFI_CONNECTED); settle(); shot("wifi_connected");
    ui_show(UI_WIFI_FAILED); settle(); shot("wifi_failed");
    g_ssid = "Familie-Mueller-Schmidt-Gaeste-5";
    ui_show(UI_WIFI_CONNECTING); settle(); shot("wifi_connecting_long_ssid");
    g_ssid = "Café Müller Straße #1";
    ui_show(UI_WIFI_FAILED); settle(); shot("wifi_failed_umlaut_ssid");
    close_everything();

    // wizard: scan list, password
    ui_show(UI_WIFI_WIZARD); settle(); shot("wizard_searching");
    wizard_scan_task(nullptr);                     // what the scan task does on the device
    settle();
    shot("wizard_list");
    lv_obj_t *list = lv_obj_get_child(s_sys, 1), *target = nullptr;
    for (uint32_t i = 0; list && i < lv_obj_get_child_cnt(list); i++) {       // find the 32 wide characters entry
        lv_obj_t *btn = lv_obj_get_child(list, i);
        for (uint32_t k = 0; k < lv_obj_get_child_cnt(btn); k++) {
            lv_obj_t *c = lv_obj_get_child(btn, k);
            if (lv_obj_check_type(c, &lv_label_class) && !strncmp(lv_label_get_text(c), "WWWW", 4)) target = btn;
        }
    }
    if (target) tap_obj(target);                   // the list is gone afterwards: do not touch it again
    else { printf("  FAIL wizard entry not found\n"); g_warnings++; }
    settle();
    if (valid(w_wizard_pass)) lv_textarea_set_text(w_wizard_pass, "geheimes-passwort");
    run_ms(100);
    shot("wizard_password");
    close_everything();

    // OTA screens
    printf("-- ota\n");
    ota.remoteVersion = "1.9.0";
    ota.stage = OTA_AVAILABLE; ota.active = true;
    ui_show(UI_OTA_AVAILABLE); settle(); shot("ota_available");
    ota.remoteVersion = "10.12.134-rc1";
    ui_show(UI_OTA_AVAILABLE); settle(); shot("ota_available_long_version");
    ota.remoteVersion = "1.9.0";
    ota.stage = OTA_DOWNLOADING; ota.total = 1700000; ota.downloaded = 800000;
    ui_show(UI_OTA_PROGRESS); settle(); shot("ota_progress");
    ota.stage = OTA_INSTALLING; run_ms(300); shot("ota_installing");
    ota.stage = OTA_DONE; run_ms(300); shot("ota_done");
    ota.stage = OTA_ERROR; ota.active = true;
    ota.error = "Download fehlgeschlagen: HTTP 404";
    ui_show(UI_OTA_PROGRESS); run_ms(100);
    ota.stage = OTA_ERROR; run_ms(300); shot("ota_error");
    ota.error = "Download fehlgeschlagen: HTTP 404 (https://btclock.blocktrainer.de/v2/firmware/firmware.bin), Flash-Schreibfehler an Offset 0x1A0000";
    ota.stage = OTA_DOWNLOADING; ui_show(UI_OTA_PROGRESS); run_ms(100);
    ota.stage = OTA_ERROR; run_ms(300); shot("ota_error_long");
    ota.stage = OTA_IDLE; ota.active = false;
    close_everything();

    // price alert overlay
    printf("-- alert\n");
    cfg.alertEnabled = true; cfg.alertCurrency = "EUR"; cfg.alertAbove = 97000;
    goto_page(PAGE_PRICE);
    run_ms(12000);
    shot("alert_above");
    tap(SCREEN_W / 2, SCREEN_H / 2);
    run_ms(500);
    live.price[CUR_EUR] = 80000;                   // leaves the zone, re-arms
    cfg.alertAbove = 0; cfg.alertBelow = 85000;
    run_ms(3000);
    live.price[CUR_EUR] = 97123.4f;
    run_ms(65000);
    live.price[CUR_EUR] = 84321.0f;
    run_ms(3000);
    shot("alert_below");
    tap(SCREEN_W / 2, SCREEN_H / 2);
    run_ms(500);
    live.price[CUR_EUR] = 97123.4f;
    cfg.alertEnabled = false;
    run_ms(2000);

    // Wi-Fi lost banner over pages with bottom content
    printf("-- banner\n");
    const int banner_pages[] = { PAGE_NEWS, PAGE_FEES, PAGE_CONV, PAGE_BITAXE, PAGE_CLOCK, PAGE_CHART };
    for (int bar = 0; bar < 2; bar++) {
        stream_bar(bar == 1);
        for (int p : banner_pages) {
            goto_page(p);
            ui_notify_wifi_lost();
            run_ms(300);
            char n[64];
            snprintf(n, sizeof(n), "banner_%s_%s", PAGE_FILE[p], bar ? "bar" : "full");
            shot(n);
            ui_notify_wifi_restored();
            run_ms(300);
        }
    }
    stream_bar(false);

    // empty and extreme data
    printf("-- edge data\n");
    {
        DataLock lock;
        live.price[0] = live.price[1] = live.price[2] = 0;
        live.blockHeight = 0;
        live.chartJson = "";
        live.chartVersion++;
        g_news.clear();
        g_bitaxe.clear();
    }
    for (int p : { (int)PAGE_PRICE, (int)PAGE_CHART, (int)PAGE_BLOCK, (int)PAGE_FEES, (int)PAGE_NEWS, (int)PAGE_DIFF, (int)PAGE_BITAXE }) {
        goto_page(p);
        char n[64];
        snprintf(n, sizeof(n), "empty_%s", PAGE_FILE[p]);
        shot(n);
    }
    load_sample_data();
    live.price[CUR_EUR] = 1234567890.0f;
    live.blockHeight = 2147483647;
    live.feeLow = 12345.6f; live.feeMid = 99999.9f; live.feeHigh = 123456.7f;
    live.feeCost[CUR_EUR][0] = 1234567.89f; live.feeCost[CUR_EUR][1] = 22.0f; live.feeCost[CUR_EUR][2] = 0.0f;
    live.diffChange = -100.0f; live.diffProgress = 100.0f; live.diffBlocks = 2016;
    live.chartChange[CUR_EUR] = -88.88f;
    live.streamKnown = false;
    for (int p : { (int)PAGE_PRICE, (int)PAGE_CHART, (int)PAGE_BLOCK, (int)PAGE_FEES, (int)PAGE_DIFF }) {
        goto_page(p);
        char n[64];
        snprintf(n, sizeof(n), "huge_%s", PAGE_FILE[p]);
        shot(n);
    }
    load_sample_data();
    live.streamKnown = false;
    goto_page(PAGE_PRICE);
    printf("render done: %d images, %d warnings\n", g_shots, g_warnings);
}

// ---------------------------------------------------------------- stress mode

struct HeapMark { size_t blocks, bytes; };
static HeapMark heap_mark() { return { host_heap_live_blocks(), host_heap_live_bytes() }; }

static const float FLOATS[] = { 0.0f, -1.0f, 1e-9f, 0.4f, 0.5f, 1.0f, 97123.4f, 105432.1f, 1e7f, 3.4e38f, -3.4e38f,
                                NAN, INFINITY, -INFINITY, 2147483648.0f, -2147483904.0f, 1e12f };
static const int INTS[] = { 0, -1, 1, 912345, INT_MAX, INT_MIN, 2016, 999999999, -999999999 };

static String long_text(size_t n, const char *unit)
{
    String s;
    while (s.length() < n) s += unit;
    return s;
}

static String random_string()
{
    switch (rnd(9)) {
        case 0: return String();
        case 1: return String("x");
        case 2: return long_text(3000, "Lange Überschrift mit Umlauten äöüß und #FF0000 Farbcode# ");
        case 3: return String("#FFA500 recolor %s %d %n # ## \n\n\t");
        case 4: return long_text(200, "€„ ");
        case 5: return String("😀 emoji \xC3 broken utf8 \xE2\x82");
        case 6: return String("normal headline");
        case 7: return long_text(70, "W");
        default: return String("Familie-Müller-Schmidt-Gaeste-5");
    }
}

static float random_float() { return FLOATS[rnd(sizeof(FLOATS) / sizeof(FLOATS[0]))]; }

static String random_chart_json()
{
    switch (rnd(14)) {
        case 0: return String();
        case 1: return String("{}");
        case 2: return String("not json at all {{{");
        case 3: return String("{\"tf\":{}}");
        case 4: return String("{\"tf\":{\"24h\":{\"p\":[]},\"5d\":{\"p\":[]}}}");
        case 5: return String("{\"tf\":{\"24h\":{\"p\":[[1,2,3,4]]},\"5d\":{\"p\":[[1,2,3,4]]}}}");
        case 6: return String("{\"tf\":{\"24h\":{\"p\":[[1,5,5,5],[2,5,5,5]]},\"5d\":{\"p\":[[1,5,5,5],[2,5,5,5]]}}}");
        case 7: return String("{\"tf\":{\"24h\":{\"p\":[[1],[2],[3]]},\"5d\":{\"p\":[null,\"x\",[1,\"a\",{},[]]]}}}");
        case 8: return String("{\"tf\":{\"24h\":{\"p\":[[1,1e39,-1e39,3e38],[2,-1e39,1e39,0],[3,0,0,0]]},\"5d\":{\"p\":[[1,1e39,-1e39,3e38],[2,-1e39,1e39,0]]}}}");
        case 9: return make_chart_json(48, 120, 97123.4f, 105432.1f, 91234.7f);
        case 10: return make_chart_json(300, 400, 97123.4f, 105432.1f, 91234.7f);
        case 11: return make_chart_json(2, 2, 1.0f, 1.0f, 1.0f);
        case 12: return make_chart_json(48, 120, 0.0f, 0.0f, 0.0f);
        default: return make_chart_json(1 + rnd(150), 1 + rnd(150), 97123.4f * (1 + rnd(1000)), 105432.1f, 91234.7f / (1 + rnd(1000)));
    }
}

static void fuzz_data()
{
    DataLock lock;
    for (int i = 0; i < 3; i++) {
        live.price[i] = rnd(3) ? random_float() : (rnd(2) ? 97123.4f : 0.0f);
        live.chartChange[i] = random_float(); live.priceChange[i] = random_float();
    }
    live.blockHeight = INTS[rnd(sizeof(INTS) / sizeof(INTS[0]))];
    live.feeLow = random_float(); live.feeMid = random_float(); live.feeHigh = random_float();
    for (int c = 0; c < 3; c++) for (int i = 0; i < 3; i++) live.feeCost[c][i] = random_float();
    live.diffProgress = random_float(); live.diffChange = random_float();
    live.diffBlocks = INTS[rnd(sizeof(INTS) / sizeof(INTS[0]))];
    if (rnd(2)) { live.chartJson = random_chart_json(); live.chartVersion++; }
    live.streamTitle = random_string();
    static const char *ST[] = { "live", "scheduled", "none", "" };
    live.streamStatus = ST[rnd(4)];
    live.streamKnown = rnd(3) != 0;
    live.streamStart = rnd(3) == 0 ? 0 : (int64_t)host_time(nullptr) + (int64_t)rnd(100000) - 50000;

    g_news.clear();
    int nn = rnd(4) == 0 ? 0 : rnd(8);
    for (int i = 0; i < nn; i++) g_news.push_back({ rnd(3) ? (long)host_time(nullptr) - (long)rnd(100000) : (long)rnd(100), random_string(), random_string() });

    g_bitaxe.clear();
    int nb = rnd(3) == 0 ? 0 : 1 + rnd(3);
    for (int i = 0; i < nb; i++) {
        BitaxeDevice d;
        d.ip = random_string(); d.hostname = rnd(2) ? random_string() : String();
        d.hashrate = rnd(4) ? String("1") : String(); d.hashGh = random_float();
        d.temp = random_string(); d.vrTemp = random_string(); d.fan = random_string();
        d.bestDiff = random_string(); d.sessionDiff = random_string(); d.networkDiff = random_string();
        g_bitaxe.push_back(d);
    }
}

static void update_all_pages()
{
    for (int p = 0; p < PAGE_COUNT; p++) if (PAGE_OPS[p].update) PAGE_OPS[p].update();
}

static void collect(lv_obj_t *o, std::vector<lv_obj_t *> &v)
{
    v.push_back(o);
    uint32_t n = lv_obj_get_child_cnt(o);
    for (uint32_t i = 0; i < n; i++) collect(lv_obj_get_child(o, i), v);
}

// Sends the events a user could cause to every widget below `root`.
static void poke_widgets(lv_obj_t *root, int long_press_pct)
{
    if (!valid(root)) return;
    std::vector<lv_obj_t *> v;
    collect(root, v);
    for (lv_obj_t *o : v) {
        if (!lv_obj_is_valid(o)) continue;
        if (lv_obj_check_type(o, &lv_switch_class)) {
            if (lv_obj_has_state(o, LV_STATE_CHECKED)) lv_obj_clear_state(o, LV_STATE_CHECKED);
            else lv_obj_add_state(o, LV_STATE_CHECKED);
            lv_event_send(o, LV_EVENT_VALUE_CHANGED, nullptr);
        } else if (lv_obj_check_type(o, &lv_slider_class)) {
            lv_slider_set_value(o, 3 + rnd(98), LV_ANIM_OFF);
            lv_event_send(o, LV_EVENT_VALUE_CHANGED, nullptr);
            lv_event_send(o, LV_EVENT_RELEASED, nullptr);
        } else if (lv_obj_check_type(o, &lv_btnmatrix_class) || lv_obj_check_type(o, &lv_keyboard_class)) {
            for (int k = 0; k < 6; k++) {
                if (!lv_obj_is_valid(o)) break;
                lv_btnmatrix_set_selected_btn(o, rnd(16));
                lv_event_send(o, LV_EVENT_VALUE_CHANGED, nullptr);
            }
        } else if (lv_obj_has_flag(o, LV_OBJ_FLAG_CLICKABLE)) {
            lv_event_send(o, LV_EVENT_PRESSED, nullptr);
            lv_event_send(o, LV_EVENT_SHORT_CLICKED, nullptr);
            lv_event_send(o, LV_EVENT_CLICKED, nullptr);
            lv_event_send(o, LV_EVENT_RELEASED, nullptr);
            if ((int)rnd(100) < long_press_pct) lv_event_send(o, LV_EVENT_LONG_PRESSED, nullptr);
        }
    }
}

static void random_taps(int n)
{
    for (int i = 0; i < n; i++) {
        int x = rnd(HOST_W), y = rnd(HOST_H);
        if (rnd(4) == 0) {                          // a drag
            g_host_touch_x = x; g_host_touch_y = y; g_host_touch_down = true;
            run_ms(40);
            for (int k = 0; k < 4; k++) { g_host_touch_x = (int)rnd(HOST_W); g_host_touch_y = (int)rnd(HOST_H); run_ms(30); }
            g_host_touch_down = false;
            run_ms(60);
        } else {
            tap(x, y, rnd(3) == 0 ? 700 : 70);      // some are long presses
        }
    }
}

static void canonical_state()
{
    ui_setup_done();
    ui_menu_close();
    ota.stage = OTA_IDLE; ota.active = false;
    {
        DataLock lock;
        live.streamKnown = false;
    }
    default_config();
    cfg.pg[PAGE_NEWS] = true;
    load_sample_data();
    live.streamKnown = false;
    s_pending_page = PAGE_CLOCK;
    run_ms(1500);
    s_pending_page = PAGE_CLOCK;
    run_ms(1500);
    ui_setup_done();
    run_ms(500);
}

static void stress_cycle(int scale)
{
    // 1. pages: real switching with the stream bar toggling underneath
    for (int i = 0; i < 200 / scale; i++) {
        if (i % 5 == 0) {
            live.streamKnown = !live.streamKnown;
            live.streamStatus = rnd(2) ? "live" : "scheduled";
            live.streamStart = (int64_t)host_time(nullptr) + 600;
        }
        s_pending_page = rnd(PAGE_COUNT);
        run_ms(rnd(3) == 0 ? 20 : 700, 20);
        if (i % 3 == 0) update_all_pages();
    }
    // direct build / destroy through show_page (no cooldown), including the same page twice in a row
    for (int i = 0; i < 200 / scale; i++) {
        int p = rnd(PAGE_COUNT);
        host_show_page(p);
        if (rnd(4) == 0) host_show_page(p);
        update_all_pages();
        if (rnd(5) == 0) PAGE_OPS[p].destroy();     // destroyed twice later by show_page
        update_all_pages();
        lv_timer_handler();
    }
    // 2. menu open / close
    for (int i = 0; i < 100 / scale; i++) {
        s_pending_page = rnd(PAGE_COUNT);
        run_ms(40, 20);
        ui_menu_open();
        if (rnd(4) == 0) ui_menu_open();           // already open
        run_ms(40, 20);
        if (rnd(2)) poke_widgets(lv_layer_top(), 20);
        run_ms(40, 20);
        ui_menu_close();
        if (rnd(3) == 0) ui_menu_close();
        run_ms(rnd(2) ? 20 : 80, 20);
        if (rnd(10) == 0) { cfg.pg[rnd(PAGE_COUNT)] = true; }
    }
    // 3. every setting that rebuilds pages
    for (int i = 0; i < 60 / scale; i++) {
        static const char *CUR[3] = { "USD", "EUR", "CHF" };
        cfg.currency = CUR[rnd(3)];
        cfg.priceSplit = rnd(2);
        cfg.priceSeparator = rnd(2);
        cfg.splitSecCur = rnd(2) ? String() : String(CUR[rnd(3)]);
        cfg.chartTf = rnd(4);
        cfg.rotation = rnd(2) ? 1 : 3;
        cfg.brightness = rnd(101);
        cfg.autoPage = rnd(3) ? 0 : 5;
        cfg.pageOrder = rnd(2) ? String() : String("pg_news, pg_clock,bogus,pg_price,,pg_news");
        s_pending_page = rnd(PAGE_COUNT);
        run_ms(100, 20);
        ui_settings_changed();
        run_ms(rnd(2) ? 100 : 700, 20);
    }
    cfg.autoPage = 0;
    // 4. status screens, replaced and cleared in random order
    for (int i = 0; i < 150 / scale; i++) {
        int s = rnd(10);
        g_ssid = random_string();
        if (s == 9) { ui_setup_done(); }
        else if (s == 5) {                          // wizard flow
            ui_show(UI_WIFI_WIZARD);
            run_ms(40, 20);
            g_host_scan_count = rnd(19);
            wizard_scan_task(nullptr);
            run_ms(40, 20);
            if (rnd(2)) poke_widgets(s_sys, 0);
        } else if (s == 6) {
            ota.remoteVersion = random_string();
            ui_show(UI_OTA_AVAILABLE);
            run_ms(40, 20);
            if (rnd(2)) poke_widgets(s_sys, 0);
        } else if (s == 7) {
            ota.stage = OTA_DOWNLOADING; ota.total = rnd(3) ? 1000000 : 0; ota.downloaded = (int)rnd(1200000);
            ui_show(UI_OTA_PROGRESS);
            run_ms(60, 20);
            ota.stage = (OtaStage)rnd(7);
            ota.active = rnd(4) != 0;
            ota.error = random_string();
            run_ms(60, 20);
        } else {
            ui_show((UiScreen)rnd(9));
            if (rnd(3) == 0) ui_notify_wifi_restored();
        }
        run_ms(40, 20);
        if (rnd(8) == 0) poke_widgets(lv_layer_top(), 10);
    }
    ota.stage = OTA_IDLE; ota.active = false;
    ui_setup_done();
    run_ms(100, 20);
    // 5. alert overlay
    cfg.alertEnabled = true; cfg.alertCurrency = "EUR"; cfg.alertAbove = 50000; cfg.alertBelow = 0;
    for (int i = 0; i < 6 / scale + 1; i++) {
        live.price[CUR_EUR] = 97123.4f;
        run_ms(1100, 100);
        run_ms(61000, 500);
        live.price[CUR_EUR] = 10;
        run_ms(1100, 100);
        tap(240, 160);
    }
    cfg.alertEnabled = false;
    // 6. data fuzz with every page's update(), also on pages that are not shown, plus random touches
    for (int i = 0; i < 300 / scale; i++) {
        fuzz_data();
        if (rnd(3) == 0) { s_pending_page = rnd(PAGE_COUNT); run_ms(700, 20); }
        update_all_pages();
        run_ms(rnd(2) ? 20 : 1100, 20);
        if (rnd(10) == 0) {
            lv_obj_t *c = s_container;
            poke_widgets(c, 0);                    // converter keypad, buttons
        }
    }
    random_taps(150 / scale);
    cfg.pg[PAGE_NEWS] = true;
    for (int i = 0; i < PAGE_COUNT; i++) cfg.pg[i] = true;
}

static int stress(unsigned seed)
{
    printf("== stress (seed %u) ==\n", seed);
    g_rng = seed;
    memset(g_builds_of, 0, sizeof(g_builds_of));
    default_config();
    load_sample_data();
    ui_show_first_page();
    settle();

    // every page built and destroyed 200 times, in a shuffled order, with the stream bar toggling
    for (int round = 0; round < 200; round++) {
        int order[PAGE_COUNT];
        for (int i = 0; i < PAGE_COUNT; i++) order[i] = i;
        for (int i = PAGE_COUNT - 1; i > 0; i--) std::swap(order[i], order[rnd(i + 1)]);
        for (int i = 0; i < PAGE_COUNT; i++) {
            host_show_page(order[i]);
            update_all_pages();
            lv_timer_handler();
        }
        if (round % 10 == 0) { live.streamKnown = !live.streamKnown; run_ms(1100, 100); }
    }

    // warm-up so lazily allocated LVGL state exists before the baseline is taken
    stress_cycle(8);
    canonical_state();
    HeapMark base = heap_mark();
    printf("baseline after warm-up: %zu blocks, %zu bytes (peak so far %zu)\n", base.blocks, base.bytes, host_heap_peak_bytes());

    int rounds = 3;
    int grew = 0;
    for (int r = 0; r < rounds; r++) {
        stress_cycle(1);
        canonical_state();
        HeapMark m = heap_mark();
        printf("after round %d: %zu blocks, %zu bytes (peak %zu), alerts=%d clicks=%d\n", r + 1, m.blocks, m.bytes,
               host_heap_peak_bytes(), g_host_stat.alerts, g_host_stat.clicks);
        if (m.blocks > base.blocks || m.bytes > base.bytes + 256) grew++;
    }
    HeapMark end = heap_mark();
    printf("LVGL heap growth after %d full rounds: %+lld blocks, %+lld bytes\n", rounds,
           (long long)end.blocks - (long long)base.blocks, (long long)end.bytes - (long long)base.bytes);
    unsigned min_builds = ~0u;
    printf("page builds (each one destroys the previous page):");
    for (int p = 0; p < PAGE_COUNT; p++) { printf(" %s=%u", PAGE_FILE[p], g_builds_of[p]); if (g_builds_of[p] < min_builds) min_builds = g_builds_of[p]; }
    printf("\nminimum builds per page: %u%s\n", min_builds, min_builds >= 200 ? "" : "  (BELOW 200)");
    printf("coverage: %u page changes seen by the loop, %u menu opens, %d touch clicks, %d alerts, %d page-flag updates, "
           "%d OTA starts, %d Wi-Fi connects\n", g_page_builds, g_menu_opens, g_host_stat.clicks, g_host_stat.alerts,
           g_host_stat.page_flags_calls, g_host_stat.ota_starts, g_host_stat.reconnects);
    printf("stress finished without a sanitizer abort\n");
    return grew ? 1 : 0;
}

// ---------------------------------------------------------------- init

static lv_disp_t *host_init_ui()
{
    setenv("TZ", "Europe/Berlin", 1);
    tzset();
    default_config();
    if (!ui_init()) { fprintf(stderr, "ui_init failed\n"); exit(2); }
    return lv_disp_get_default();
}

int main(int argc, char **argv)
{
    std::string mode = argc > 1 ? argv[1] : "all";
    unsigned seed = 1;
    if (mode == "render" && argc > 2) g_outdir = argv[2];
    if (mode == "stress" && argc > 2) seed = (unsigned)atoi(argv[2]);
    if (mode == "all" && argc > 2) g_outdir = argv[2];

    if (getenv("HOST_SELFTEST")) {         // proves the sanitizers are armed: this must abort with an ASAN report
        char *p = (char *)ui_alloc(8);
        volatile char c = p[8];
        (void)c;
    }
    lv_disp_t *disp = host_init_ui();
    (void)disp;
    h_last_auto = millis();
    int rc = 0;
    if (mode == "render" || mode == "all") {
        std::string mk = "mkdir -p '" + g_outdir + "'";
        if (system(mk.c_str()) != 0) return 2;
        render_all();
        if (g_warnings) { printf("layout checks: %d warnings\n", g_warnings); }
    }
    if (mode == "stress" || mode == "all") rc = stress(seed);
    return rc;
}
