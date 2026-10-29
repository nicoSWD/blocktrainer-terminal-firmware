#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include "net.h"
#include "config.h"

LiveData live;
std::vector<NewsItem> g_news;
std::vector<BitaxeDevice> g_bitaxe;
SemaphoreHandle_t g_data_mutex;
SemaphoreHandle_t g_http_mutex;

bool net_connected() { return WiFi.status() == WL_CONNECTED; }

void net_init()
{
    g_data_mutex = xSemaphoreCreateMutex();
    g_http_mutex = xSemaphoreCreateMutex();
}

void net_parse_bitaxe_ips()
{
    g_bitaxe.clear();
    const String &s = cfg.bitaxeIPs;
    int start = 0;
    while (start < (int)s.length()) {
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

// ---------------------------------------------------------------- task table

static TaskHandle_t h_price, h_chart, h_mempool, h_bitaxe, h_stream, h_news;

static void task_price(void *);
static void task_chart(void *);
static void task_mempool(void *);
static void task_bitaxe(void *);
static void task_stream(void *);
static void task_news(void *);

// A fetch task only runs while a page that shows its data is enabled.
static bool wants_price()   { return cfg.pg[PAGE_PRICE] || cfg.pg[PAGE_FEES] || cfg.pg[PAGE_CHART]; }
static bool wants_chart()   { return cfg.pg[PAGE_CHART] || cfg.pg[PAGE_PRICE]; }
static bool wants_mempool() { return cfg.pg[PAGE_BLOCK] || cfg.pg[PAGE_FEES] || cfg.pg[PAGE_DIFF]; }
static bool wants_bitaxe()
{
    if (!cfg.pg[PAGE_BITAXE]) return false;
    DataLock lock;                          // callers must not hold the data lock
    return !g_bitaxe.empty();
}
static bool wants_stream()  { return cfg.showYoutubeBar; }
static bool wants_news()    { return true; }

struct TaskDef {
    TaskHandle_t *handle;
    TaskFunction_t fn;
    const char *name;
    uint32_t stack;
    bool (*wanted)();
};

static const TaskDef TASKS[] = {
    { &h_price,   task_price,   "FetchPrice",   0x2800, wants_price   },
    { &h_chart,   task_chart,   "FetchChart",   0x2800, wants_chart   },
    { &h_mempool, task_mempool, "FetchMempool", 0x2800, wants_mempool },
    { &h_bitaxe,  task_bitaxe,  "BitaxeStatus", 0x2800, wants_bitaxe  },
    { &h_stream,  task_stream,  "StreamStatus", 0x2800, wants_stream  },
    { &h_news,    task_news,    "FetchNews",    0x4000, wants_news    },
};

void net_stack_report(String &out)
{
    for (const TaskDef &t : TASKS)
        if (*t.handle) out += String("  ") + t.name + ": " + (unsigned)uxTaskGetStackHighWaterMark(*t.handle) + " bytes never used\n";
}

// Starts the tasks that are needed and do not exist yet. A task whose page is switched off
// is never suspended from outside (it might hold a mutex); it notices in its own loop
// and idles until it is wanted again.
void net_apply_page_flags()
{
    for (const TaskDef &t : TASKS) {
        if (*t.handle == nullptr && t.wanted()) {
            DBG("task %s: start\n", t.name);
            xTaskCreatePinnedToCore(t.fn, t.name, t.stack, nullptr, 1, t.handle, 0);
        }
    }
}

// ---------------------------------------------------------------- helpers

// One GET under the HTTP mutex. Returns the status code, `body` is filled on 200.
static int http_get(const String &url, String &body, bool no_cache = false)
{
    xSemaphoreTake(g_http_mutex, portMAX_DELAY);
    DBG("[heap] free=%u maxBlock=%u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    HTTPClient http;
    http.begin(url);
    http.addHeader("Connection", "close");
    http.addHeader("Accept", "application/json");
    if (no_cache) {
        http.addHeader("Accept-Encoding", "identity");
        http.addHeader("Cache-Control", "no-cache");
    }
    http.setUserAgent("ESP32");
    int code = http.GET();
    if (code == 200) body = http.getString();
    http.end();
    xSemaphoreGive(g_http_mutex);
    return code;
}

// Stack-friendly sleep: arduino-esp32 runs a 1 kHz tick, so ticks equal milliseconds.
static inline void sleep_ms(uint32_t ms) { vTaskDelay(ms); }

// ---------------------------------------------------------------- price

// Payload: {"t":1793266460,"p":{"EUR":[price,change%],"USD":[...],"CHF":[...]}}
static void task_price(void *)
{
    for (;;) {
        if (!wants_price()) { sleep_ms(500); continue; }
        if (!net_connected()) { sleep_ms(500); continue; }
        int interval = cfg.priceInterval;

        String body;
        int code = http_get(URL_PRICE, body);
        if (code == 200 && !body.isEmpty()) {
            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                DBG("price: JSON error %s\n", err.c_str());
            } else {
                JsonObject p = doc["p"];
                float pr[3] = { p["USD"][0] | 0.0f, p["EUR"][0] | 0.0f, p["CHF"][0] | 0.0f };
                float ch[3] = { p["USD"][1] | 0.0f, p["EUR"][1] | 0.0f, p["CHF"][1] | 0.0f };
                if (pr[0] <= 0 && pr[1] <= 0 && pr[2] <= 0) {
                    DBG("price: no USD/EUR/CHF prices in payload\n");
                } else {
                    int64_t t = doc["t"] | (int64_t)0;
                    {
                        DataLock lock;
                        for (int i = 0; i < 3; i++) { live.price[i] = pr[i]; live.priceChange[i] = ch[i]; }
                        if (t != 0) live.priceStamp = t;
                        live.satsPerUnit = pr[0] > 0 ? (int)(1e8f / pr[0]) : 0;
                        live.dataVersion++;
                        live.lastFetchMs = millis();
                    }
                    DBG("price: %.2f USD (%.2f%%) | %.2f EUR (%.2f%%) | %.2f CHF (%.2f%%)\n",
                        pr[0], ch[0], pr[1], ch[1], pr[2], ch[2]);
                }
            }
        } else if (code != -1) {
            DBG("price: HTTP %d\n", code);
        }
        sleep_ms(interval);
    }
}

// ---------------------------------------------------------------- mempool

static const int MEMPOOL_INTERVAL_MS = 10000;
static const float TX_VSIZE = 140.0f;        // reference transaction size for the fee costs

// Payload: {"height":N,"difficulty":{"progressPercent":..,"difficultyChange":..,"remainingBlocks":..},
//           "fees":{"low":..,"medium":..,"high":..}}
static void task_mempool(void *)
{
    for (;;) {
        if (!wants_mempool()) { sleep_ms(500); continue; }
        if (!net_connected()) { sleep_ms(500); continue; }

        String body;
        int code = http_get(URL_MEMPOOL, body);
        if (code == 200) {
            DynamicJsonDocument doc(2048);
            if (deserializeJson(doc, body)) {
                DBG("mempool: JSON error\n");
            } else {
                int height = doc["height"] | 0;
                float prog = doc["difficulty"]["progressPercent"] | 0.0f;
                float chg  = doc["difficulty"]["difficultyChange"] | 0.0f;
                int blocks = doc["difficulty"]["remainingBlocks"] | 0;
                float fee[3] = { doc["fees"]["low"] | 0.0f, doc["fees"]["medium"] | 0.0f,
                                 doc["fees"]["high"] | 0.0f };
                {
                    DataLock lock;
                    if (height > 0) live.blockHeight = height;
                    live.diffProgress = prog;
                    live.diffChange = chg;
                    live.diffBlocks = blocks;
                    live.feeLow = fee[0]; live.feeMid = fee[1]; live.feeHigh = fee[2];
                    for (int cur = 0; cur < 3; cur++)
                        for (int lvl = 0; lvl < 3; lvl++)
                            live.feeCost[cur][lvl] = live.price[cur] * fee[lvl] * TX_VSIZE / 1e8f;
                    live.dataVersion++;
                    live.lastFetchMs = millis();
                }
                DBG("mempool: block %d, difficulty %.1f%% / %.2f%% / %d blocks, fees %.1f/%.1f/%.1f\n",
                    height, prog, chg, blocks, fee[0], fee[1], fee[2]);
            }
        } else if (code != -1) {
            DBG("mempool: HTTP %d\n", code);
        }
        sleep_ms(MEMPOOL_INTERVAL_MS);
    }
}

// ---------------------------------------------------------------- chart

// Per-device offset (60..239 s) so a fleet of devices does not hit the API on the hour.
static uint32_t chart_jitter_s()
{
    uint64_t mac = ESP.getEfuseMac();
    uint32_t x = (uint32_t)(mac >> 32) ^ (uint32_t)mac;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x % 180 + 60;
}

// The chart payload changes once an hour: fetch just after each full hour, retry after
// 2, 5 and 10 minutes when the data is stale, and poll every 5 s until the first data arrives.
static void task_chart(void *)
{
    static const uint32_t RETRY_MS[3] = { 120000, 300000, 600000 };
    static const uint32_t BOOTSTRAP_MS = 5000;
    const uint32_t jitter = chart_jitter_s();
    uint32_t next_at = 0;
    uint8_t retries = 0;
    bool have_data = false;

    for (;;) {
        if (!wants_chart()) { sleep_ms(500); continue; }
        if (!net_connected()) { sleep_ms(500); continue; }
        if (next_at != 0 && (int32_t)(next_at - millis()) > 0) { sleep_ms(250); continue; }

        String body;
        int code = http_get(URL_CHART, body);
        if (code == 200 && body.indexOf("\"tf\"") >= 0) {
            {
                DataLock lock;
                live.chartJson = body;
                live.chartVersion++;
                live.dataVersion++;
                live.lastFetchMs = millis();
            }
            have_data = true;
            retries = 0;
            uint32_t into_hour_s = (uint32_t)(time(nullptr) % 3600);
            next_at = millis() + (3600 - into_hour_s + jitter) * 1000UL;
            DBG("chart: stored, jitter %us\n", (unsigned)jitter);
        } else {
            if (code == 200) DBG("chart: unexpected payload (len %d)\n", (int)body.length());
            else if (code != -1) DBG("chart: HTTP %d\n", code);
            next_at = millis() + (have_data && retries < 3 ? RETRY_MS[retries++] : BOOTSTRAP_MS);
        }
        net_update_chart_change(0);
    }
}

// Change of each currency against the oldest point of the selected timeframe.
// Payload: {"tf":{"24h":{"step":1800,"p":[[ts,EUR,USD,CHF],...]},"5d":{"step":3600,"p":[...]}}}
void net_update_chart_change(uint32_t min_age_ms)
{
    static uint32_t last = 0;
    if (min_age_ms && millis() - last < min_age_ms) return;

    String json;
    float now_p[3];
    {
        DataLock lock;
        json = live.chartJson;
        memcpy(now_p, live.price, sizeof(now_p));
    }
    if (json.isEmpty()) return;

    DynamicJsonDocument doc(24576);
    if (deserializeJson(doc, json)) return;
    const char *tf = (cfg.chartTf == 2 || cfg.chartTf == 3) ? "5d" : "24h";
    JsonArray pts = doc["tf"][tf]["p"];
    if (pts.isNull() || pts.size() < 1) {
        DBG("chart: no points for timeframe %s\n", tf);
        return;
    }
    float ref[3] = { pts[0][2] | 0.0f, pts[0][1] | 0.0f, pts[0][3] | 0.0f };   // USD, EUR, CHF
    float ch[3];
    for (int i = 0; i < 3; i++)
        ch[i] = ref[i] > 0 && now_p[i] > 0 ? (now_p[i] - ref[i]) / ref[i] * 100.0f : 0.0f;
    {
        DataLock lock;
        memcpy(live.chartChange, ch, sizeof(ch));
    }
    last = millis();
}

// ---------------------------------------------------------------- news

// Payload: array of {"ts":..,"headline":"..","author":".."}
static void task_news(void *)
{
    for (;;) {
        if (!wants_news()) { sleep_ms(500); continue; }
        if (!net_connected()) { sleep_ms(5000); continue; }

        String body;
        int code = http_get(URL_NEWS, body);
        if (code == 200) {
            DynamicJsonDocument doc(8192);
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                DBG("news: JSON error %s\n", err.c_str());
            } else {
                std::vector<NewsItem> items;
                JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc["news"].as<JsonArray>();
                for (JsonObject o : arr)
                    items.push_back({ (long)(o["ts"] | 0L), String((const char *)(o["headline"] | "")),
                                      String((const char *)(o["author"] | "")) });
                DataLock lock;
                g_news.swap(items);
                live.dataVersion++;
                live.lastFetchMs = millis();
                DBG("news: %d items\n", (int)g_news.size());
            }
        } else if (code != -1) {
            DBG("news: HTTP %d\n", code);
        }
        sleep_ms(600000);
    }
}

// ---------------------------------------------------------------- stream bar

// "2025-10-29T18:00:00Z" (a numeric UTC offset is applied) -> unix time, 0 when unparsable.
static int64_t parse_iso_utc(const String &s)
{
    int y, mo, d, h, mi, sec = 0;
    if (sscanf(s.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &sec) < 5) return 0;
    // days since 1970-01-01 (proleptic Gregorian, Howard Hinnant's algorithm)
    y -= mo <= 2;
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + (int)doe - 719468;
    int64_t t = days * 86400 + h * 3600 + mi * 60 + sec;
    int n = s.length();
    if (n > 6 && (s[n - 6] == '+' || s[n - 6] == '-') && s[n - 3] == ':') {
        int oh = s.substring(n - 5, n - 3).toInt(), om = s.substring(n - 2).toInt();
        int off = oh * 3600 + om * 60;
        t += s[n - 6] == '+' ? -off : off;
    }
    return t;
}

// Payload: {"status":"live|scheduled|..","title":..,"scheduled_time":..,"actual_start_time":..}
// Poll every 5 min while live, 10 min when a stream is scheduled, otherwise every 20 min.
static void task_stream(void *)
{
    for (;;) {
        if (!wants_stream()) { sleep_ms(500); continue; }
        if (!net_connected()) { sleep_ms(500); continue; }

        String body;
        int code = http_get(URL_YOUTUBE, body, true);
        uint32_t next_ms = 1200000;
        if (code == 200) {
            DynamicJsonDocument doc(2048);
            DeserializationError err = deserializeJson(doc, body);
            if (err) {
                DBG("stream: JSON error %s\n", err.c_str());
            } else if (doc.containsKey("error")) {
                DBG("stream: server reported an error\n");
            } else {
                String status = doc["status"] | "";
                String title  = doc["title"] | "";
                String sched  = doc["scheduled_time"] | "";
                bool is_live = status == "live";
                bool is_sched = status == "scheduled";
                if (is_live) next_ms = 300000;
                else if (is_sched && !sched.isEmpty()) next_ms = 600000;
                {
                    DataLock lock;
                    live.streamScheduled = is_sched;
                    live.streamStatus = status;
                    live.streamTitle = title;
                    live.streamStart = parse_iso_utc(sched);
                    live.streamKnown = true;
                    live.dataVersion++;
                    live.lastFetchMs = millis();
                }
                DBG("stream: %s, next poll in %lu ms\n", status.c_str(), (unsigned long)next_ms);
            }
        } else {
            DBG("stream: HTTP %d\n", code);
        }
        sleep_ms(next_ms);
    }
}

// ---------------------------------------------------------------- Bitaxe miners

// 1234567 -> "1.23 M"
static String diff_string(double v)
{
    static const char *UNITS[] = { "", " K", " M", " G", " T", " P" };
    int u = 0;
    while (v >= 1000.0 && u < 5) { v /= 1000.0; u++; }
    return String(v, u ? 2 : 0) + UNITS[u];
}

// Polls one miner per cycle (round robin): GET http://<ip>/api/system/info, one retry.
// Field names differ between miner firmware versions, so aliases are accepted.
static void task_bitaxe(void *)
{
    static const uint32_t CYCLE_MS = 2000;
    uint32_t idx = 0;
    for (;;) {
        if (!wants_bitaxe()) { sleep_ms(500); continue; }
        if (!net_connected()) { sleep_ms(CYCLE_MS); continue; }
        String ip;
        uint32_t dev;
        {
            DataLock lock;
            if (g_bitaxe.empty()) { ip = ""; dev = 0; }
            else {
                if (idx >= g_bitaxe.size()) idx = 0;
                ip = g_bitaxe[idx].ip;
                dev = idx;
                idx = (idx + 1) % g_bitaxe.size();
            }
        }
        if (ip.isEmpty()) { sleep_ms(CYCLE_MS); continue; }

        String url = String("http://") + ip + "/api/system/info";
        String body;
        int code = -1;
        for (int attempt = 0; attempt < 2 && code != 200; attempt++)
            code = http_get(url, body);

        if (code != 200) {
            DBG("bitaxe %u (%s): unreachable\n", (unsigned)dev, ip.c_str());
        } else {
            DynamicJsonDocument doc(4096);
            if (deserializeJson(doc, body)) {
                DBG("bitaxe %u: JSON error\n", (unsigned)dev);
            } else {
                float gh   = doc["hashRate"] | (doc["hash_rate_5s"] | (doc["hash_rate"] | 0.0f));
                float temp = doc["temp"] | 0.0f;
                float vr   = doc["vrTemp"] | 0.0f;
                float fan  = doc["fanspeed"] | (doc["fanSpeed"] | 0.0f);
                String best    = diff_string(doc["bestDiff"] | 0.0);
                String session = diff_string(doc["bestSessionDiff"] | 0.0);
                String network = diff_string(doc["networkDifficulty"] | 0.0);
                DataLock lock;
                if (dev < g_bitaxe.size() && g_bitaxe[dev].ip == ip) {
                    BitaxeDevice &d = g_bitaxe[dev];
                    d.hostname = String((const char *)(doc["hostname"] | ""));
                    d.hashrate = gh >= 1000 ? String(gh / 1000.0f, 2) + " TH/s" : String(gh, 0) + " GH/s";
                    d.temp = String(temp, 1);
                    d.vrTemp = String(vr, 1);
                    d.fan = String(fan, 0);
                    d.hashGh = gh;
                    d.bestDiff = best;
                    d.sessionDiff = session;
                    d.networkDiff = network;
                    live.dataVersion++;
                }
                DBG("bitaxe %u: %.3f GH/s\n", (unsigned)dev, gh);
            }
        }
        sleep_ms(CYCLE_MS);
    }
}
