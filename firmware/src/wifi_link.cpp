#include <WiFi.h>
#include <Preferences.h>
#include "wifi_link.h"
#include "config.h"
#include "net.h"
#include "web.h"
#include "ui.h"
#include "wifi_scan.h"

volatile bool g_wifi_ok = false;
volatile bool g_wifi_connecting = false;
volatile bool g_wifi_reconnecting = false;
volatile bool g_wifi_silent_drop = false;
String g_ssid, g_pass;

static uint32_t s_watchdog_kicks = 0;

// ---------------------------------------------------------------- credentials

bool wifi_load_credentials()
{
    Preferences p;
    p.begin(NVS_NAMESPACE, true);
    if (!p.isKey("ssid") || !p.isKey("password")) {
        p.end();
        return false;
    }
    String ssid = p.getString("ssid", "");
    String pass = p.getString("password", "");
    p.end();
    if (ssid.isEmpty()) return false;
    g_ssid = ssid;
    g_pass = pass;
    return true;
}

void wifi_save_credentials()
{
    Preferences p;
    p.begin(NVS_NAMESPACE, false);
    p.putString("ssid", g_ssid);
    p.putString("password", g_pass);
    p.end();
    DBG("wifi: credentials saved\n");
}

// ---------------------------------------------------------------- events

// A lost link starts the reconnect task (unless a restart is pending or one is already
// running); getting an address clears the reconnect state.
static void on_wifi_event(arduino_event_id_t id, arduino_event_info_t)
{
    if (id == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        WiFi.setAutoReconnect(false);
        g_wifi_reconnecting = false;
        g_wifi_silent_drop = false;
        ui_notify_wifi_restored();
        return;
    }
    if (id != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
    if (g_restart_requested || g_wifi_reconnecting) return;
    bool silent = g_wifi_silent_drop;
    g_wifi_reconnecting = true;
    if (!silent) ui_notify_wifi_lost();
    wifi_start_reconnect_task(false);
}

void wifi_start_events() { WiFi.onEvent(on_wifi_event); }

// ---------------------------------------------------------------- reconnect task

static const uint32_t CONNECT_TIMEOUT_MS = 20000;

// Scans, picks the strongest access point with the saved SSID and connects to its channel
// and BSSID (a plain begin() when the scan finds nothing). Waits up to 20 s per attempt.
// The initial attempt after boot runs once; later attempts repeat every 2 s.
static void reconnect_task(void *param)
{
    bool initial = param != nullptr;
    DBG("wifi: reconnecting\n");
    for (int attempt = 1;; attempt++) {
        DBG("wifi: attempt %d\n", attempt);
        WiFi.disconnect(true, false);
        delay(100);

        WifiScan scan = wifi_scan(2);
        const WifiAp *best = wifi_find_ap(scan.aps, g_ssid);
        DBG("wifi: scan found %d access points (code %d, %d tries)\n", (int)scan.aps.size(), scan.code, scan.attempts);
        if (best) {
            DBG("wifi: strongest AP for %s on channel %d (%d dBm)\n", g_ssid.c_str(), best->channel, best->rssi);
            uint8_t bssid[6];
            memcpy(bssid, best->bssid, 6);
            WiFi.begin(g_ssid.c_str(), g_pass.c_str(), best->channel, bssid, true);
        } else {
            DBG("wifi: SSID not in scan, plain connect\n");
            WiFi.begin(g_ssid.c_str(), g_pass.c_str());
        }

        bool ok = false;
        for (uint32_t t0 = millis(); millis() - t0 < CONNECT_TIMEOUT_MS; delay(250)) {
            if (WiFi.status() == WL_CONNECTED) {
                WiFi.setAutoReconnect(false);
                ok = true;
                break;
            }
        }
        if (ok) {
            DBG("wifi: connected\n");
            g_wifi_ok = true;
            break;
        }
        ui_notify_wifi_retry();
        if (initial) {
            DBG("wifi: initial connect failed\n");
            break;
        }
        delay(2000);
    }
    g_wifi_connecting = false;
    vTaskDelete(nullptr);
}

void wifi_start_reconnect_task(bool initial)
{
    xTaskCreatePinnedToCore(reconnect_task, "WiFiReconnect", 0x2000,
                            initial ? (void *)1 : nullptr, 1, nullptr, 1);
}

// ---------------------------------------------------------------- watchdog

static const uint32_t WATCHDOG_PERIOD_MS = 5000;
static const uint32_t STALL_LIMIT_MS     = 180000;   // no successful fetch for 3 min
static const uint32_t KICK_INTERVAL_MS   = 30000;

// When data pages are enabled but nothing has been fetched for a while, drop the link
// silently so the reconnect task rebuilds it.
static void watchdog_task(void *)
{
    uint32_t last_kick = 0;
    for (;;) {
        vTaskDelay(WATCHDOG_PERIOD_MS);
        bool needs_data = cfg.pg[PAGE_PRICE] || cfg.pg[PAGE_CHART] || cfg.pg[PAGE_BLOCK] ||
                          cfg.pg[PAGE_BITAXE] || cfg.pg[PAGE_FEES];
        if (!needs_data || live.lastFetchMs == 0) continue;

        uint32_t now = millis();
        uint32_t age = now - live.lastFetchMs;
        if (age < STALL_LIMIT_MS) continue;
        if (last_kick != 0 && now - last_kick < KICK_INTERVAL_MS) continue;
        if (g_wifi_reconnecting) continue;

        s_watchdog_kicks++;
        DBG("watchdog: no fetch for %lu ms, dropping link (kick %lu)\n",
            (unsigned long)age, (unsigned long)s_watchdog_kicks);
        g_wifi_silent_drop = true;
        WiFi.disconnect(true, false);
        last_kick = now;
    }
}

void wifi_start_watchdog()
{
    xTaskCreatePinnedToCore(watchdog_task, "Watchdog", 0x1000, nullptr, 1, nullptr, 1);
}
