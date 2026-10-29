// data.cpp: the six fetch tasks and the task table that starts/suspends them.
//
// Verified against the decompile unless marked UNVERIFIED:
//  * the task table at 0x3fc95718 (handle, entry, name, stack, enable predicate)
//  * start = xTaskCreatePinnedToCore(fn, name, stack, NULL, prio 1, &handle, core 0)
//    or vTaskResume when the task exists but is suspended; disabled => vTaskSuspend
//  * request headers, the two mutexes (data 0x3fc9bb50, http 0x3fc9bb54) and
//    the JSON key names read by each task
//  * vTaskDelay() takes ms here (arduino-esp32 runs a 1 kHz tick)

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "data.h"
#include "config.h"

bool wifi_connected() { return WiFi.status() == WL_CONNECTED; }

// ------------------------------------------------------------------ task table
static TaskHandle_t h_price, h_chart, h_mempool, h_bitaxe, h_stream, h_news;

// enable predicates (FUN_4200b27c, b298, b2ac, b2c8, b2e8)
static bool en_price()   { return cfg.pg[PAGE_PRICE] || cfg.pg[PAGE_FEES] || cfg.pg[PAGE_CHART]; }
static bool en_chart()   { return cfg.pg[PAGE_CHART] || cfg.pg[PAGE_PRICE]; }
static bool en_mempool() { return cfg.pg[PAGE_BLOCK] || cfg.pg[PAGE_FEES] || cfg.pg[PAGE_DIFF]; }
static bool en_bitaxe()  { return cfg.pg[PAGE_BITAXE] && !g_bitaxe.empty(); }
static bool en_stream()  { return cfg.showYoutubeBar; }
static bool en_news()    { return true; }   // table entry uses a stub that returns true (0x42113d60)

struct TaskDef {
    TaskHandle_t *handle;
    TaskFunction_t fn;
    const char *name;
    uint32_t stack;
    bool (*enabled)();
};

static const TaskDef TASKS[] = {
    { &h_price,   task_fetch_price,   "FetchPrice",   0x2000, en_price   },
    { &h_chart,   task_fetch_chart,   "FetchChart",   0x2000, en_chart   },
    { &h_mempool, task_fetch_mempool, "FetchMempool", 0x2000, en_mempool },
    { &h_bitaxe,  task_bitaxe_status, "BitaxeStatus", 0x2000, en_bitaxe  },
    { &h_stream,  task_stream_status, "StreamStatus", 0x2000, en_stream  },
    { &h_news,    task_fetch_news,    "FetchNews",    0x4000, en_news    },
};

void tasks_apply_page_flags()
{
    for (const TaskDef &t : TASKS) {
        if (t.enabled()) {
            if (*t.handle == nullptr) {
                DBG("Task %s wird gestartet\n", t.name);
                xTaskCreatePinnedToCore(t.fn, t.name, t.stack, nullptr, 1, t.handle, 0);
            } else if (eTaskGetState(*t.handle) == eSuspended) {
                DBG("Task %s wird fortgesetzt\n", t.name);
                vTaskResume(*t.handle);
            }
        } else {
            if (*t.handle == nullptr) {
                DBG("Task %s nicht gestartet\n", t.name);
            } else if (eTaskGetState(*t.handle) == eSuspended) {
                DBG("Task %s bereits pausiert\n", t.name);
            } else {
                DBG("Task %s wird pausiert\n", t.name);
                vTaskSuspend(*t.handle);
            }
        }
    }
    if (cfg.debugEnabled) Serial.println("Tasks aktualisiert");
}

// ------------------------------------------------------------------ helpers
// Every task does: take http mutex -> HTTPClient with these headers -> GET.
static int http_get(HTTPClient &http, const String &url, String &body, bool youtube = false)
{
    http.begin(url);
    http.addHeader("Connection", "close");
    http.addHeader("Accept", "application/json");
    if (youtube) {
        http.addHeader("Accept-Encoding", "identity");
        http.addHeader("Cache-Control", "no-cache");
    }
    http.setUserAgent("ESP32");
    int code = http.GET();
    if (code == 200) body = http.getString();
    return code;
}

static void log_heap()
{
    static size_t min_free = SIZE_MAX, min_block = SIZE_MAX;
    size_t f = ESP.getFreeHeap(), b = ESP.getMaxAllocHeap();
    if (f < min_free) min_free = f;
    if (b < min_block) min_block = b;
    DBG("[HEAP] free=%u (min=%u)  maxBlock=%u (min=%u)\n", (unsigned)f, (unsigned)min_free,
        (unsigned)b, (unsigned)min_block);
}

