// Host replacements for everything the UI sources reference outside the UI: shared data (net.cpp),
// settings storage (config.cpp), hardware (hw.cpp), OTA / Wi-Fi state, diagnostics, Arduino bits.
#include <Arduino.h>
#include <WiFi.h>
#include <atomic>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#define malloc_size malloc_usable_size
#endif
#include "config.h"
#include "net.h"
#include "hw.h"
#include "ota.h"
#include "wifi_link.h"
#include "diag.h"
#include "host_display.h"
#include "host_api.h"

// ---------------------------------------------------------------- clock
static uint32_t g_clock_ms = 100000;
static time_t g_wall_base = 1763127127;      // Fri 2025-11-14 14:32:07 Europe/Berlin
extern "C" uint32_t millis(void) { return g_clock_ms; }
extern "C" void delay(uint32_t ms) { g_clock_ms += ms; }
extern "C" time_t host_time(time_t *t)
{
    time_t v = g_wall_base + (time_t)((g_clock_ms - 100000) / 1000);
    if (t) *t = v;
    return v;
}
void host_clock_advance(uint32_t ms) { g_clock_ms += ms; }
void host_set_wall_time(time_t t) { g_wall_base = t - (time_t)((g_clock_ms - 100000) / 1000); }

HardwareSerial Serial;

// ---------------------------------------------------------------- display / touch
uint16_t g_host_fb[HOST_W * HOST_H];
bool g_host_touch_down = false;
int g_host_touch_x = 0, g_host_touch_y = 0;
int g_host_brightness = 0;

// ---------------------------------------------------------------- heap accounting
static std::atomic<size_t> g_blocks{0}, g_bytes{0}, g_peak{0};
static void account(ssize_t dblocks, ssize_t dbytes)
{
    g_blocks += dblocks;
    size_t now = (g_bytes += dbytes);
    size_t peak = g_peak.load();
    while (now > peak && !g_peak.compare_exchange_weak(peak, now)) {}
}
extern "C" {
void *heap_caps_malloc(size_t size, uint32_t)
{
    void *p = malloc(size);
    if (p) account(1, (ssize_t)malloc_size(p));
    return p;
}
void *heap_caps_calloc(size_t n, size_t size, uint32_t)
{
    void *p = calloc(n, size);
    if (p) account(1, (ssize_t)malloc_size(p));
    return p;
}
void *heap_caps_realloc(void *ptr, size_t size, uint32_t)
{
    ssize_t old = ptr ? (ssize_t)malloc_size(ptr) : 0;
    void *p = realloc(ptr, size);
    if (size == 0) { if (ptr) account(-1, -old); return nullptr; }
    if (!p) return nullptr;
    account(ptr ? 0 : 1, (ssize_t)malloc_size(p) - old);
    return p;
}
void heap_caps_free(void *ptr)
{
    if (!ptr) return;
    account(-1, -(ssize_t)malloc_size(ptr));
    free(ptr);
}
size_t heap_caps_get_free_size(uint32_t) { return 4u << 20; }
size_t heap_caps_get_largest_free_block(uint32_t) { return 2u << 20; }
size_t heap_caps_get_minimum_free_size(uint32_t) { return 2u << 20; }
size_t host_heap_live_blocks(void) { return g_blocks; }
size_t host_heap_live_bytes(void) { return g_bytes; }
size_t host_heap_peak_bytes(void) { return g_peak; }
}

// ---------------------------------------------------------------- Wi-Fi
bool g_host_wifi_connected = true;
int g_host_scan_count = 10;
WiFiClass WiFi;

