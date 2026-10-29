// display.h: LVGL 8 UI on a LovyanGFX panel driver.
//
// The firmware does NOT draw with LovyanGFX calls. LovyanGFX only provides the
// 8-bit parallel panel driver (Bus_Parallel8) and the touch controller; every
// screen is an LVGL object tree. Many Ghidra symbols named "lgfx_*" in the saved
// decompilations are in fact LVGL functions, see MODULES.md for the mapping.
#pragma once
#include <Arduino.h>
#include <lvgl.h>

// Screens driven from setup() (shown via lv_async_call in the original).
enum UiScreen {
    UI_WELCOME,          // FUN_4200b918 "Willkommensseite"
    UI_WIFI_CONNECTING,  // FUN_42030a78 "Verbindung wird hergestellt. \nSSID: #FFA500 %s#"
    UI_WIFI_CONNECTED,   // FUN_42030ba8 "Verbindung erfolgreich hergestellt.\nSSID: #FFA500 %s#"
    UI_WIFI_FAILED,      // FUN_42030f60 "Verbindung fehlgeschlagen!\nSSID: #FFA500 %s#"
    UI_WIFI_LOST,        // FUN_42031090 "Verbindung unterbrochen!"
    UI_WIFI_WIZARD,      // FUN_42030994 "WLAN Setup wird gestartet..." + scan list + password entry
    UI_OTA_AVAILABLE,    // FUN_42031224 "NEUE FIRMWARE VERFÜGBAR"
    UI_OTA_PROGRESS,     // FUN_42031500 "UPDATE WIRD INSTALLIERT"
    UI_OTA_DONE,         // FUN_420316e0 "UPDATE ERFOLGREICH"
};

void display_init();                         // FUN_4200b9c8
void display_set_brightness(int percent);    // DAT_3fc9bda8 + panel backlight
void ui_task(void *);                        // FUN_4200c404 ("lvgl" task)
void ui_show(UiScreen s);
void ui_show_price_alert(bool above, int level, const String &currency);   // FUN_4202ce74

// page handling (FUN_4200c210 / FUN_4200bc2c / FUN_4200b77c)
void ui_request_page(int page, bool animate);   // queues a switch for ui_task
int  ui_default_page();                         // first enabled page honouring pageOrder
void ui_set_current_page(int page);
int  ui_current_page();

// called from other tasks; all of these hop into the LVGL thread
void ui_async_notify_wifi_lost();
void ui_async_notify_wifi_restored();
void ui_async_notify_wifi_retry();
void ui_async_setup_done();
void ui_async_show_first_page();
void ui_start_clock_timer(uint32_t period_ms);
void ui_reset_chart_cache();                 // FUN_420348ec

// page record, one row of the table at 0x3c21ae88 (stride 0x20)
struct PageDef {
    int idx;                      // PageId
    bool menu_only;               // 'flag': 1 only for RECHNER
    const char *name;             // "PREIS", "CHART", ...
    const char *pref_key;         // "pg_price", ...
    int key_code;                 // 0x70.. (function key code used by the web UI order list)
    bool (*available)();          // enable predicate (BITAXE needs a configured device)
    void (*build)(lv_obj_t *);    // FUN_xxxx at table +0x18, creates the page
    void (*destroy)();            // FUN_xxxx at table +0x1c
};
extern const PageDef PAGES[];