static inline bool lock_data()   { return xSemaphoreTake(g_data_mutex, portMAX_DELAY) == pdTRUE; }
static inline void unlock_data() { xSemaphoreGive(g_data_mutex); }

// ------------------------------------------------------------------ FetchPrice
// FUN_4201fc6c. Waits cfg.priceInterval between requests, 250 ms when the URL is
// empty, 500 ms while WiFi is down.
void task_fetch_price(void *)
{
    DBG("💰 FetchPrice Task gestartet.\n");
    for (;;) {
        String url = URL_PRICE;                       // copy of 0x3fc9ba10 under the data mutex
        int interval = cfg.priceInterval;
        if (url.isEmpty()) { vTaskDelay(250); continue; }
        if (!wifi_connected()) { vTaskDelay(500); continue; }

        xSemaphoreTake(g_http_mutex, portMAX_DELAY);
        log_heap();
        HTTPClient http;
        String body;
        int code = http_get(http, url, body);
        http.end();
        xSemaphoreGive(g_http_mutex);

        if (code == 200) {
            if (body.isEmpty()) {
                DBG("⚠️ FetchPrice: leeres Payload trotz 200 OK.\n");
            } else {
                // Live payload (checked against api.blocktrainer.de):
                //   {"t":1793266460,"p":{"EUR":[price,change%],"USD":[price,change%],"CHF":[price,change%]}}
                DynamicJsonDocument doc(2048);
                DeserializationError err = deserializeJson(doc, body);
                if (err) {
                    DBG("⚠️ FetchPrice JSON Fehler: %s\n", err.c_str());
                } else {
                    JsonObject p = doc["p"];
                    float pr[3] = { p["USD"][0] | 0.0f, p["EUR"][0] | 0.0f, p["CHF"][0] | 0.0f };
                    float ch[3] = { p["USD"][1] | 0.0f, p["EUR"][1] | 0.0f, p["CHF"][1] | 0.0f };
                    if (pr[0] <= 0 && pr[1] <= 0 && pr[2] <= 0) {
                        DBG("⚠️ FetchPrice: Keine USD/EUR/CHF Preise im JSON gefunden.\n");
                    } else {
                        int64_t t = doc["t"] | (int64_t)0;
                        if (lock_data()) {
                            for (int i = 0; i < 3; i++) { live.price[i] = pr[i]; live.priceExtra[i] = ch[i]; }
                            if (t != 0) live.priceStamp = t;
                            live.dataVersion++;
                            live.satsPerUnit = pr[0] > 0 ? (int)(1e8f / pr[0]) : 0;   // sats per USD ("moscow"), FUN_4201d2f4
                            unlock_data();
                        }
                        DBG("💰 BTC Preise: %.2f USD (%.2f%%) | %.2f EUR (%.2f%%) | %.2f CHF (%.2f%%)\n",
                            pr[0], ch[0], pr[1], ch[1], pr[2], ch[2]);
                        live.lastFetchMs = millis();
                    }
                }
            }
        } else if (code != -1) {
            DBG("⚠️ FetchPrice HTTP Fehler: %d\n", code);
        }
        vTaskDelay(interval);
    }
}

// ------------------------------------------------------------------ FetchMempool
// FUN_4201770c. Interval: 0x3fc9bac4, default 10000 ms.
static const int MEMPOOL_INTERVAL_MS = 10000;

