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

struct WifiJoin {
    bool ok = false;               // associated and got an address
    bool found = false;            // the name was in the scan, so the strongest access point was pinned
    int channel = 0;               // of the pinned access point
    int rssi = 0;
    int scan_code = 0;             // as in WifiScan
    int scan_count = 0;            // access points the scan found
};

// One connection attempt, the same for the boot connect and the reconnect task: drops any current link,
// scans, pins the strongest access point with this exact name (plain begin() when the scan does not see
// it), then waits up to `timeout_ms` for the connection. Holds the radio for the whole attempt, so a
// second user (a scan for the wizard's network list) waits instead of disconnecting a join in progress.
WifiJoin wifi_join(const String &ssid, const String &pass, uint32_t timeout_ms, void (*tick)() = nullptr);
