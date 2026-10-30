#include <WebServer.h>
#include <WiFi.h>
#include <time.h>
#include "web.h"
#include "config.h"
#include "net.h"
#include "pages.h"
#include "hw.h"
#include "ui.h"
#include <Update.h>
#include "bootguard.h"
#include "ota.h"
#include "web_page.h"
#include "timezones.h"

static WebServer server(80);
volatile bool g_restart_requested = false;

static String checked(bool b)
{
    return b ? " checked" : "";
}
static String selected(bool b)
{
    return b ? " selected" : "";
}

// "1", "on" and "true" are true; anything else, or a missing checkbox, is false.
static bool arg_bool(const char *name)
{
    if (!server.hasArg(name)) {
        return false;
    }
    String v = server.arg(name);
    v.toLowerCase();
    return v == "1" || v == "on" || v == "true";
}

static String json_escape(const String &s)
{
    String out;
    out.reserve(s.length() + 8);
    for (size_t i = 0; i < s.length(); i++) {
        char c = s[i];
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c == '\n') {
            out += "\\n";
        } else if (c == '\r') {
            out += "\\r";
        } else if (c == '\t') {
            out += "\\t";
        } else if ((uint8_t)c < 0x20) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return out;
}

// ---------------------------------------------------------------- GET /

static String render_index()
{
    String h = FPSTR(WEB_PAGE_HTML);

    static const char *CURRENCIES[3] = {"USD", "EUR", "CHF"};
    for (const char *c : CURRENCIES) {
        String k = c;
        h.replace("%" + k + "_SELECTED%", checked(cfg.currency == k)); // radio buttons
        h.replace("%ALERT_" + k + "_SEL%", selected(cfg.alertCurrency == k));
        h.replace("%SPLITSEC_" + k + "_SEL%", selected(cfg.splitSecCur == k));
    }
    h.replace("%SPLITSEC_AUTO_SEL%", selected(cfg.splitSecCur.isEmpty()));

    h.replace("%YOUTUBE_ON%", checked(cfg.showYoutubeBar));
    h.replace("%EVENTS_ON%", checked(cfg.showEvents));
    h.replace("%UPDATES_ON%", checked(cfg.checkForUpdates));
    h.replace("%DEBUG_ON%", checked(cfg.debugEnabled));
    h.replace("%PRICESEPARATOR_ON%", checked(cfg.priceSeparator));
    h.replace("%PRICESPLITSCREEN_ON%", checked(cfg.priceSplit));
    h.replace("%SECONDARY_CURRENCY_DISPLAY%", cfg.priceSplit ? "" : "display:none");
    h.replace("%LED_ENABLED%", checked(cfg.ledEnabled));
    h.replace("%ROTATION_CHECKED%", checked(cfg.rotation == 3)); // "Rotation 180": rotation 3 instead of 1
    h.replace("%ALERT_ENABLED%", checked(cfg.alertEnabled));
    h.replace("%NIGHTMODE_ACTIVE_ON%", checked(cfg.nightActive));
    h.replace("%LED_STRIP_0%", selected(cfg.ledStripType == 0));
    h.replace("%LED_STRIP_1%", selected(cfg.ledStripType == 1));

    h.replace("%BRIGHTNESS%", String(cfg.brightness));
    h.replace("%LED_BRIGHTNESS%", String(cfg.ledBrightness));
    h.replace("%BUZZER_VOLUME%", String(cfg.buzzerVolume));
    h.replace("%ALERT_ABOVE%", String(cfg.alertAbove));
    h.replace("%ALERT_BELOW%", String(cfg.alertBelow));
    h.replace("%NIGHTMODE_START%", cfg.nightStart);
    h.replace("%NIGHTMODE_STOP%", cfg.nightStop);
    h.replace("%NIGHTMODE_DELAY%", String(cfg.nightDelay));
    h.replace("%PRICE_INTERVAL%", String(cfg.priceInterval));

    // The page script reads: const BITAXE_IPS=[%BITAXE_IPS%];
    String ips;
    {
        DataLock lock;
        for (size_t i = 0; i < g_bitaxe.size(); i++) {
            if (i) {
                ips += ",";
            }
            ips += "\"" + g_bitaxe[i].ip + "\"";
        }
    }
    h.replace("%BITAXE_IPS%", ips);

    // Drag-sortable page list in the effective order; the script posts the data-keys back as pageOrder.
    String sort;
    int pos = 1;
    for (int page : page_order()) {
        sort += String("<div class='sort-item' draggable='true' data-key='") + PAGE_INFO[page].prefKey +
                "'><span class='sort-pos'>" + String(pos++) + "</span>" + PAGE_INFO[page].name + "</div>";
    }
    h.replace("%PAGE_SORT_ITEMS%", sort);

    String tz;
    for (int i = 0; i < TIMEZONE_COUNT; i++) {
        tz += String("<option value='") + TIMEZONES[i].def + "'" +
              selected(cfg.timezoneDef == TIMEZONES[i].def) + ">" + TIMEZONES[i].label + "</option>";
    }
    h.replace("<select name=\"timezone\">", "<select name=\"timezone\">" + tz);

    h.replace("%FIRMWARE_VERSION%", FIRMWARE_VERSION);
    h.replace("%MAC%", WiFi.macAddress());
    return h;
}

