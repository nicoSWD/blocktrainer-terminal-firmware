// LVGL user interface on the 480x320 touch panel.
//
// All LVGL calls happen on the "lvgl" task. The functions below are safe to call from any task:
// they queue the work for that task.
#pragma once
#include <Arduino.h>

// Full-screen status screens driven from setup() and the Wi-Fi/OTA code.
enum UiScreen {
    UI_WELCOME,          // "WLAN Setup wird gestartet..."
    UI_WIFI_CONNECTING,
    UI_WIFI_CONNECTED,
    UI_WIFI_FAILED,
    UI_WIFI_LOST,
    UI_WIFI_WIZARD,      // network list and password entry
    UI_OTA_AVAILABLE,    // "NEUE FIRMWARE VERFUEGBAR"
    UI_OTA_PROGRESS,
    UI_OTA_DONE,
};

bool ui_is_ready();                     // the panel and LVGL are up
void ui_stack_report(String &out);      // stack headroom of the lvgl task, for /diag
bool ui_init();                         // panel, LVGL, layers; false when the panel could not be set up
void ui_start();                        // starts the "lvgl" task
void ui_show(UiScreen screen);
void ui_notify_wifi_lost();
void ui_notify_wifi_restored();
void ui_notify_wifi_retry();
void ui_setup_done();                   // closes the status screens before the first page
void ui_show_first_page();
void ui_settings_changed();             // web form saved: apply brightness and rotation
