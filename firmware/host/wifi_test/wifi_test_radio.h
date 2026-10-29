// Shared between wifi_test.cpp and wifi_test_stubs.cpp.
#pragma once
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
    void reset();
};
extern Radio g_radio;
