// config.cpp: NVS-backed settings. Order, key names and defaults follow
// FUN_42008a84: first every missing key is created with its default, then all
// keys are read into the globals.

#include "config.h"

Settings cfg;
LiveData live;
Preferences prefs;
SemaphoreHandle_t g_data_mutex;
SemaphoreHandle_t g_http_mutex;
std::vector<NewsItem> g_news;
std::vector<BitaxeDevice> g_bitaxe;

static const char *PG_KEYS[PAGE_COUNT] = {
    "pg_price", "pg_chart", "pg_block", "pg_clock", "pg_bitaxe",
    "pg_fees", "pg_conv", "pg_news", "pg_diff",
};

void config_defaults()
{
    cfg = Settings();
    cfg.chartTf = 1;           // FUN_4200a598 sets 3fc9ba50 = 1 before NVS load overrides it
    cfg.debugEnabled = false;
}

void bitaxe_parse_ips()
{
    g_bitaxe.clear();
    int start = 0;
    String s = cfg.bitaxeIPs;
    while (start >= 0 && start < (int)s.length()) {
        int comma = s.indexOf(',', start);
        String ip = comma < 0 ? s.substring(start) : s.substring(start, comma);
        ip.trim();
        if (ip.length()) {
            BitaxeDevice d;
            d.ip = ip;
            g_bitaxe.push_back(d);
        }
        if (comma < 0) break;
        start = comma + 1;
    }
}

static void ensure_defaults()
{
    if (!prefs.isKey("currency"))        prefs.putString("currency", "USD");
    if (!prefs.isKey("timezoneLabel"))   prefs.putString("timezoneLabel", "");
    if (!prefs.isKey("timezoneDef"))     prefs.putString("timezoneDef", "CET-1CEST,M3.5.0/2,M10.5.0/3");
    if (!prefs.isKey("priceInterval"))   prefs.putInt("priceInterval", 10000);
    if (!prefs.isKey("showYoutubeBar"))  prefs.putBool("showYoutubeBar", true);
    if (!prefs.isKey("showEvents"))      prefs.putBool("showEvents", true);
    if (!prefs.isKey("checkForUpdates")) prefs.putBool("checkForUpdates", true);
    if (!prefs.isKey("brightness"))      prefs.putInt("brightness", 60);
    if (!prefs.isKey("rotation"))        prefs.putInt("rotation", 1);
    if (!prefs.isKey("bitaxeIPs")) {
        // migrate the old single-IP key if present
        String ips = "";
        if (prefs.isKey("bitaxeIP")) ips = prefs.getString("bitaxeIP", "");
        prefs.putString("bitaxeIPs", ips);
    }
    if (!prefs.isKey("nightActive"))     prefs.putBool("nightActive", false);
    if (!prefs.isKey("nightStart"))      prefs.putString("nightStart", "23:00");
    if (!prefs.isKey("nightStop"))       prefs.putString("nightStop", "07:00");
    if (!prefs.isKey("nightDelay"))      prefs.putInt("nightDelay", 30);
    if (!prefs.isKey("stackUnit"))       prefs.putString("stackUnit", "BTC");
    if (!prefs.isKey("btcHoldingSats"))  prefs.putLong64("btcHoldingSats", (uint64_t)-1);
    if (!prefs.isKey("debugEnabled"))    prefs.putBool("debugEnabled", false);
    if (!prefs.isKey("priceSeparator"))  prefs.putBool("priceSeparator", true);
    if (!prefs.isKey("priceSplit"))      prefs.putBool("priceSplit", false);
    for (int i = 0; i < PAGE_COUNT; i++)
        if (!prefs.isKey(PG_KEYS[i]))    prefs.putBool(PG_KEYS[i], true);
    if (!prefs.isKey("conv_saved"))      prefs.putBool("conv_saved", false);
    if (!prefs.isKey("conv_btc"))        prefs.putDouble("conv_btc", 0);
    if (!prefs.isKey("pg_stack"))        prefs.putBool("pg_stack", prefs.getLong64("btcHoldingSats", -1) >= 0);
    if (!prefs.isKey("pageOrder"))       prefs.putString("pageOrder", "");
    if (!prefs.isKey("ledEnabled"))      prefs.putBool("ledEnabled", true);
    if (!prefs.isKey("ledBrightness"))   prefs.putUChar("ledBrightness", 50);
    if (!prefs.isKey("ledStripType"))    prefs.putUChar("ledStripType", 1);
    if (!prefs.isKey("buzzerVolume"))    prefs.putUChar("buzzerVolume", 50);
    if (!prefs.isKey("alertEnabled"))    prefs.putBool("alertEnabled", false);
    if (!prefs.isKey("alertCurrency"))   prefs.putString("alertCurrency", "EUR");
    if (!prefs.isKey("alertAbove"))      prefs.putInt("alertAbove", 0);
    if (!prefs.isKey("alertBelow"))      prefs.putInt("alertBelow", 0);
}

