// Wi-Fi network scan with retries, shared by the setup wizard, the setup access point page and the
// reconnect task, so the "scan fails or finds nothing" cases are handled in one place.
#pragma once
#include <Arduino.h>
#include <vector>

#define WIFI_SCAN_ATTEMPTS 3

struct WifiAp {
    String ssid;                   // empty for a hidden network
    int rssi = -100;
    int channel = 0;
    uint8_t bssid[6] = {0};
};

struct WifiScan {
    std::vector<WifiAp> aps;       // every access point that was found, strongest first
    int code = 0;                  // result of the last WiFi.scanNetworks(): count, 0 = nothing found, < 0 = failed
    int attempts = 0;              // scans that were run
};

// Runs up to `attempts` scans (at least one). A scan that fails or finds nothing is retried after a short
// pause: the radio refuses to scan while it is still joining or retrying a network, and the first scan after
// a mode change can come back empty. Anything but an established connection is disconnected before each
// scan. `tick` (optional) is called while waiting, for a watchdog. Results are copied out of the Wi-Fi
// driver, which is told to free its own copy, so they stay valid whatever the radio does next.
WifiScan wifi_scan(int attempts = WIFI_SCAN_ATTEMPTS, void (*tick)() = nullptr);

// One entry per visible network name, each with its strongest access point, strongest first, at most `max`.
// Hidden networks are left out. Names are compared exactly (they are case sensitive).
std::vector<WifiAp> wifi_networks(const std::vector<WifiAp> &aps, size_t max);

// The strongest access point that has exactly this name, or nullptr (also for an empty name).
const WifiAp *wifi_find_ap(const std::vector<WifiAp> &aps, const String &ssid);
