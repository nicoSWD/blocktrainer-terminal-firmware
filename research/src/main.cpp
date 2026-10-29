// main.cpp: setup()/loop(), reconstructed from loopTask -> FUN_4200bcf0 (setup)
// and FUN_4200c8d4 (loop). Every Serial print is gated on cfg.debugEnabled.

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "config.h"
#include "data.h"
#include "display.h"
#include "ota.h"
#include "web.h"
#include "wifi_mgr.h"
#include "hw.h"

static unsigned long s_last_handle_ms = 0;     // 0x3fc9c0a8

void setup()
{
    Serial.begin(921600);                       // 0xe1000
    delay(5000);
    DBG("Start Setup\n");

    g_data_mutex = xSemaphoreCreateMutex();     // 0x3fc9bb50
    g_http_mutex = xSemaphoreCreateMutex();     // 0x3fc9bb54

    config_defaults();                          // FUN_4200a598
    config_load();                              // FUN_42008a84
    display_init();                             // FUN_4200b9c8: LGFX + LVGL + layers + status bar
    ui_reset_chart_cache();                     // FUN_420348ec
    led_init();                                 // FUN_42033e90
    buzzer_init();                              // FUN_42032fb8
    led_set(255, 120, 0);                       // orange while booting (FUN_4203408c)

    // FUN_4200c404: the UI coordinator ("lvgl" task, stack 0x2000, prio 2, core 1)
    xTaskCreatePinnedToCore(ui_task, "lvgl", 0x2000, nullptr, 2, nullptr, 1);
    watchdog_start();                           // FUN_42024424
    WiFi.mode(WIFI_STA);                        // FUN_4206db98(1)

    bool have_creds = wifi_load_credentials();  // FUN_420249c0
    for (;;) {
        if (!g_wifi_ok) {
            if (!have_creds) {
                DBG("Willkommensseite wird aufgerufen\n");
                ui_show(UI_WELCOME);
                delay(4000);
            } else {
                // stored credentials: one WiFiReconnect attempt (initial = true)
                ui_show(UI_WIFI_CONNECTING);
                DBG("Gespeicherte WLAN-Daten gefunden, verbinde...\n");
                g_wifi_ok = false;
                g_wifi_connecting = true;
                wifi_start_reconnect_task(true);
                while (g_wifi_connecting) delay(100);
                if (g_wifi_ok) {
                    DBG("Verbindung erfolgreich\n");
                    wifi_save_credentials();
                    ui_show(UI_WIFI_CONNECTED);
                    g_wifi_reconnecting = false;
                }
                else {
                    DBG("Verbindung fehlgeschlagen\n");
                    ui_show(UI_WIFI_FAILED);
                    delay(4000);
                    g_wifi_reconnecting = false;
                }
            }
        }
        if (!g_wifi_ok) {
            // setup wizard (WLAN Setup wird gestartet..., WiFiScanTask, password entry)
            DBG("WLAN Setup wird gestartet...\n");
            g_wifi_ok = false;
            g_wifi_connecting = true;
            ui_show(UI_WIFI_WIZARD);
            while (g_wifi_connecting) delay(1);
            if (g_wifi_ok) {
                wifi_save_credentials();
                ui_show(UI_WIFI_CONNECTED);
                g_wifi_reconnecting = false;
            } else {
                ui_show(UI_WIFI_FAILED);
                g_wifi_reconnecting = false;
                delay(8000);
                have_creds = false;
                continue;
            }
        }
        break;
    }

    if (cfg.checkForUpdates) {
        ota_check();                            // FUN_4202233c
        if (ota.stage == OTA_AVAILABLE) ui_show(UI_OTA_AVAILABLE);
    }
    // the OTA prompt blocks startup while the user decides (SKIP clears `active`; an install
    // keeps it set until the user restarts the device)
    while (ota.active && ota.stage != OTA_ERROR) delay(10);

    ui_async_setup_done();                      // FUN_4200b928
    DBG("\nZeitzone geladen: (%s)\n", cfg.timezoneLabel.c_str());
    configTzTime(cfg.timezoneDef.c_str(), "pool.ntp.org");    // FUN_4207a11c
    struct tm t;
    if (!getLocalTime(&t, 5000)) DBG("Fehler: Keine Zeit von NTP-Server erhalten.\n");
    else DBG("NTP-Zeit erfolgreich abgerufen!\n");

    ui_start_clock_timer(1000);                 // FUN_4205874c(0x4200bba4, 1000)
    web_begin();                                // FUN_42039ee8
    tasks_apply_page_flags();                   // FUN_4200b888

    // wait for the first price (up to 10 s) before showing the first page
    uint32_t t0 = millis();
    while (live.price[0] < 0.01f && millis() - t0 < 10000) delay(100);

    int first = ui_default_page();              // FUN_4200b77c
    ui_set_current_page(first);
    ui_async_show_first_page();                 // FUN_4200bc60
    DBG("\nGespeicherte Helligkeit: (%d)\n", cfg.brightness);
    display_set_brightness(cfg.brightness);
    wifi_start_events();                        // FUN_42024b18: WiFi.onEvent(handler), only after setup finished
    DBG("%s arbeitet mit %d MHz\n\n", "ESP32-S3", (int)getCpuFrequencyMhz());
    DBG("############# Setup erfolgreich abgeschlossen #############\n");
}

// FUN_4200c8d4: delay(2); service the web server and warn if a request is slow.
void loop()
{
    delay(2);
    uint32_t t0 = millis();
    if (WiFi.status() == WL_CONNECTED) {
        web_loop();
        uint32_t dt = millis() - t0;
        if (dt > 0x82)
            DBG("⚠️ [WARNUNG] %s dauerte %lu ms (Limit: %lu ms)\n", "server.handleClient", (unsigned long)dt, 130UL);
        if (s_last_handle_ms != 0 && millis() - s_last_handle_ms > 0x82)
            DBG("✅ loop() laufzeit %lums\n", (unsigned long)(millis() - s_last_handle_ms));
        s_last_handle_ms = millis();
    }
}