static const char *SCAN[] = {
    "FRITZ!Box 7590 JK",
    "Vodafone-A1B2C3",
    "FRITZ!Box 7590 JK",                           // same network seen on a second access point
    "Familie-Mueller-Schmidt-Gaeste-5",            // 32 characters, the maximum
    "Café Müller Straße",  // non-ASCII
    "",                                            // hidden network
    "Telekom_FON",
    "WWWWWWWWWWWWWWWWWWWWWWWWWWWWWWWW",            // 32 wide characters
    "WLAN-Nachbar #1 €",
    "o2-WLAN42",
};
int WiFiClass::scanNetworks(bool, bool, bool, uint32_t) { delay(2000); return g_host_scan_count; }   // a scan takes a few seconds
wl_status_t WiFiClass::begin(const char *, const char *, int32_t, const uint8_t *, bool) { return WL_DISCONNECTED; }
bool WiFiClass::disconnect(bool, bool) { return true; }
void WiFiClass::scanDelete() {}
int32_t WiFiClass::channel(int i) { return 1 + (i * 5) % 13; }
const uint8_t *WiFiClass::BSSID(int i)
{
    static uint8_t b[6];
    for (int k = 0; k < 6; k++) b[k] = (uint8_t)(0x10 * k + i);
    return b;
}
String WiFiClass::SSID(int i)
{
    if (i < 0) return String();
    if (i < (int)(sizeof(SCAN) / sizeof(SCAN[0]))) return String(SCAN[i]);
    char b[32];
    snprintf(b, sizeof(b), "Nachbar-WLAN-%02d", i);
    return String(b);
}
int32_t WiFiClass::RSSI(int i) { return -40 - 4 * i; }

// ---------------------------------------------------------------- settings
Settings cfg;
Preferences prefs;
void config_defaults()
{
    cfg = Settings();
    cfg.chartTf = 1;
    cfg.priceSeparator = true;     // what create_missing_keys() writes on a fresh device
    cfg.debugEnabled = false;
}

// ---------------------------------------------------------------- shared live data
LiveData live;
std::vector<NewsItem> g_news;
std::vector<BitaxeDevice> g_bitaxe;
SemaphoreHandle_t g_data_mutex = xSemaphoreCreateMutex();
SemaphoreHandle_t g_http_mutex = xSemaphoreCreateMutex();
void net_init() {}
void net_parse_bitaxe_ips() {}
void net_apply_page_flags() { g_host_stat.page_flags_calls++; }
void net_update_chart_change(uint32_t) {}
void net_stack_report(String &) {}
bool net_connected() { return g_host_wifi_connected; }

// ---------------------------------------------------------------- hardware
HostStats g_host_stat;
bool hw_is_ready() { return true; }
void hw_init() {}
void hw_settings_changed() {}
void buzzer_click() { g_host_stat.clicks++; }
void buzzer_alert() { g_host_stat.alerts++; }
void led_pulse(uint8_t, uint8_t, uint8_t) { g_host_stat.led_calls++; }
void led_solid(uint8_t r, uint8_t g, uint8_t b) { g_host_stat.led_calls++; g_host_stat.led_rgb = (r << 16) | (g << 8) | b; }
void led_off() { g_host_stat.led_calls++; g_host_stat.led_rgb = 0; }
void led_save() {}
void led_restore() {}
void led_tick() {}

// ---------------------------------------------------------------- OTA / Wi-Fi link
OtaState ota;
void ota_check() {}
bool ota_version_newer(const String &, const String &) { return true; }
void ota_start_task() { g_host_stat.ota_starts++; }
int ota_progress_percent()
{
    if (ota.total <= 0) return 0;
    long long p = (long long)ota.downloaded * 100 / ota.total;
    return p < 0 ? 0 : p > 100 ? 100 : (int)p;
}

volatile bool g_wifi_ok = false;
volatile bool g_wifi_connecting = false;
volatile bool g_wifi_reconnecting = false;
volatile bool g_wifi_silent_drop = false;
String g_ssid, g_pass;
bool wifi_load_credentials() { return false; }
void wifi_save_credentials() {}
void wifi_start_events() {}
void wifi_start_reconnect_task(bool) { g_host_stat.reconnects++; g_wifi_connecting = false; }
void wifi_start_watchdog() {}

// ---------------------------------------------------------------- diagnostics
void diag_init() {}
void diag_stage(const char *) {}
void diag_printf(const char *fmt, ...)
{
    if (!getenv("HOST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}
String diag_report() { return String(); }