void config_load()
{
    prefs.begin(NVS_NAMESPACE, false);
    ensure_defaults();

    cfg.currency       = prefs.getString("currency", "USD");
    cfg.timezoneLabel  = prefs.getString("timezoneLabel", "");
    cfg.timezoneDef    = prefs.getString("timezoneDef", "CET-1CEST,M3.5.0/2,M10.5.0/3");
    cfg.priceInterval  = prefs.getInt("priceInterval", 10000);
    cfg.showYoutubeBar = prefs.getBool("showYoutubeBar", true);
    cfg.showEvents     = prefs.getBool("showEvents", true);
    cfg.checkForUpdates = prefs.getBool("checkForUpdates", true);
    cfg.brightness     = prefs.getInt("brightness", 60);
    cfg.rotation       = prefs.getInt("rotation", 1);
    cfg.bitaxeIPs      = prefs.getString("bitaxeIPs", "");
    bitaxe_parse_ips();

    cfg.nightActive    = prefs.getBool("nightActive", false);
    cfg.nightStart     = prefs.getString("nightStart", "23:00");
    cfg.nightStop      = prefs.getString("nightStop", "07:00");
    cfg.nightDelay     = prefs.getInt("nightDelay", 30);
    cfg.debugEnabled   = prefs.getBool("debugEnabled", true);
    cfg.priceSeparator = prefs.getBool("priceSeparator", false);
    cfg.priceSplit     = prefs.getBool("priceSplit", false);
    cfg.splitSecCur    = prefs.getString("splitSecCur", "");
    for (int i = 0; i < PAGE_COUNT; i++)
        cfg.pg[i] = prefs.getBool(PG_KEYS[i], true);
    cfg.convSaved      = prefs.getBool("conv_saved", false);
    cfg.convBtc        = prefs.getDouble("conv_btc", 0);
    cfg.autoPage       = prefs.getInt("autoPage", 0);
    cfg.chartTf        = prefs.getInt("chartTf", 0);
    cfg.pageOrder      = prefs.getString("pageOrder", "");
    cfg.ledEnabled     = prefs.getBool("ledEnabled", true);
    cfg.ledBrightness  = prefs.getUChar("ledBrightness", 50);
    cfg.ledStripType   = prefs.getUChar("ledStripType", 1);
    cfg.buzzerVolume   = prefs.getUChar("buzzerVolume", 50);
    cfg.alertEnabled   = prefs.getBool("alertEnabled", false);
    cfg.alertCurrency  = prefs.getString("alertCurrency", "EUR");
    cfg.alertAbove     = prefs.getInt("alertAbove", 0);
    cfg.alertBelow     = prefs.getInt("alertBelow", 0);

    if (cfg.debugEnabled) {
        Serial.println("✅ Einstellungen initialisiert:");
        Serial.printf("- Währung: %s\n", cfg.currency.c_str());
        Serial.printf("- Zeitzone: %s (%s)\n", cfg.timezoneLabel.c_str(), cfg.timezoneDef.c_str());
        Serial.printf("- Preis-Abfrageintervall: %d ms\n", cfg.priceInterval);
        Serial.printf("- YouTube-Bar anzeigen: %s\n", cfg.showYoutubeBar ? "JA" : "NEIN");
        Serial.printf("- Events anzeigen: %s\n", cfg.showEvents ? "JA" : "NEIN");
        Serial.printf("- Update-Prüfung aktiv: %s\n", cfg.checkForUpdates ? "JA" : "NEIN");
        Serial.printf("- Display-Helligkeit: %d %%\n", cfg.brightness);
        Serial.printf("- Display-Rotation: %d\n", cfg.rotation);
        Serial.printf("- Debugging aktiv: %s\n", cfg.debugEnabled ? "JA" : "NEIN");
        Serial.printf("- Tausenderpunkt: %s\n", cfg.priceSeparator ? "JA" : "NEIN");
        Serial.printf("- Nachtmodus Aktiv: %s\n", cfg.nightActive ? "JA" : "NEIN");
        Serial.printf("- Nachtmodus Start: %s\n", cfg.nightStart.c_str());
        Serial.printf("- Nachtmodus Stop : %s\n", cfg.nightStop.c_str());
        Serial.printf("- Nachtmodus Delay: %d s\n", cfg.nightDelay);
        Serial.printf("- pricePageActive: %s\n", cfg.pg[PAGE_PRICE] ? "JA" : "NEIN");
        Serial.printf("- chartPageActive: %s\n", cfg.pg[PAGE_CHART] ? "JA" : "NEIN");
        Serial.printf("- blockPageActive: %s\n", cfg.pg[PAGE_BLOCK] ? "JA" : "NEIN");
        Serial.printf("- clockPageActive: %s\n", cfg.pg[PAGE_CLOCK] ? "JA" : "NEIN");
        Serial.printf("- bitaxePageActive: %s\n", cfg.pg[PAGE_BITAXE] ? "JA" : "NEIN");
        Serial.printf("- feesPageActive: %s\n", cfg.pg[PAGE_FEES] ? "JA" : "NEIN");
    }
}

