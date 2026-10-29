// wifi.h: credentials, reconnect task, WiFi events and the fetch watchdog.
#pragma once
#include <Arduino.h>

// State flags shared with the UI task (all live in DRAM in the original).
extern volatile bool g_wifi_ok;            // 0x3fc9b84c  last connect attempt succeeded
extern volatile bool g_wifi_connecting;    // 0x3fc9b84d  a connect/wizard attempt is running
extern volatile bool g_wifi_reconnecting;  // 0x3fc9b84e  WiFiReconnect task active (UI shows a banner)
extern volatile bool g_wifi_silent_drop;   // 0x3fc9b84f  watchdog-triggered disconnect

extern String g_ssid;                      // 0x3fc9b82c
extern String g_pass;                      // 0x3fc9b83c

bool wifi_load_credentials();              // FUN_420249c0: NVS "ssid"/"password" in namespace BTCLOCK
void wifi_save_credentials();              // FUN_42024900: prints "WLAN-Daten gespeichert!"
void wifi_start_events();                  // registers FUN_42024830
void wifi_start_reconnect_task(bool initial);   // "WiFiReconnect", stack 0x2000, prio 1, core 1
void watchdog_start();                     // FUN_42024424: "Watchdog", stack 0x1000, prio 1, core 1
