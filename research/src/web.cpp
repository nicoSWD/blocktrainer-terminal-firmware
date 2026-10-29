// web.cpp: embedded configuration server (Arduino WebServer on port 80).
//
// Routes registered by FUN_42039ee8 (verified):
//   GET  /            FUN_42035b84  settings page, %PLACEHOLDER% substitution
//   GET  /status.json FUN_42035400  live values
//   POST /save        FUN_420373f8  form handler, writes NVS
//   GET  /reset       FUN_4203733c  clears the whole NVS namespace, replies {"ok":true}
//   any  /restart     FUN_420372f8  sets the restart flag (0x3fc9b850), replies {"ok":true}
//
// The HTML itself (index_html.h) is a rebuilt equivalent of the embedded page, not
// a byte copy: see extracted/web/ for the original fragments.

#include <WebServer.h>
#include <WiFi.h>
#include <time.h>
#include "web.h"
#include "config.h"
#include "data.h"
#include "index_html.h"
#include "timezones.h"
#include "display.h"

static WebServer server(80);                   // object at 0x3fc9bb5c
volatile bool g_restart_requested = false;     // DAT_3fc9b850

static String chk(bool b) { return b ? " checked" : ""; }
static String sel(bool b) { return b ? " selected" : ""; }

// "1", "on" and "true" count as true; anything else (or a missing checkbox) is false.
static bool arg_bool(const char *name)
{
    if (!server.hasArg(name)) return false;
    String v = server.arg(name);
    v.toLowerCase();
    return v == "1" || v == "on" || v == "true";
}

static const char *WEEKDAYS[7] = { "SONNTAG", "MONTAG", "DIENSTAG", "MITTWOCH", "DONNERSTAG", "FREITAG", "SAMSTAG" };

// ---------------------------------------------------------------- GET /
static String render_index()
{
    String h = FPSTR(INDEX_HTML);

    const char *cur[3] = { "USD", "EUR", "CHF" };
    for (const char *c : cur) {
        String k = String(c);
        h.replace("%" + k + "_SELECTED%", chk(cfg.currency == k));      // radio buttons: "checked"
        h.replace("%ALERT_" + k + "_SEL%", sel(cfg.alertCurrency == k));
        h.replace("%SPLITSEC_" + k + "_SEL%", sel(cfg.splitSecCur == k));
    }
    h.replace("%SPLITSEC_AUTO_SEL%", sel(cfg.splitSecCur.isEmpty()));

    h.replace("%YOUTUBE_ON%", chk(cfg.showYoutubeBar));
    h.replace("%EVENTS_ON%", chk(cfg.showEvents));
    h.replace("%UPDATES_ON%", chk(cfg.checkForUpdates));
    h.replace("%DEBUG_ON%", chk(cfg.debugEnabled));
    h.replace("%PRICESEPARATOR_ON%", chk(cfg.priceSeparator));
    h.replace("%PRICESPLITSCREEN_ON%", chk(cfg.priceSplit));
    h.replace("%LED_ENABLED%", chk(cfg.ledEnabled));
    h.replace("%ROTATION_CHECKED%", chk(cfg.rotation == 3));         // "Rotation 180" ticked = rotation 3 (FUN_4203587c)
    h.replace("%ALERT_ENABLED%", chk(cfg.alertEnabled));
    h.replace("%NIGHTMODE_ACTIVE_ON%", chk(cfg.nightActive));
    h.replace("%LED_STRIP_0%", sel(cfg.ledStripType == 0));
    h.replace("%LED_STRIP_1%", sel(cfg.ledStripType == 1));

    h.replace("%BRIGHTNESS%", String(cfg.brightness));
    h.replace("%LED_BRIGHTNESS%", String(cfg.ledBrightness));
    h.replace("%BUZZER_VOLUME%", String(cfg.buzzerVolume));
    h.replace("%ALERT_ABOVE%", String(cfg.alertAbove));
    h.replace("%ALERT_BELOW%", String(cfg.alertBelow));
    h.replace("%NIGHTMODE_START%", cfg.nightStart);
    h.replace("%NIGHTMODE_STOP%", cfg.nightStop);
    h.replace("%NIGHTMODE_DELAY%", String(cfg.nightDelay));
    h.replace("%PRICE_INTERVAL%", String(cfg.priceInterval));
    // the page script does: const BITAXE_IPS=[%BITAXE_IPS%];  -> "1.2.3.4","5.6.7.8"
    String ips;
    for (size_t i = 0; i < g_bitaxe.size(); i++) {
        if (i) ips += ",";
        ips += "\"" + g_bitaxe[i].ip + "\"";
    }
    h.replace("%BITAXE_IPS%", ips);

    // drag-sortable page list: <div class='sort-item' draggable='true' data-key='K'><span class='sort-pos'>N</span>LABEL</div>
    // UNVERIFIED: data-key value and the pageOrder serialisation the script posts back.
    String sort;
    for (int i = 0; i < PAGE_COUNT; i++) {
        if (PAGES[i].menu_only) continue;
        sort += String("<div class='sort-item' draggable='true' data-key='") + PAGES[i].pref_key +
                "'><span class='sort-pos'>" + String(i + 1) + "</span>" + PAGES[i].name + "</div>";
    }
    h.replace("%PAGE_SORT_ITEMS%", sort);

    // <select name="timezone"> options: value = TZ definition, text = label (table at 0x3c2292ac)
    String tz;
    for (int i = 0; i < TIMEZONE_COUNT; i++) {
        tz += String("<option value='") + TIMEZONES[i].def + "'" +
              (cfg.timezoneDef == TIMEZONES[i].def ? " selected" : "") + ">" + TIMEZONES[i].label + "</option>";
    }
    h.replace("<select name=\"timezone\">", "<select name=\"timezone\">" + tz);
    h.replace("%FIRMWARE_VERSION%", FIRMWARE_VERSION);
    h.replace("%MAC%", WiFi.macAddress());
    h.replace("%SECONDARY_CURRENCY_DISPLAY%", cfg.priceSplit ? "" : "display:none");
    return h;
}

