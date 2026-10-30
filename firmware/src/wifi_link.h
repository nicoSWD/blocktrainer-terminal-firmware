// Wi-Fi credentials, reconnect task and the fetch watchdog.
#pragma once
#include <Arduino.h>

extern volatile bool g_wifi_ok;           // last connect attempt succeeded
extern volatile bool g_wifi_connecting;   // a connect or setup-wizard attempt is running
extern volatile bool g_wifi_reconnecting; // reconnect task active (the UI shows a banner)
extern volatile bool g_wifi_silent_drop;  // the watchdog dropped the link on purpose

extern String g_ssid;
extern String g_pass;

bool wifi_load_credentials(); // NVS keys "ssid" / "password"
void wifi_save_credentials();
void wifi_start_events();                     // registers the disconnect / got-IP handler
void wifi_start_reconnect_task(bool initial); // initial: one attempt, then hand over to the setup wizard
void wifi_start_watchdog();                   // reconnects when fetches have stalled