void task_fetch_mempool(void *)
{
    for (;;) {
        if (!wifi_connected()) { vTaskDelay(500); continue; }

        xSemaphoreTake(g_http_mutex, portMAX_DELAY);
        log_heap();
        HTTPClient http;
        String body;
        int code = http_get(http, URL_MEMPOOL, body);
        http.end();
        xSemaphoreGive(g_http_mutex);

        if (code == 200) {
            DynamicJsonDocument doc(2048);
            if (deserializeJson(doc, body)) {
                DBG("⚠️ JSON-Fehler bei Mempool-Daten.\n");
            } else {
                // keys in rodata order: height, difficulty{progressPercent,difficultyChange,
                // remainingBlocks}, fees{low,medium,high}
                int height   = doc["height"] | 0;
                float prog   = doc["difficulty"]["progressPercent"] | 0.0f;
                float chg    = doc["difficulty"]["difficultyChange"] | 0.0f;
                int blocks   = doc["difficulty"]["remainingBlocks"] | 0;
                float fl     = doc["fees"]["low"] | 0.0f;
                float fm     = doc["fees"]["medium"] | 0.0f;
                float fh     = doc["fees"]["high"] | 0.0f;
                // cost in BTC of a 140 vB transaction at each fee level
                float c[3] = { fl * 140.0f / 1e8f, fm * 140.0f / 1e8f, fh * 140.0f / 1e8f };
                if (lock_data()) {
                    if (height > 0) live.blockHeight = height;
                    live.diffProgress = prog;
                    live.diffChange = chg;
                    live.diffBlocks = blocks;
                    live.dataVersion++;
                    live.feeLow = fl; live.feeMid = fm; live.feeHigh = fh;
                    for (int cur = 0; cur < 3; cur++)          // price[] x cost: USD, EUR, CHF rows
                        for (int lvl = 0; lvl < 3; lvl++)
                            live.feeCost[cur][lvl] = live.price[cur] * c[lvl];
                    unlock_data();
                }
                DBG("📦⛏️💵 Mempool: Block=%d, Diff=%.1f%%/%.2f%%/%dBlöcke, Fees(low/med/high)=%.1f/%.1f/%.1f\n",
                    height, prog, chg, blocks, fl, fm, fh);
                live.lastFetchMs = millis();
            }
        } else if (code != -1) {
            DBG("HTTP Fehler: %d\n", code);
        }
        vTaskDelay(MEMPOOL_INTERVAL_MS);
    }
}

// ------------------------------------------------------------------ FetchChart
// FUN_42014a70. Stores the raw JSON ({"tf": "...", "points": [...]}) in live.chartJson;
// the pages parse "points" lazily (FUN_42034b20 / FUN_420272bc). Hourly cadence
// with a per-device jitter:
//   jitter_s = xorshift32(efuseMacHi ^ efuseMacLo) % 180 + 60     (FUN_420147c4)
// Fast "BOOTSTRAP" polling every 5000 ms until the first data arrives, then
// "HOURLY": if the data is not yet from the new hour, retry after 120/300/600 s
// (table at 0x3c218ea4), then fall back to the normal interval.
static uint32_t chart_jitter_s()
{
    uint64_t mac = ESP.getEfuseMac();
    uint32_t x = (uint32_t)(mac >> 32) ^ (uint32_t)mac;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x % 180 + 60;
}

void task_fetch_chart(void *)
{
    DBG("📈 FetchChart Task gestartet.\n");
    static const uint32_t RETRY_MS[3] = { 120000, 300000, 600000 };
    const uint32_t jitter = chart_jitter_s();
    const uint32_t boot_interval = 5000;      // 0x3fc9ba44
    uint32_t next_at = 0;
    uint8_t retries = 0;
    bool have_data = false;
    for (;;) {
        if (!wifi_connected()) { vTaskDelay(500); continue; }
        if (next_at != 0 && (int32_t)(next_at - millis()) > 0) { vTaskDelay(250); continue; }

        xSemaphoreTake(g_http_mutex, portMAX_DELAY);
        log_heap();
        HTTPClient http;
        String body;
        int code = http_get(http, URL_CHART, body);
        http.end();
        xSemaphoreGive(g_http_mutex);

        if (code == 200 && body.indexOf("\"tf\"") >= 0) {
            if (lock_data()) {
                live.chartJson = body;
                live.dataVersion++;
                unlock_data();
            }
            live.lastFetchMs = millis();
            have_data = true;
            retries = 0;
            // schedule the next hourly fetch (UNVERIFIED: exact alignment to the hour)
            time_t now = time(nullptr);
            uint32_t into_hour_s = (uint32_t)(now % 3600);
            uint32_t wait_s = 3600 - into_hour_s + jitter;
            next_at = millis() + wait_s * 1000UL;
            DBG("✅ ChartJson gespeichert. HourJitter=%us (mode=%s)\n", jitter, "HOURLY");
        } else {
            if (code == 200) DBG("⚠️ FetchChart: Unerwartetes Payload-Format (len=%d). Anfang: %.60s\n", (int)body.length(), body.c_str());
            else if (code != -1) DBG("⚠️ FetchChart HTTP Fehler: %d\n", code);
            next_at = millis() + (have_data && retries < 3 ? RETRY_MS[retries++] : boot_interval);
        }
        chart_update_changes(0);
    }
}

