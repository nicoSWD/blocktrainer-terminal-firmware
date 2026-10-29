#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <vector>
#include <esp_heap_caps.h>
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#include "config.h"
#include "bootguard.h"
#include "net.h"
#include "hw.h"
#include "ui.h"
#include "wifi_link.h"
#include "wifi_portal.h"
#include "ota.h"
#include "web.h"

static bool s_have_display = false;

// Connection attempts (about 25 s each, alternating between the known networks) before the setup
// screen appears.
// Generous on purpose: after a power cut the router often boots slower than the device.
static const int MAX_ATTEMPTS = 6;
static const uint32_t OTA_PROMPT_TIMEOUT_MS = 120000;     // the update prompt skips itself after this
static const uint32_t WIZARD_TIMEOUT_MS = 300000;         // then the setup access point takes over

// Without a display nobody calls led_tick() from the UI task, so run it here.
static void led_only_task(void *)
{
    for (;;) {
        led_tick();
        vTaskDelay(10);
    }
}

struct WifiCandidate {
    String ssid, pass;
    bool persist;           // store in NVS after a successful connection
};

static bool try_candidate(const WifiCandidate &c)
{
    g_ssid = c.ssid;
    g_pass = c.pass;
    ui_show(UI_WIFI_CONNECTING);
    g_wifi_ok = false;
    g_wifi_connecting = true;
    wifi_start_reconnect_task(true);
    while (g_wifi_connecting) { boot_guard_feed(); delay(100); }
    if (!g_wifi_ok) return false;
    if (c.persist) wifi_save_credentials();
    ui_show(UI_WIFI_CONNECTED);
    g_wifi_reconnecting = false;
    return true;
}

// Tries the built-in network (include/secrets.h, if present) and the stored credentials in turn.
// When neither works the setup wizard runs on the screen, or the setup access point when there
// is no usable screen.
static void connect_wifi()
{
    std::vector<WifiCandidate> candidates;
#ifdef BUILTIN_WIFI_SSID
    candidates.push_back({ BUILTIN_WIFI_SSID, BUILTIN_WIFI_PASS, false });
#endif
    if (wifi_load_credentials()) candidates.push_back({ g_ssid, g_pass, true });

    for (int attempt = 0; attempt < MAX_ATTEMPTS && !candidates.empty(); attempt++) {
        const WifiCandidate &c = candidates[attempt % candidates.size()];
        DBG("wifi: attempt %d of %d (%s)\n", attempt + 1, MAX_ATTEMPTS, c.ssid.c_str());
        if (try_candidate(c)) return;
        ui_show(UI_WIFI_FAILED);
        g_wifi_reconnecting = false;
        delay(2000);
    }

    for (;;) {
        if (!s_have_display) wifi_portal_run();     // restarts the device once credentials work

        DBG("wifi: starting setup wizard\n");
        ui_show(UI_WELCOME);
        delay(2500);
        boot_guard_feed();
        g_wifi_ok = false;
        g_wifi_connecting = true;
        ui_show(UI_WIFI_WIZARD);
        uint32_t wizard_start = millis();
        while (g_wifi_connecting) {
            boot_guard_feed();
            delay(1);
            // a screen that cannot be used must not be the only way to set up Wi-Fi
            if (millis() - wizard_start > WIZARD_TIMEOUT_MS) wifi_portal_run();
        }
        if (g_wifi_ok) {
            wifi_save_credentials();
            ui_show(UI_WIFI_CONNECTED);
            g_wifi_reconnecting = false;
            return;
        }
        ui_show(UI_WIFI_FAILED);
        g_wifi_reconnecting = false;
        delay(8000);
        boot_guard_feed();
    }
}

// A finished install runs the new image after a moment, whoever started it.
static void restart_after_update()
{
    delay(4000);
    g_restart_requested = true;
    ESP.restart();
}

void setup()
{
    Serial.begin(115200);
    delay(500);
    diag_init();
    boot_guard_begin();
    diag_stage("setup");

    net_init();
    config_defaults();
    config_load();
    diag_stage("config loaded");
    DBG("firmware %s\n", FIRMWARE_VERSION);

    // Safe mode (several crashes in a row) skips the display and the sound/LED drivers, so
    // that Wi-Fi, the web server and OTA still work and a fixed image can be installed.
    if (!boot_guard_safe_mode()) {
        diag_stage("ui_init");
        s_have_display = ui_init();
        diag_stage("hw_init");
        hw_init();
        diag_stage("hardware ready");
        led_pulse(255, 120, 0);                 // orange while booting
        if (s_have_display) ui_start();
        else xTaskCreatePinnedToCore(led_only_task, "led", 0x1000, nullptr, 1, nullptr, 1);
    } else {
        Serial.println("SAFE MODE: no display, no sound/LED. /safe?exit=1 returns to normal mode.");
    }

    wifi_start_watchdog();
    WiFi.mode(WIFI_STA);
    diag_stage("wifi");
    connect_wifi();
    diag_stage("wifi connected");
    web_begin();
    diag_stage("web server up");                                // before the update prompt: /safe, /ota and /update work from here on

    if (cfg.checkForUpdates || boot_guard_safe_mode()) {
        diag_stage("ota check");
        ota_check();
        if (ota.stage == OTA_AVAILABLE) {
            if (s_have_display) {
                ui_show(UI_OTA_AVAILABLE);
            } else {
                DBG("ota: headless, installing without a prompt\n");
                ota.stage = OTA_CONFIRMED;
                ota_start_task();
            }
        }
    }
    // The update prompt holds startup until the user decides. A finished install restarts the
    // device into the new image after a moment.
    uint32_t prompt_start = millis();
    while (ota.active && ota.stage != OTA_ERROR) {
        boot_guard_feed();
        web_loop();                             // keeps the web server reachable while the prompt is up
        if (ota.stage == OTA_DONE) restart_after_update();
        // an unanswered prompt (blank panel, no touch) must not hold the device: skip the update
        if (ota.stage == OTA_AVAILABLE && millis() - prompt_start > OTA_PROMPT_TIMEOUT_MS) {
            DBG("ota: prompt timed out, skipping\n");
            ota.stage = OTA_IDLE;
            ota.active = false;
        }
        delay(2);
    }

    diag_stage("ntp");
    ui_setup_done();
    configTzTime(cfg.timezoneDef.c_str(), "pool.ntp.org");
    struct tm t;
    if (!getLocalTime(&t, 5000)) DBG("ntp: no time received\n");

    net_apply_page_flags();
    diag_stage("waiting for first price");

    // give the first price up to 10 s before showing the first page
    uint32_t start = millis();
    while (true) {
        {
            DataLock lock;
            if (live.price[CUR_USD] >= 0.01f || live.price[CUR_EUR] >= 0.01f) break;
        }
        if (millis() - start > 10000) break;
        delay(100);
    }

    diag_stage("first page");
    ui_show_first_page();
    ui_settings_changed();
    wifi_start_events();
    diag_stage("ready");
    DBG("setup complete, %d MHz, heap free %u (internal %u, largest block %u), psram %u\n", (int)getCpuFrequencyMhz(),
        (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
        (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL), (unsigned)ESP.getFreePsram());
}

void loop()
{
    delay(2);
    boot_guard_tick();
    if (ota.stage == OTA_DONE) restart_after_update();
    if (WiFi.status() == WL_CONNECTED) web_loop();
}
