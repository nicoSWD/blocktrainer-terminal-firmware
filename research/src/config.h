// config.h: persistent settings (Preferences namespace "BTCLOCK") and the
// runtime globals shared between tasks, pages and web handlers.
//
// Every field notes the firmware global it maps to (DRAM 0x3fc9xxxx) and the
// source of the default. Values come from FUN_4200a598 (compile-time defaults)
// and FUN_42008a84 (NVS init + load) in the decompiled image.

#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

#define FIRMWARE_VERSION "1.8.2"          // DAT_3fc9bac8
#define NVS_NAMESPACE    "BTCLOCK"

// ---- endpoints (defaults set in FUN_4200a598) ----
#define URL_PRICE    "https://api.blocktrainer.de/price.json"    // 3fc9ba10
#define URL_CHART    "https://api.blocktrainer.de/chart.json"    // 3fc9ba24
#define URL_MEMPOOL  "https://api.blocktrainer.de/mempool.json"  // 3fc9bab4
#define URL_YOUTUBE  "https://api.blocktrainer.de/youtube.json"  // 3fc9b97c
#define URL_NEWS     "https://api.blocktrainer.de/news.json"
#define URL_UPDATE   "https://btclock.blocktrainer.de/v2/firmware/firmware.json"  // 3fc9bad8

// ---- pages (index = position in the page table, see display.cpp) ----
enum PageId {
    PAGE_PRICE = 0, PAGE_CHART, PAGE_BLOCK, PAGE_CLOCK, PAGE_BITAXE,
    PAGE_FEES, PAGE_CONV, PAGE_NEWS, PAGE_DIFF,
    PAGE_COUNT
};

struct Settings {
    // strings
    String currency        = "USD";                              // 3fc9b944
    String timezoneLabel   = "";                                 // 3fc9b8d4 (default is DAT_3c121e18)
    String timezoneDef     = "CET-1CEST,M3.5.0/2,M10.5.0/3";     // 3fc9b8e4
    String nightStart      = "23:00";                            // 3fc9ba80
    String nightStop       = "07:00";                            // 3fc9ba90
    String splitSecCur     = "";                                 // 3fc9b958
    String pageOrder       = "";                                 // 3fc9b878
    String alertCurrency   = "EUR";                              // 3fc9b9ec
    String stackUnit       = "BTC";                              // stored, no page uses it in this build
    String bitaxeIPs       = "";                                 // comma separated, parsed into bitaxe devices

    // ints
    int  priceInterval  = 10000;   // ms, 3fc9ba20
    int  brightness     = 60;      // 3fc9baa4
    int  rotation       = 1;       // 3fc9baa8
    int  nightDelay     = 30;      // 3fc9baa0
    int  autoPage       = 0;       // 3fc9b864
    int  chartTf        = 0;       // 3fc9ba50 (0=24h ... see chart page)
    int  alertAbove     = 0;       // 3fc9b9fc
    int  alertBelow     = 0;       // 3fc9ba00
    int64_t btcHoldingSats = -1;   // stored, no page uses it in this build
    double  convBtc     = 0;       // 3fc9b898

    // uint8
    uint8_t ledBrightness = 50;    // 3fc9baae
    uint8_t ledStripType  = 1;     // 3fc9bab0
    uint8_t buzzerVolume  = 50;    // 3fc9bab1

    // bools
    bool showYoutubeBar   = true;  // 3fc9b9bc
    bool showEvents       = true;  // 3fc9b9e8
    bool checkForUpdates  = true;  // 3fc9b8a1
    bool nightActive      = false; // 3fc9ba7c
    bool debugEnabled     = false; // 3fc9b818 (key is created as 0 first, the later read default of 1 never applies)
    bool priceSeparator   = false; // 3fc9b954
    bool priceSplit       = false; // 3fc9b955
    bool convSaved        = false; // 3fc9b8a0
    bool ledEnabled       = true;  // 3fc9baaf
    bool alertEnabled     = false; // 3fc9b9e9
    bool pg[PAGE_COUNT]   = {true, true, true, true, true, true, true, true, true}; // 3fc9b888..b890
    bool pgStack          = false; // stored, no page uses it in this build
};

extern Settings cfg;

// ---- live data, written by the Fetch* tasks under g_data_mutex (3fc9bb50) ----
struct LiveData {
    // FetchPrice
    float price[3]       = {0, 0, 0};   // USD/EUR/CHF: 3fc9b8f4 / b8f8 / b8fc
    float priceExtra[3]  = {0, 0, 0};   // 3fc9b900 / b904 / b908: change % from the API, exposed as chg_* in /status.json
    int   satsPerUnit    = 0;           // 3fc9b910 sats per USD ("moscow" in /status.json); the fees page recomputes it per currency
    int64_t priceStamp   = 0;           // 3fc9b968/b96c, written only when non-zero
    // FetchMempool
    int   blockHeight    = 0;           // 3fc9b90c
    float feeLow = 0, feeMid = 0, feeHigh = 0;   // 3fc9b914 / b918 / b91c
    float feeCost[3][3]  = {};          // 3fc9b920..b940: fee * constant vsize * price per currency
    float diffProgress   = 0;           // 3fc9b970 (percent)
    float diffChange     = 0;           // 3fc9b974 (percent)
    int   diffBlocks     = 0;           // 3fc9b978
    // chart (derived by FUN_42035094 from the stored ChartJson)
    float chartChange[3] = {0, 0, 0};   // 3fc9ba54 / ba58 / ba5c
    String chartJson;                   // 3fc9ba34
    // StreamStatus (YouTube bar)
    String streamStatus;                // 3fc9b9ac: "live", "scheduled", "finished", ...
    bool  streamScheduled = false;      // 3fc9b9bd
    bool  streamKnown     = false;      // 3fc9b9be
    // counters
    uint32_t dataVersion = 0;           // 3fc9bb48, bumped by every successful fetch
    uint32_t lastFetchMs = 0;           // 3fc9bb58
};

extern LiveData live;
extern SemaphoreHandle_t g_data_mutex;  // 3fc9bb50
extern SemaphoreHandle_t g_http_mutex;  // 3fc9bb54, serialises HTTP clients

struct NewsItem { long ts; String headline; String author; };
extern std::vector<NewsItem> g_news;

struct BitaxeDevice {
    String ip, hostname, hashrate, temp, vrTemp, fan, bestDiff, bestSessionDiff, networkDiff, asic;
    // the firmware keeps 11 Strings (0xe0 bytes) per device
};
extern std::vector<BitaxeDevice> g_bitaxe;   // 3fc9ba6c

// ---- config API ----
void config_defaults();                  // FUN_4200a598
void config_load();                      // FUN_42008a84 (creates missing keys, then reads all)
void config_save_all();                  // used by /save
void config_reset();                     // /reset: clears the whole namespace
extern Preferences prefs;                // 3fc9bc7c
void bitaxe_parse_ips();                 // splits cfg.bitaxeIPs on ',' into g_bitaxe

// Debug logging, gated by cfg.debugEnabled (every Serial print in the firmware is).
#define DBG(...) do { if (cfg.debugEnabled) Serial.printf(__VA_ARGS__); } while (0)
