// Background fetch tasks and the shared live data they fill.
#pragma once
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

// Index into the per-currency arrays below.
enum Currency { CUR_USD = 0, CUR_EUR, CUR_CHF };

struct NewsItem {
    long   ts;
    String headline;
    String author;
};

struct BitaxeDevice {
    String ip, hostname, hashrate, temp, vrTemp, fan;
    String bestDiff, sessionDiff, networkDiff;      // compact strings such as "1.23 G"
    float  hashGh = 0;                              // raw hashrate in GH/s
};

struct LiveData {
    // price (USD, EUR, CHF)
    float   price[3]       = {0, 0, 0};
    float   priceChange[3] = {0, 0, 0};     // percent, as reported by the API
    int     satsPerUnit    = 0;             // sats per USD
    int64_t priceStamp     = 0;             // API timestamp of the last price

    // mempool
    int   blockHeight = 0;
    float feeLow = 0, feeMid = 0, feeHigh = 0;   // sat/vB
    float feeCost[3][3] = {};                    // [currency][low, mid, high], cost of a 140 vB tx
    float diffProgress = 0;                      // percent of the current epoch
    float diffChange   = 0;                      // expected change, percent
    int   diffBlocks   = 0;                      // blocks to the next adjustment

    // chart: raw JSON plus the change over the selected timeframe
    String   chartJson;
    uint32_t chartVersion = 0;              // bumped when chartJson changes
    float    chartChange[3] = {0, 0, 0};

    // stream bar
    String streamStatus;                    // "live", "scheduled", ...
    String streamTitle;
    int64_t streamStart = 0;                // scheduled start, unix time (0 = unknown)
    bool   streamScheduled = false;
    bool   streamKnown     = false;

    uint32_t dataVersion = 0;               // bumped by every successful fetch
    uint32_t lastFetchMs = 0;
};

extern LiveData live;
extern std::vector<NewsItem> g_news;
extern std::vector<BitaxeDevice> g_bitaxe;
extern SemaphoreHandle_t g_data_mutex;      // guards `live`, `g_news`, `g_bitaxe`
extern SemaphoreHandle_t g_http_mutex;      // serialises HTTP clients (low heap)

void net_init();                            // creates the mutexes, call before anything else
void net_parse_bitaxe_ips();                // rebuilds g_bitaxe from cfg.bitaxeIPs
void net_apply_page_flags();                // starts, resumes or suspends tasks to match the enabled pages
void net_update_chart_change(uint32_t min_age_ms);   // derives live.chartChange from the chart JSON

void net_stack_report(String &out);         // stack headroom of the fetch tasks, for /diag
bool net_connected();                       // Wi-Fi is associated

// RAII lock for `live`/`g_news`/`g_bitaxe`.
struct DataLock {
    bool ok;
    DataLock() : ok(xSemaphoreTake(g_data_mutex, portMAX_DELAY) == pdTRUE) {}
    ~DataLock() { if (ok) xSemaphoreGive(g_data_mutex); }
    DataLock(const DataLock &) = delete;
    DataLock &operator=(const DataLock &) = delete;
};