// ---------------------------------------------------------------- GET /status.json

static void handle_status()
{
    static const char *WEEKDAYS[7] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                      "Thursday", "Friday", "Saturday"};
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char time_str[8], date_str[12];
    strftime(time_str, sizeof(time_str), "%H:%M", &tmv);
    strftime(date_str, sizeof(date_str), "%d %b %Y", &tmv);

    String out;
    out.reserve(1024);
    char b[256];
    {
        DataLock lock;
        int sats_per_eur = live.price[CUR_EUR] > 0 ? (int)(1e8f / live.price[CUR_EUR]) : 0;
        snprintf(b, sizeof(b),
                 "{\"price\":{\"usd\":%.0f,\"eur\":%.0f,\"chf\":%.0f,\"chg_usd\":%.2f,\"chg_eur\":%.2f,"
                 "\"chg_chf\":%.2f,\"moscow\":%d,\"moscow_eur\":%d}",
                 live.price[CUR_USD], live.price[CUR_EUR], live.price[CUR_CHF], live.priceChange[CUR_USD],
                 live.priceChange[CUR_EUR], live.priceChange[CUR_CHF], live.satsPerUnit, sats_per_eur);
        out += b;
        snprintf(b, sizeof(b), ",\"block\":{\"height\":%d}", live.blockHeight);
        out += b;
        snprintf(b, sizeof(b), ",\"fees\":{\"low\":%.1f,\"mid\":%.1f,\"high\":%.1f}", live.feeLow,
                 live.feeMid, live.feeHigh);
        out += b;
        snprintf(b, sizeof(b), ",\"difficulty\":{\"progress\":%.1f,\"change\":%.2f,\"blocks\":%d}",
                 live.diffProgress, live.diffChange, live.diffBlocks);
        out += b;
        out += ",\"bitaxe\":[";
        for (size_t i = 0; i < g_bitaxe.size(); i++) {
            if (i) {
                out += ",";
            }
            out += "{\"name\":\"" + json_escape(g_bitaxe[i].hostname) + "\",\"hash\":\"" +
                   json_escape(g_bitaxe[i].hashrate) + "\",\"temp\":\"" + g_bitaxe[i].temp +
                   " C\",\"fan\":\"" + g_bitaxe[i].fan + " %\"}";
        }
        out += "],\"news\":[";
        for (size_t i = 0; i < g_news.size(); i++) {
            if (i) {
                out += ",";
            }
            out += "{\"ts\":" + String(g_news[i].ts) + ",\"headline\":\"" + json_escape(g_news[i].headline) +
                   "\",\"author\":\"" + json_escape(g_news[i].author) + "\"}";
        }
    }
    out += "],\"clock\":{\"time\":\"" + String(time_str) + "\",\"date\":\"" + String(date_str) +
           "\",\"weekday\":\"" + WEEKDAYS[tmv.tm_wday] + "\"}}";
    server.send(200, "application/json", out);
}

// ---------------------------------------------------------------- POST /save