// ---------------------------------------------------------------- GET /status.json
// Format string pieces are in rodata at 0x3c2290xx.
static void handle_status()
{
    time_t now = time(nullptr);
    struct tm tmv;
    localtime_r(&now, &tmv);
    char tbuf[8], dbuf[12];
    strftime(tbuf, sizeof(tbuf), "%H:%M", &tmv);
    strftime(dbuf, sizeof(dbuf), "%d.%m.%Y", &tmv);

    String out;
    out.reserve(1024);
    char b[256];
    xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    int moscowEur = live.price[1] > 0 ? (int)(1e8f / live.price[1]) : 0;   // UNVERIFIED: second "moscow" value
    snprintf(b, sizeof(b),
        "{\"price\":{\"usd\":%.0f,\"eur\":%.0f,\"chf\":%.0f,\"chg_usd\":%.2f,\"chg_eur\":%.2f,\"chg_chf\":%.2f,\"moscow\":%d,\"moscow_eur\":%d}",
        live.price[0], live.price[1], live.price[2],
        live.priceExtra[0], live.priceExtra[1], live.priceExtra[2], live.satsPerUnit, moscowEur);
    out += b;
    snprintf(b, sizeof(b), ",\"block\":{\"height\":%d}", live.blockHeight);
    out += b;
    snprintf(b, sizeof(b), ",\"fees\":{\"low\":%.1f,\"mid\":%.1f,\"high\":%.1f}", live.feeLow, live.feeMid, live.feeHigh);
    out += b;
    snprintf(b, sizeof(b), ",\"difficulty\":{\"progress\":%.1f,\"change\":%.2f,\"blocks\":%d}",
             live.diffProgress, live.diffChange, live.diffBlocks);
    out += b;
    out += ",\"bitaxe\":[";
    for (size_t i = 0; i < g_bitaxe.size(); i++) {
        if (i) out += ",";
        out += "{\"name\":\"" + g_bitaxe[i].hostname + "\",\"hash\":\"" + g_bitaxe[i].hashrate +
               "\",\"temp\":\"" + g_bitaxe[i].temp + " C\",\"fan\":\"" + g_bitaxe[i].fan + " %\"}";
    }
    out += "],\"news\":[";
    for (size_t i = 0; i < g_news.size(); i++) {
        if (i) out += ",";
        out += "{\"ts\":" + String(g_news[i].ts) + ",\"headline\":\"" + g_news[i].headline +
               "\",\"author\":\"" + g_news[i].author + "\"}";   // NOTE: no JSON escaping in the original either (UNVERIFIED)
    }
    xSemaphoreGive(g_data_mutex);
    out += "],\"clock\":{\"time\":\"" + String(tbuf) + "\",\"date\":\"" + String(dbuf) +
           "\",\"weekday\":\"" + String(WEEKDAYS[tmv.tm_wday]) + "\"}}";
    server.send(200, "application/json", out);
}

