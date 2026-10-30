// A scriptable Wi-Fi radio and a virtual clock for the host Wi-Fi scan test. The radio models what the
// ESP32 does that matters for scanning: a scan fails while a join is pending until disconnect() is called,
// results disappear after scanDelete(), and tests can force any return code for the next scans.
#include <string.h>
#include "WiFi.h"
#include "wifi_test_radio.h"

static uint32_t g_now = 0;
extern "C" uint32_t millis(void) { return g_now; }
extern "C" void delay(uint32_t ms)
{
    g_now += ms;
    g_radio.waited_ms += ms;
    if (g_radio.join_pending && g_now >= g_radio.join_done_at) {
        g_radio.join_pending = false;
        g_host_wifi_connected = true;
    }
}
extern "C" time_t host_time(time_t *t) { if (t) *t = 0; return 0; }

bool g_host_wifi_connected = false;
int g_host_scan_count = 0;
WiFiClass WiFi;
Radio g_radio;
static std::vector<RadioAp> results_;      // what the driver holds after the last scan
static bool results_valid_ = false;

void Radio::reset()
{
    *this = Radio();
    g_host_wifi_connected = false;
}

int WiFiClass::scanNetworks(bool, bool, bool, uint32_t)
{
    g_radio.scan_calls++;
    g_now += g_radio.scan_ms;
    g_radio.scan_time_ms += g_radio.scan_ms;
    results_valid_ = false;
    int code;
    if (!g_radio.forced.empty()) {
        code = g_radio.forced.front();
        g_radio.forced.erase(g_radio.forced.begin());
    } else if (g_radio.busy_joining) {
        code = -2;                                  // WIFI_SCAN_FAILED: the radio is busy joining a network
    } else {
        code = -1;                                  // use the environment
    }
    if (code == -1) code = (int)g_radio.env.size();
    if (code > 0) {
        results_ = g_radio.env;
        results_.resize(code);
        results_valid_ = true;
    }
    return code;
}

bool WiFiClass::disconnect(bool, bool)
{
    g_radio.disconnect_calls++;
    g_radio.busy_joining = false;
    g_radio.join_pending = false;
    g_host_wifi_connected = false;
    return true;
}

wl_status_t WiFiClass::begin(const char *ssid, const char *pass, int32_t channel, const uint8_t *bssid, bool)
{
    g_radio.begin_calls++;
    g_radio.begin_ssid = ssid ? ssid : "";
    g_radio.begin_channel = channel;
    g_radio.begin_pinned = bssid != nullptr;
    if (bssid) memcpy(g_radio.begin_bssid, bssid, 6);
    auto it = g_radio.accept.find(g_radio.begin_ssid);
    g_radio.busy_joining = true;                     // a pending join keeps the radio busy until disconnect()
    if (it != g_radio.accept.end() && it->second == (pass ? pass : "")) {
        g_radio.join_pending = true;
        g_radio.join_done_at = g_now + g_radio.join_ms;
    }
    return WL_DISCONNECTED;
}

void WiFiClass::scanDelete()
{
    g_radio.scan_delete_calls++;
    results_valid_ = false;
}

String WiFiClass::SSID(int i)
{
    if (!results_valid_ || i < 0 || i >= (int)results_.size()) return String();
    return String(results_[i].ssid.c_str());
}
int32_t WiFiClass::RSSI(int i) { return results_valid_ && i >= 0 && i < (int)results_.size() ? results_[i].rssi : 0; }
int32_t WiFiClass::channel(int i) { return results_valid_ && i >= 0 && i < (int)results_.size() ? results_[i].channel : 0; }
const uint8_t *WiFiClass::BSSID(int i)
{
    if (!results_valid_ || i < 0 || i >= (int)results_.size()) return nullptr;
    return results_[i].bssid;
}