void config_save_all()
{
    prefs.putString("currency", cfg.currency);
    prefs.putString("timezoneLabel", cfg.timezoneLabel);
    prefs.putString("timezoneDef", cfg.timezoneDef);
    prefs.putInt("priceInterval", cfg.priceInterval);
    prefs.putBool("showYoutubeBar", cfg.showYoutubeBar);
    prefs.putBool("showEvents", cfg.showEvents);
    prefs.putBool("checkForUpdates", cfg.checkForUpdates);
    prefs.putInt("brightness", cfg.brightness);
    prefs.putInt("rotation", cfg.rotation);
    prefs.putString("bitaxeIPs", cfg.bitaxeIPs);
    prefs.putBool("nightActive", cfg.nightActive);
    prefs.putString("nightStart", cfg.nightStart);
    prefs.putString("nightStop", cfg.nightStop);
    prefs.putInt("nightDelay", cfg.nightDelay);
    prefs.putBool("debugEnabled", cfg.debugEnabled);
    prefs.putBool("priceSeparator", cfg.priceSeparator);
    prefs.putBool("priceSplit", cfg.priceSplit);
    prefs.putString("splitSecCur", cfg.splitSecCur);
    prefs.putString("pageOrder", cfg.pageOrder);
    prefs.putBool("ledEnabled", cfg.ledEnabled);
    prefs.putUChar("ledBrightness", cfg.ledBrightness);
    prefs.putUChar("ledStripType", cfg.ledStripType);
    prefs.putUChar("buzzerVolume", cfg.buzzerVolume);
    prefs.putBool("alertEnabled", cfg.alertEnabled);
    prefs.putString("alertCurrency", cfg.alertCurrency);
    prefs.putInt("alertAbove", cfg.alertAbove);
    prefs.putInt("alertBelow", cfg.alertBelow);
    for (int i = 0; i < PAGE_COUNT; i++)
        prefs.putBool(PG_KEYS[i], cfg.pg[i]);
}

void config_reset()
{
    // FUN_4203733c: opens the namespace, clear(), end(). The device is NOT restarted here;
    // the web UI calls /restart separately.
    Preferences p;
    if (!p.begin(NVS_NAMESPACE, false)) {
        DBG("Konnte NVS nicht öffnen.\n");
        return;
    }
    DBG("Lösche den gesamten NVS-Speicher...\n");
    p.clear();
    p.end();
    DBG("NVS-Speicher gelöscht.\n");
}