// ------------------------------------------------------------------ chart helpers
// FUN_42035094 / FUN_42034e8c: change of each currency against the oldest point of the
// selected timeframe. Live payload (checked):
//   {"tf":{"24h":{"step":1800,"p":[[ts,EUR,USD,CHF], ...]}, "5d":{"step":3600,"p":[...]}}}
// The reference prices land in 0x3fc9f748 (EUR), 0x3fc9f744 (USD), 0x3fc9f740 (CHF); the
// timeframe is "5d" when chartTf is 2 or 3, else "24h". The percent formula (FUN_42034ae4)
// is UNVERIFIED: (now - reference) / reference * 100.
void chart_update_changes(uint32_t min_age_ms)
{
    static uint32_t last = 0;
    if (min_age_ms && millis() - last < min_age_ms) return;
    String json;
    float now_p[3];
    if (lock_data()) { json = live.chartJson; memcpy(now_p, live.price, sizeof(now_p)); unlock_data(); }
    else return;
    if (json.isEmpty()) return;
    DynamicJsonDocument doc(24576);
    if (deserializeJson(doc, json)) return;
    const char *tf = (cfg.chartTf == 2 || cfg.chartTf == 3) ? "5d" : "24h";
    JsonArray pts = doc["tf"][tf]["p"];
    if (pts.isNull() || pts.size() < 1) {
        DBG("📈 ChartPage: tf[%s] -> p Array nicht gefunden\n", tf);
        return;
    }
    float ref_eur = pts[0][1] | 0.0f, ref_usd = pts[0][2] | 0.0f, ref_chf = pts[0][3] | 0.0f;
    float ch[3] = {
        ref_usd > 0 && now_p[0] > 0 ? (now_p[0] - ref_usd) / ref_usd * 100.0f : 0.0f,
        ref_eur > 0 && now_p[1] > 0 ? (now_p[1] - ref_eur) / ref_eur * 100.0f : 0.0f,
        ref_chf > 0 && now_p[2] > 0 ? (now_p[2] - ref_chf) / ref_chf * 100.0f : 0.0f,
    };
    if (lock_data()) { memcpy(live.chartChange, ch, sizeof(ch)); unlock_data(); }
    last = millis();
}

// ------------------------------------------------------------------ FetchNews
// FUN_4201b28c. Items carry "ts", "headline", "author". 5 s retry while WiFi is
// down, otherwise every 600000 ms.
void task_fetch_news(void *)
{
    DBG("📰 FetchNews Task gestartet.\n");
    for (;;) {
        if (!wifi_connected()) { vTaskDelay(5000); continue; }

        xSemaphoreTake(g_http_mutex, portMAX_DELAY);
        log_heap();
        HTTPClient http;
        String body;
        int code = http_get(http, URL_NEWS, body);
        http.end();
        xSemaphoreGive(g_http_mutex);

        if (code == 200) {
            DynamicJsonDocument doc(8192);
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                DBG("⚠️ FetchNews JSON Error: %s\n", err.c_str());
            } else {
                std::vector<NewsItem> items;
                JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["news"].as<JsonArray>();
                for (JsonObject o : arr)
                    items.push_back({ (long)(o["ts"] | 0L), String((const char *)(o["headline"] | "")),
                                      String((const char *)(o["author"] | "")) });
                if (lock_data()) {
                    g_news.swap(items);
                    live.dataVersion++;
                    unlock_data();
                }
                DBG("📰 %d News geladen.\n", (int)g_news.size());
                live.lastFetchMs = millis();
            }
        } else if (code != -1) {
            DBG("⚠️ FetchNews HTTP Error: %d\n", code);
        }
        vTaskDelay(600000);
    }
}

