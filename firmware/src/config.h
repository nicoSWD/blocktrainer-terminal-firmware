// Persistent settings (NVS namespace "BTCLOCK").
//
// Key names and value types are shared with the stock firmware, so a device keeps its
// Wi-Fi credentials and settings when it is switched between the two images. Do not rename
// a key or change its type: Preferences silently returns the default when the stored type
// differs.

#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include "diag.h"

// Override per build with  PLATFORMIO_BUILD_FLAGS='-DFIRMWARE_VERSION=\"0.1.1\"' pio run -e ...
// (the release workflow does this from the tag). Compared as %d.%d.%d against the latest GitHub release.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "0.1.0"
#endif
#define NVS_NAMESPACE    "BTCLOCK"

#define URL_PRICE    "https://api.blocktrainer.de/price.json"
#define URL_CHART    "https://api.blocktrainer.de/chart.json"
#define URL_MEMPOOL  "https://api.blocktrainer.de/mempool.json"
#define URL_YOUTUBE  "https://api.blocktrainer.de/youtube.json"
#define URL_NEWS     "https://api.blocktrainer.de/news.json"
// Update channel: the latest (non-draft, non-prerelease) release of this project on GitHub.
#ifndef URL_UPDATE      // the host OTA test points this at a local server
#define URL_UPDATE   "https://api.github.com/repos/nicoSWD/blocktrainer-terminal-firmware/releases/latest"
#endif
// The release asset holding the normal build is named OTA_ASSET_PREFIX + version + OTA_ASSET_SUFFIX
// (see "Stage release assets" in .github/workflows/firmware-release.yml).
#define OTA_ASSET_PREFIX "bt-terminal-firmware-v"
#define OTA_ASSET_SUFFIX ".bin"

// Page identifiers, in the order of the page table in ui.cpp.
enum PageId {
    PAGE_PRICE = 0, PAGE_CHART, PAGE_BLOCK, PAGE_CLOCK, PAGE_BITAXE,
    PAGE_FEES, PAGE_CONV, PAGE_NEWS, PAGE_DIFF,
    PAGE_COUNT
};

struct Settings {
    String currency       = "USD";
    String timezoneLabel  = "";
    String timezoneDef    = "CET-1CEST,M3.5.0/2,M10.5.0/3";
    String nightStart     = "23:00";
    String nightStop      = "07:00";
    String splitSecCur    = "";
    String pageOrder      = "";            // comma separated page names, empty = default order
    String alertCurrency  = "EUR";
    String stackUnit      = "BTC";         // stored for compatibility, no page uses it
    String bitaxeIPs      = "";            // comma separated

    int  priceInterval = 10000;            // ms between price fetches
    int  brightness    = 60;               // percent
    int  rotation      = 1;                // LovyanGFX rotation, 1 = landscape
    int  nightDelay    = 30;               // seconds
    int  autoPage      = 0;                // seconds between automatic page changes, 0 = off
    int  chartTf       = 0;                // chart timeframe index
    int  alertAbove    = 0;
    int  alertBelow    = 0;
    int64_t btcHoldingSats = -1;           // stored for compatibility, no page uses it
    double  convBtc    = 0;

    uint8_t ledBrightness = 50;            // percent
    uint8_t ledStripType  = 1;             // 0 = WS2812 (GRB), otherwise WS2811 (RGB)
    uint8_t buzzerVolume  = 50;            // percent

    bool showYoutubeBar  = true;
    bool showEvents      = true;
    bool checkForUpdates = true;
    bool nightActive     = false;
    bool debugEnabled    = false;
    bool priceSeparator  = false;
    bool priceSplit      = false;
    bool convSaved       = false;
    bool ledEnabled      = true;
    bool alertEnabled    = false;
    bool pg[PAGE_COUNT]  = {true, true, true, true, true, true, true, true, true};
    bool pgStack         = false;          // stored for compatibility, no page uses it
};

extern Settings cfg;
extern Preferences prefs;

void config_defaults();     // resets cfg to compile-time defaults
void config_load();         // creates missing keys, then reads every key into cfg
void config_save_all();     // writes cfg back (used by the web form)
void config_reset();        // erases the whole namespace; the caller restarts the device

// Logging: always into the RTC ring buffer (served at /diag), to Serial only when debug is on.
#define DBG(...) diag_printf(__VA_ARGS__)