static void handle_save()
{
    if (server.hasArg("currency")) {
        cfg.currency = server.arg("currency");
    }
    if (server.hasArg("timezone")) {
        // The posted value is the TZ definition; the label is looked up in the table.
        String def = server.arg("timezone");
        String label = def;
        for (int i = 0; i < TIMEZONE_COUNT; i++) {
            if (def == TIMEZONES[i].def) {
                label = TIMEZONES[i].label;
                break;
            }
        }
        cfg.timezoneDef = def;
        cfg.timezoneLabel = label;
        setenv("TZ", def.c_str(), 1);
        tzset();
        DBG("web: timezone %s (%s)\n", label.c_str(), def.c_str());
    }
    if (server.hasArg("priceInterval")) {
        cfg.priceInterval = server.arg("priceInterval").toInt();
    }
    cfg.rotation = arg_bool("rotation") ? 3 : 1;
    cfg.ledEnabled = arg_bool("ledEnabled");
    if (server.hasArg("ledStripType")) {
        cfg.ledStripType = server.arg("ledStripType").toInt();
    }
    if (server.hasArg("brightness")) {
        cfg.brightness = server.arg("brightness").toInt();
    }
    if (server.hasArg("ledBrightness")) {
        cfg.ledBrightness = server.arg("ledBrightness").toInt();
    }
    if (server.hasArg("buzzerVolume")) {
        cfg.buzzerVolume = server.arg("buzzerVolume").toInt();
    }
    cfg.showYoutubeBar = arg_bool("showYoutubeBar");
    cfg.showEvents = arg_bool("showEvents");
    cfg.checkForUpdates = arg_bool("checkForUpdates");
    cfg.debugEnabled = arg_bool("debug");
    cfg.priceSeparator = arg_bool("priceSeparator");
    cfg.priceSplit = arg_bool("priceSplitScreen");
    if (server.hasArg("splitSecondaryCurrency")) {
        String v = server.arg("splitSecondaryCurrency");
        cfg.splitSecCur = (v == "EUR" || v == "USD" || v == "CHF") ? v : "";
    }
    if (server.hasArg("pageOrder")) {
        cfg.pageOrder = server.arg("pageOrder");
    }
    cfg.nightActive = arg_bool("nightmodeActive");
    if (server.hasArg("nightmodeStart")) {
        cfg.nightStart = server.arg("nightmodeStart");
    }
    if (server.hasArg("nightmodeStop")) {
        cfg.nightStop = server.arg("nightmodeStop");
    }
    if (server.hasArg("nightmodeDelay")) {
        cfg.nightDelay = server.arg("nightmodeDelay").toInt();
    }
    cfg.alertEnabled = arg_bool("alertEnabled");
    if (server.hasArg("alertCurrency")) {
        cfg.alertCurrency = server.arg("alertCurrency");
    }
    if (server.hasArg("alertAbove")) {
        cfg.alertAbove = server.arg("alertAbove").toInt();
    }
    if (server.hasArg("alertBelow")) {
        cfg.alertBelow = server.arg("alertBelow").toInt();
    }

    // One "bitaxeIP" input per miner: strip a leading http(s)://, join with commas.
    bool any_ip = false;
    String joined;
    for (int i = 0; i < server.args(); i++) {
        if (server.argName(i) != "bitaxeIP") {
            continue;
        }
        any_ip = true;
        String ip = server.arg(i);
        ip.trim();
        if (ip.startsWith("http://")) {
            ip = ip.substring(7);
        } else if (ip.startsWith("https://")) {
            ip = ip.substring(8);
        }
        if (ip.isEmpty()) {
            continue;
        }
        if (joined.length()) {
            joined += ",";
        }
        joined += ip;
    }
    if (any_ip) {
        cfg.bitaxeIPs = joined;
        DBG("web: bitaxe IPs %s\n", joined.c_str());
        DataLock lock;
        net_parse_bitaxe_ips();
    }

    config_save_all();
    net_apply_page_flags();
    ui_settings_changed();
    hw_settings_changed();
    server.send(200, "application/json", "{\"ok\":true}");
}

// ---------------------------------------------------------------- /reset, /restart

static void handle_reset()
{
    config_reset();
    server.send(200, "application/json", "{\"ok\":true}");
}

static void handle_restart()
{
    g_restart_requested = true;
    server.send(200, "application/json", "{\"ok\":true}");
    delay(300);
    ESP.restart();
}

