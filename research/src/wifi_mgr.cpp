// wifi.cpp: verified against FUN_420249c0/24900 (credentials), FUN_42024830
// (event handler), FUN_42024450 (WiFiReconnect task) and FUN_420242cc (Watchdog).

#include <WiFi.h>
#include <Preferences.h>
#include "wifi_mgr.h"
#include "config.h"
#include "data.h"
#include "web.h"
#include "display.h"

volatile bool g_wifi_ok = false;
volatile bool g_wifi_connecting = false;
volatile bool g_wifi_reconnecting = false;
volatile bool g_wifi_silent_drop = false;
String g_ssid, g_pass;

static uint32_t s_reconnect_count = 0;        // 0x3fc9e6f8

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
    DBG("WLAN-Daten gespeichert!\n");
}

// ---------------------------------------------------------------- events
// FUN_42024830. Event 5 = STA_DISCONNECTED: start the reconnect task unless a
// restart was requested or a reconnect is already running. Event 7 = STA_GOT_IP:
// clear the reconnect flags.
static void on_wifi_event(arduino_event_id_t id, arduino_event_info_t)
{
    if (id == ARDUINO_EVENT_WIFI_STA_GOT_IP) {
        WiFi.setAutoReconnect(false);
        g_wifi_reconnecting = false;
        g_wifi_silent_drop = false;
        ui_async_notify_wifi_restored();
        return;
    }
    if (id != ARDUINO_EVENT_WIFI_STA_DISCONNECTED) return;
    if (g_restart_requested) return;
    if (g_wifi_reconnecting) return;
    bool silent = g_wifi_silent_drop;
    g_wifi_reconnecting = true;
    if (!silent) ui_async_notify_wifi_lost();
    wifi_start_reconnect_task(false);
}

void wifi_start_events() { WiFi.onEvent(on_wifi_event); }

// ---------------------------------------------------------------- WiFiReconnect
// Scan, pick the strongest AP with the saved SSID, connect to its channel/BSSID
// (falls back to a plain begin() if the scan finds nothing), wait up to 20 s.
// `initial` (task parameter) = first connect after boot: one attempt, then the
// caller falls through to the setup wizard. Otherwise retry every 2 s forever.
static void wifi_reconnect_task(void *param)
{
    bool initial = param != nullptr;
    DBG("📡 Starte WLAN-Reconnect...\n");
    int attempt = 0;
    for (;;) {
        attempt++;
        DBG("\n📡 Reconnect-Versuch %d\n", attempt);
        WiFi.disconnect(true, false);
        delay(100);
        int n = WiFi.scanNetworks(false, false, false, 300);
        int best_rssi = -100, best_ch = 0;
        uint8_t best_bssid[6] = {0};
        bool found = false;
        for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i) == g_ssid && WiFi.RSSI(i) > best_rssi) {
                best_rssi = WiFi.RSSI(i);
                best_ch = WiFi.channel(i);
                memcpy(best_bssid, WiFi.BSSID(i), 6);
                found = true;
            }
        }
        if (!found) {
            DBG("❌ Kein passender Access Point gefunden - versuche Standardverbindung.\n");
            WiFi.begin(g_ssid.c_str(), g_pass.c_str());
        } else {
            char mac[18];
            snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X", best_bssid[0], best_bssid[1],
                     best_bssid[2], best_bssid[3], best_bssid[4], best_bssid[5]);
            DBG("🔄 Verbinde mit stärkstem AP (%s), Kanal %d\n", g_ssid.c_str(), best_ch);
            DBG("📡 AP-MAC: %s\n", mac);
            WiFi.begin(g_ssid.c_str(), g_pass.c_str(), best_ch, best_bssid, true);
        }
        DBG("🔄 Verbindungsversuch: ");
        uint32_t t0 = millis();
        int dots = 0;
        bool ok = false;
        while (millis() - t0 < 20000) {
            if (WiFi.status() == WL_CONNECTED) {
                WiFi.setAutoReconnect(false);
                ok = true;
                break;
            }
            DBG(".");
            if (++dots % 10 == 0) DBG(" ");
            delay(250);
        }
        if (cfg.debugEnabled) Serial.println();
        if (ok) {
            DBG("✅ WLAN erneut verbunden!\n");
            g_wifi_ok = true;
            break;
        }
        DBG("⏳ Kein Erfolg.\n");
        ui_async_notify_wifi_retry();
        if (initial) {
            DBG("🚫 Initiale Verbindung fehlgeschlagen - wechsle zur Setup-Seite.\n");
            break;
        }
        DBG("🔁 Neuer Versuch in 2 Sekunden...\n");
        delay(2000);
    }
    g_wifi_connecting = false;
    vTaskDelete(nullptr);
}

void wifi_start_reconnect_task(bool initial)
{
    xTaskCreatePinnedToCore(wifi_reconnect_task, "WiFiReconnect", 0x2000, initial ? (void *)1 : nullptr, 1, nullptr, 1);
}

// ---------------------------------------------------------------- Watchdog
// Every 5 s. Only active if some data page needs fetches (price, chart, block,
// bitaxe or fees enabled). If the last successful fetch (live.lastFetchMs) is
// older than 180000 ms (0x2bf21 is the "ok" threshold), and at most every 30 s,
// force a silent WiFi disconnect so the reconnect task rebuilds the link.
static void watchdog_task(void *)
{
    uint32_t last_ok_log = 0, last_kick = 0;
    for (;;) {
        uint32_t now = millis();
        bool needs_data = cfg.pg[PAGE_PRICE] || cfg.pg[PAGE_CHART] || cfg.pg[PAGE_BLOCK] ||
                          cfg.pg[PAGE_BITAXE] || cfg.pg[PAGE_FEES];
        if (!needs_data) { vTaskDelay(5000); continue; }
        if (live.lastFetchMs == 0) { vTaskDelay(5000); continue; }

        uint32_t age = now - live.lastFetchMs;
        if (age < 0x2bf21) {
            if (now - last_ok_log > 60000) {
                DBG("✅ Watchdog: ok (letzter Fetch vor %lu ms) | reconnectCount = %lu\n",
                    (unsigned long)age, (unsigned long)s_reconnect_count);
                last_ok_log = now;
            }
        } else if (last_kick == 0 || now - last_kick > 29999) {
            if (!g_wifi_reconnecting) {
                s_reconnect_count++;
                DBG("🛑 Watchdog: keine Fetches seit %lu ms → trigger DISCONNECT (silent) | reconnectCount = %lu\n",
                    (unsigned long)age, (unsigned long)s_reconnect_count);
                g_wifi_silent_drop = true;
                WiFi.disconnect(true, false);
                last_kick = now;
            }
        }
        vTaskDelay(5000);
    }
}

void watchdog_start()
{
    xTaskCreatePinnedToCore(watchdog_task, "Watchdog", 0x1000, nullptr, 1, nullptr, 1);
}
