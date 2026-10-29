#include <WiFi.h>
#include <algorithm>
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

WifiScan wifi_scan(int attempts, void (*tick)())
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
