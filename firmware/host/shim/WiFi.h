// Host shim for the Wi-Fi API used by the UI: a fixed scan list and a switchable connection state.
#pragma once
#include "Arduino.h"

typedef enum {
    WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_SCAN_COMPLETED = 2,
    WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_CONNECTION_LOST = 5, WL_DISCONNECTED = 6
} wl_status_t;

class IPAddress {
public:
    uint8_t b[4] = {0, 0, 0, 0};
    IPAddress() {}
    IPAddress(uint8_t a, uint8_t b1, uint8_t c, uint8_t d) { b[0] = a; b[1] = b1; b[2] = c; b[3] = d; }
    String toString() const
    {
        char buf[20];
        snprintf(buf, sizeof(buf), "%u.%u.%u.%u", b[0], b[1], b[2], b[3]);
        return String(buf);
    }
};

extern bool g_host_wifi_connected;
extern int g_host_scan_count;       // how many entries scanNetworks() reports (max 18)

class WiFiClass {
public:
    wl_status_t status() { return g_host_wifi_connected ? WL_CONNECTED : WL_DISCONNECTED; }
    IPAddress localIP() { return IPAddress(192, 168, 178, 123); }
    int scanNetworks(bool async = false, bool show_hidden = false, bool passive = false, uint32_t max_ms_per_chan = 300);
    String SSID(int i);
    int32_t RSSI(int i);
    int32_t channel(int i);
    const uint8_t *BSSID(int i);
    bool disconnect(bool wifioff = false, bool eraseap = false);
    void scanDelete();
};
extern WiFiClass WiFi;
