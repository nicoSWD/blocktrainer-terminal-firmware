// Shared between wifi_test.cpp and wifi_test_stubs.cpp.
#pragma once
#include <map>
#include <string>
#include <vector>
#include <stdint.h>

struct RadioAp {
    std::string ssid;
    int rssi;
    int channel;
    uint8_t bssid[6];
};

struct Radio {
    std::vector<RadioAp> env;           // access points in range, in the order the driver reports them
    std::vector<int> forced;            // return codes for the next scans: N >= 0, -2 failed, -1 use `env`
    bool busy_joining = false;          // scans fail until disconnect() is called
    uint32_t scan_ms = 3500;            // virtual duration of one scan
    int scan_calls = 0, disconnect_calls = 0, scan_delete_calls = 0;
    uint32_t waited_ms = 0;             // time spent in delay()
    uint32_t scan_time_ms = 0;          // time spent scanning
    // joining: begin() succeeds only for a name/password pair in `accept`, `join_ms` of virtual time later
    std::map<std::string, std::string> accept;
    uint32_t join_ms = 1500;
    int begin_calls = 0;
    std::string begin_ssid;
    int begin_channel = 0;              // 0 for a plain begin()
    uint8_t begin_bssid[6] = {0};
    bool begin_pinned = false;          // begin() was given a BSSID
    bool join_pending = false;
    uint32_t join_done_at = 0;
    void reset();
};
extern Radio g_radio;