// ------------------------------------------------------------------ StreamStatus
// FUN_42023190: youtube bar. JSON keys: status, title, scheduled_time,
// actual_start_time, error. Next poll delay:
//   live                         -> 300000 ms (0x3fc9b9cc)
//   otherwise                    -> 1200000 ms (0x3fc9b9c0)
//   scheduled with a valid time  -> 600000 ms (0x3fc9b9c4), shortened to the
//                                   remaining time if the start is nearer
void task_stream_status(void *)
{
    for (;;) {
        if (!wifi_connected()) { vTaskDelay(500); continue; }

        xSemaphoreTake(g_http_mutex, portMAX_DELAY);
        log_heap();
        HTTPClient http;
        String body;
        int code = http_get(http, URL_YOUTUBE, body, true);
        http.end();
        xSemaphoreGive(g_http_mutex);

        uint32_t next_ms = 1200000;
        if (code == 200) {
            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                DBG("[YT] JSON Parse Fehler: %s\n", err.c_str());
            } else if (doc.containsKey("error")) {
                DBG("[YT] JSON enthaelt Feld 'error' – Server meldet Fehler.\n");
            } else {
                String status = doc["status"] | "";
                String title  = doc["title"] | "";
                String sched  = doc["scheduled_time"] | "";
                String actual = doc["actual_start_time"] | "";
                DBG("========== [YT] JSON OK ==========\n");
                DBG("[YT] Status         : %s\n", status.c_str());
                DBG("[YT] Titel          : %s\n", title.c_str());
                DBG("[YT] Scheduled Time : %s\n", sched.c_str());
                DBG("[YT] Actual Start   : %s\n", actual.c_str());
                bool is_live = status == "live", is_sched = status == "scheduled";
                if (is_live) next_ms = 300000;
                else if (is_sched && !sched.isEmpty()) next_ms = 600000;  // shortened below when imminent
                DBG("[YT] nextDelayMs     : %lu ms\n", (unsigned long)next_ms);
                DBG("=================================\n");
                if (lock_data()) {
                    live.streamScheduled = is_sched;
                    live.streamStatus = status;
                    live.streamKnown = true;
                    live.dataVersion++;
                    unlock_data();
                }
                live.lastFetchMs = millis();
            }
        } else {
            DBG("[YT] HTTP GET fehlgeschlagen. code=%d url=%s\n", code, URL_YOUTUBE);
        }
        vTaskDelay(next_ms);
    }
}

// ------------------------------------------------------------------ BitaxeStatus
// FUN_420113d0. Polls one device per cycle (round robin, index 0x3fc9e6cc) at
// GET http://<ip>/api/system/info, one retry on failure. Cycle delay is
// max(0x3fc9ba78 (default 2000), 500) ms. Accepted key aliases:
//   hashRate | hash_rate_5s | hash_rate, temp, vrTemp, fanspeed | fanSpeed,
//   bestDiff, bestSessionDiff, networkDifficulty, hostname, asic / stats
// Display formatting (TH/s vs GH/s, "%.2f", "%.1f", "%d") is UNVERIFIED.
void task_bitaxe_status(void *)
{
    uint32_t idx = 0;
    const uint32_t cycle_ms = 2000;
    for (;;) {
        if (!wifi_connected() || g_bitaxe.empty()) { vTaskDelay(cycle_ms < 500 ? 500 : cycle_ms); continue; }
        if (idx >= g_bitaxe.size()) idx = 0;
        BitaxeDevice &d = g_bitaxe[idx];
        int dev = (int)idx;
        idx = (idx + 1) % g_bitaxe.size();

        String url = String("http://") + d.ip + "/api/system/info";
        String body;
        int code = -1;
        for (int attempt = 0; attempt < 2 && code != 200; attempt++) {
            xSemaphoreTake(g_http_mutex, portMAX_DELAY);
            HTTPClient http;
            code = http_get(http, url, body);
            http.end();
            xSemaphoreGive(g_http_mutex);
            if (code != 200 && attempt == 0)
                DBG("⏳ [Bitaxe] Erster Versuch fehlgeschlagen (Device %d, IP: %s) – Retry...\n", dev, d.ip.c_str());
        }
        if (code != 200) {
            DBG("❌ [Bitaxe] Verbindung endgültig fehlgeschlagen (Device %d, IP: %s)\n", dev, d.ip.c_str());
        } else {
            DynamicJsonDocument doc(4096);
            if (deserializeJson(doc, body)) {
                DBG("⚠️ [Bitaxe] JSON-Parse-Fehler (Device %d)\n", dev);
            } else {
                float gh = doc["hashRate"] | (doc["hash_rate_5s"] | (doc["hash_rate"] | 0.0f));
                float temp = doc["temp"] | 0.0f;
                float vr = doc["vrTemp"] | 0.0f;
                float fan = doc["fanspeed"] | (doc["fanSpeed"] | 0.0f);
                BitaxeDevice nd = d;
                nd.hostname = String((const char *)(doc["hostname"] | ""));
                nd.hashrate = gh >= 1000 ? String(gh / 1000.0f, 2) + " TH/s" : String(gh, 0) + " GH/s";
                nd.temp = String(temp, 1);
                nd.vrTemp = String(vr, 1);
                nd.fan = String(fan, 0);
                if (lock_data()) {
                    d = nd;
                    live.dataVersion++;
                    unlock_data();
                }
                DBG("[BX] Device #%d | HashRate: %.3f GH/s\n", dev, gh);
            }
        }
        vTaskDelay(cycle_ms < 500 ? 500 : cycle_ms);
    }
}
