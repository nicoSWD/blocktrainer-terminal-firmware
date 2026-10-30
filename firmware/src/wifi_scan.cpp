#include <WiFi.h>
#include <algorithm>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "wifi_scan.h"

static const uint32_t RETRY_PAUSE_MS = 800;         // grows with every retry

static void pause_ms(uint32_t ms, void (*tick)())
{
    for (uint32_t t = 0; t < ms; t += 100) {
        delay(100);
        if (tick) tick();
    }
}

static bool stronger(const WifiAp &a, const WifiAp &b) { return a.rssi > b.rssi; }

// One owner of the radio at a time. Not recursive: the public functions lock, the *_locked ones below do not.
static SemaphoreHandle_t radio_mutex()
{
    static SemaphoreHandle_t m = xSemaphoreCreateMutex();
    return m;
}

struct RadioLock {
    RadioLock() { xSemaphoreTake(radio_mutex(), portMAX_DELAY); }
    ~RadioLock() { xSemaphoreGive(radio_mutex()); }
    RadioLock(const RadioLock &) = delete;
    RadioLock &operator=(const RadioLock &) = delete;
};

static WifiScan scan_locked(int attempts, void (*tick)())
{
    WifiScan r;
    if (attempts < 1) attempts = 1;
    for (int a = 1; a <= attempts; a++) {
        r.attempts = a;
        if (WiFi.status() != WL_CONNECTED) WiFi.disconnect(false, false);    // stop a pending join, keep the radio on
        int n = WiFi.scanNetworks(false, false, false, 300);
        r.code = n;
        if (n > 0) {
            for (int i = 0; i < n; i++) {
                WifiAp ap;
                ap.ssid = WiFi.SSID(i);
                ap.rssi = WiFi.RSSI(i);
                ap.channel = WiFi.channel(i);
                const uint8_t *b = WiFi.BSSID(i);
                if (b) memcpy(ap.bssid, b, 6);
                r.aps.push_back(ap);
            }
            WiFi.scanDelete();
            std::stable_sort(r.aps.begin(), r.aps.end(), stronger);
            return r;
        }
        WiFi.scanDelete();
        if (a < attempts) pause_ms(RETRY_PAUSE_MS * a, tick);
    }
    return r;
}

WifiScan wifi_scan(int attempts, void (*tick)())
{
    RadioLock lock;
    return scan_locked(attempts, tick);
}

WifiJoin wifi_join(const String &ssid, const String &pass, uint32_t timeout_ms, void (*tick)())
{
    RadioLock lock;
    WifiJoin j;
    WiFi.disconnect(true, false);
    delay(100);

    WifiScan scan = scan_locked(2, tick);
    j.scan_code = scan.code;
    j.scan_count = (int)scan.aps.size();
    const WifiAp *best = wifi_find_ap(scan.aps, ssid);
    if (best) {
        j.found = true;
        j.channel = best->channel;
        j.rssi = best->rssi;
        uint8_t bssid[6];
        memcpy(bssid, best->bssid, 6);
        WiFi.begin(ssid.c_str(), pass.c_str(), best->channel, bssid, true);
    } else {
        WiFi.begin(ssid.c_str(), pass.c_str());
    }

    for (uint32_t t0 = millis(); millis() - t0 < timeout_ms; delay(250)) {
        if (WiFi.status() == WL_CONNECTED) { j.ok = true; return j; }
        if (tick) tick();
    }
    return j;
}

std::vector<WifiAp> wifi_networks(const std::vector<WifiAp> &aps, size_t max)
{
    std::vector<WifiAp> sorted(aps);
    std::stable_sort(sorted.begin(), sorted.end(), stronger);
    std::vector<WifiAp> out;
    for (const WifiAp &ap : sorted) {
        if (out.size() >= max) break;
        if (ap.ssid.isEmpty()) continue;
        bool seen = false;
        for (const WifiAp &o : out) if (o.ssid == ap.ssid) { seen = true; break; }
        if (!seen) out.push_back(ap);
    }
    return out;
}

const WifiAp *wifi_find_ap(const std::vector<WifiAp> &aps, const String &ssid)
{
    if (ssid.isEmpty()) return nullptr;
    const WifiAp *best = nullptr;
    for (const WifiAp &ap : aps)
        if (ap.ssid == ssid && (!best || ap.rssi > best->rssi)) best = &ap;
    return best;
}