// ---------------------------------------------------------------- /safe, /update

// GET /safe reports the crash-loop state; /safe?exit=1 clears it and restarts normally.
static void handle_safe()
{
    if (server.hasArg("exit")) {
        boot_guard_leave_safe_mode();
        g_restart_requested = true;
        server.send(200, "application/json", "{\"ok\":true}");
        delay(300);
        ESP.restart();
        return;
    }
    server.send(200, "application/json",
                String("{\"safe_mode\":") + (boot_guard_safe_mode() ? "true" : "false") +
                    ",\"crashes\":" + boot_guard_crashes() + ",\"version\":\"" FIRMWARE_VERSION "\"}");
}

// GET /ota checks the update manifest (the same one the boot prompt uses) and reports the result;
// /ota?install=1 installs a newer version if there is one. Works without touching the screen.
static void handle_ota()
{
    if (ota.taskRunning) {
        server.send(200, "application/json", "{\"busy\":true}");
        return;
    }
    ota_check();
    bool available = ota.stage == OTA_AVAILABLE;
    String reply = String("{\"current\":\"") + FIRMWARE_VERSION + "\",\"remote\":\"" + ota.remoteVersion +
                   "\",\"available\":" + (available ? "true" : "false");
    if (available && server.hasArg("install")) {
        ota.stage = OTA_CONFIRMED;
        ota.active = true;
        ui_show(UI_OTA_PROGRESS);
        ota_start_task();
        reply += ",\"installing\":true";
    } else {
        ota.stage = OTA_IDLE; // a plain query leaves no pending prompt behind
        ota.active = false;
    }
    server.send(200, "application/json", reply + "}");
}

// Direct firmware upload: `curl -F firmware=@firmware.bin http://<ip>/update`. Only registered in safe
// mode or when debugging is on, because the page has no authentication.
static void handle_update_page()
{
    server.send(
        200, "text/html",
        "<!DOCTYPE html><html><body style='font-family:sans-serif'><h3>Firmware</h3>"
        "<form method='post' action='/update' enctype='multipart/form-data'>"
        "<input type='file' name='firmware' accept='.bin'><button>Upload</button></form></body></html>");
}

static void handle_update_done()
{
    bool ok = !Update.hasError();
    server.send(200, "text/plain", ok ? "OK, restarting" : String("FAILED: ") + Update.errorString());
    if (ok) {
        g_restart_requested = true;
        delay(500);
        ESP.restart();
    }
}

static void handle_update_upload()
{
    HTTPUpload &up = server.upload();
    boot_guard_feed(); // a long upload is progress, not a hang
    if (up.status == UPLOAD_FILE_START) {
        DBG("web: upload %s\n", up.filename.c_str());
        Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (up.status == UPLOAD_FILE_WRITE) {
        if (Update.write(up.buf, up.currentSize) != up.currentSize) {
            Update.abort();
        }
        vTaskDelay(
            1); // let the idle task run: a fast upload plus the animated UI otherwise trips the task watchdog
    } else if (up.status == UPLOAD_FILE_END) {
        Update.end(true);
    } else if (up.status == UPLOAD_FILE_ABORTED) {
        Update.abort();
    }
}

void web_begin()
{
    server.on("/", HTTP_GET, [] {
        server.send(200, "text/html", render_index());
    });
    server.on("/status.json", HTTP_GET, handle_status);
    server.on("/save", HTTP_POST, handle_save);
    server.on("/reset", HTTP_GET, handle_reset);
    server.on("/restart", HTTP_ANY, handle_restart);
    server.on("/safe", HTTP_GET, handle_safe);
    server.on("/diag", HTTP_GET, [] {
        server.send(200, "text/plain", diag_report());
    });
    server.on("/ota", HTTP_GET, handle_ota);
    if (boot_guard_safe_mode() || cfg.debugEnabled) {
        server.on("/update", HTTP_GET, handle_update_page);
        server.on("/update", HTTP_POST, handle_update_done, handle_update_upload);
    }
    server.begin();
    DBG("web: server on http://%s/\n", WiFi.localIP().toString().c_str());
}

void web_loop()
{
    server.handleClient();
}