// ---------------------------------------------------------------- POST /save
// Field names verified from the rodata strings referenced by FUN_420373f8.
static void handle_save()
{
    if (server.hasArg("currency"))      cfg.currency = server.arg("currency");
    if (server.hasArg("timezone")) {
        // the posted value is the TZ definition; the label is looked up in the table
        String def = server.arg("timezone");
        String label = def;
        for (int i = 0; i < TIMEZONE_COUNT; i++)
            if (def == TIMEZONES[i].def) { label = TIMEZONES[i].label; break; }
        cfg.timezoneDef = def;
        cfg.timezoneLabel = label;
        setenv("TZ", def.c_str(), 1);
        tzset();
        DBG("🕒 Zeitzone angewendet: %s -> %s\n", label.c_str(), def.c_str());
    }
    if (server.hasArg("priceInterval")) cfg.priceInterval = server.arg("priceInterval").toInt();
    cfg.rotation = arg_bool("rotation") ? 3 : 1;                    // FUN_420373f8: ticked = 3, else 1
    cfg.ledEnabled = arg_bool("ledEnabled");
    if (server.hasArg("ledStripType"))  cfg.ledStripType = server.arg("ledStripType").toInt();
    if (server.hasArg("brightness"))    cfg.brightness = server.arg("brightness").toInt();
    if (server.hasArg("ledBrightness")) cfg.ledBrightness = server.arg("ledBrightness").toInt();
    if (server.hasArg("buzzerVolume"))  cfg.buzzerVolume = server.arg("buzzerVolume").toInt();
    cfg.showYoutubeBar  = arg_bool("showYoutubeBar");
    cfg.showEvents      = arg_bool("showEvents");
    cfg.checkForUpdates = arg_bool("checkForUpdates");
    cfg.debugEnabled    = arg_bool("debug");
    cfg.priceSeparator  = arg_bool("priceSeparator");
    cfg.priceSplit      = arg_bool("priceSplitScreen");
    if (server.hasArg("splitSecondaryCurrency")) {
        String v = server.arg("splitSecondaryCurrency");
        cfg.splitSecCur = (v == "EUR" || v == "USD" || v == "CHF") ? v : "";
    }
    if (server.hasArg("pageOrder"))     cfg.pageOrder = server.arg("pageOrder");
    cfg.nightActive = arg_bool("nightmodeActive");
    if (server.hasArg("nightmodeStart")) cfg.nightStart = server.arg("nightmodeStart");
    if (server.hasArg("nightmodeStop"))  cfg.nightStop = server.arg("nightmodeStop");
    if (server.hasArg("nightmodeDelay")) cfg.nightDelay = server.arg("nightmodeDelay").toInt();
    cfg.alertEnabled = arg_bool("alertEnabled");
    if (server.hasArg("alertCurrency"))  cfg.alertCurrency = server.arg("alertCurrency");
    if (server.hasArg("alertAbove"))     cfg.alertAbove = server.arg("alertAbove").toInt();
    if (server.hasArg("alertBelow"))     cfg.alertBelow = server.arg("alertBelow").toInt();
    // The page posts one "bitaxeIP" input per device; the handler walks all args, strips a
    // leading http:// or https:// and joins them with ',' into the "bitaxeIPs" key.
    {
        String joined;
        bool any = false;
        for (int i = 0; i < server.args(); i++) {
            if (server.argName(i) != "bitaxeIP") continue;
            any = true;
            String ip = server.arg(i);
            ip.trim();
            if (ip.startsWith("http://")) ip = ip.substring(7);
            else if (ip.startsWith("https://")) ip = ip.substring(8);
            if (ip.isEmpty()) continue;
            if (joined.length()) joined += ",";
            joined += ip;
        }
        if (any) {
            cfg.bitaxeIPs = joined;
            DBG("Bitaxe IPs: %s\n", cfg.bitaxeIPs.c_str());
            bitaxe_parse_ips();
        }
    }

    config_save_all();
    tasks_apply_page_flags();     // FUN_4200c798 re-evaluates which fetch tasks should run
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
    ESP.restart();            // FUN_42076d98(&ESP)
}

void web_begin()
{
    server.on("/", HTTP_GET, [] { server.send(200, "text/html", render_index()); });
    server.on("/status.json", HTTP_GET, handle_status);
    server.on("/save", HTTP_POST, handle_save);
    server.on("/reset", HTTP_GET, handle_reset);
    server.on("/restart", HTTP_ANY, handle_restart);
    server.begin();
    if (cfg.debugEnabled) {
        Serial.print("Webserver läuft unter der IP-Adresse: ");
        Serial.println(WiFi.localIP());
    }
}

void web_loop()
{
    server.handleClient();   // FUN_420743c0, timed: warns when it takes longer than 130 ms
}
