#include <esp_system.h>
#include <Preferences.h>
#include "bootguard.h"
#include "config.h"
#include "diag.h"

// Own NVS namespace: the stock firmware's "BTCLOCK" namespace stays untouched.
static const char *NS = "btfw";
static const int SAFE_MODE_CRASHES = 3;
// Long on purpose: a crash in a page update, the menu or a chart reload happens minutes after boot.
static const uint32_t STABLE_AFTER_MS = 300000;

static int s_crashes = 0;
static bool s_safe = false;
static bool s_stable = false;
static volatile uint32_t s_last_feed_ms = 0;

// A hang (a call that never returns) neither panics nor resets, and without USB it would leave
// the device unreachable. This task restarts it when nothing has made progress for a while and
// counts the hang like a crash, so repeated hangs end up in safe mode.
static const uint32_t HANG_LIMIT_MS = 90000;

static void watchdog_task(void *)
{
    for (;;) {
        vTaskDelay(1000);
        if (millis() - s_last_feed_ms < HANG_LIMIT_MS) {
            continue;
        }
        Preferences p;
        p.begin(NS, false);
        p.putUChar("crashes", p.getUChar("crashes", 0) + 1);
        p.end();
        Serial.println("boot watchdog: no progress, restarting");
        diag_printf("boot watchdog: no progress for %lu s, restarting\n",
                    (unsigned long)(HANG_LIMIT_MS / 1000));
        esp_restart();
    }
}

void boot_guard_feed()
{
    s_last_feed_ms = millis();
}

static bool crash_reason(esp_reset_reason_t r)
{
    return r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT ||
           r == ESP_RST_BROWNOUT;
}

void boot_guard_begin()
{
    Preferences p;
    p.begin(NS, false);
    // a new build gets a fresh start
    if (p.getString("ver", "") != FIRMWARE_VERSION) {
        p.putString("ver", FIRMWARE_VERSION);
        p.putUChar("crashes", 0);
    }
    s_crashes = p.getUChar("crashes", 0);
    if (crash_reason(esp_reset_reason())) {
        s_crashes++;
        p.putUChar("crashes", s_crashes);
    }
    p.end();
    s_safe = s_crashes >= SAFE_MODE_CRASHES;
    boot_guard_feed();
    xTaskCreate(watchdog_task, "bootwd", 3072, nullptr, 5, nullptr);
#ifdef FORCE_SAFE_MODE
    s_safe = true; // `pio run -e safe`: networking only, for bring-up
#endif
    Serial.printf("boot guard: reset reason %d, %d crash(es) in a row%s\n", (int)esp_reset_reason(),
                  s_crashes, s_safe ? ", SAFE MODE" : "");
}

bool boot_guard_safe_mode()
{
    return s_safe;
}
int boot_guard_crashes()
{
    return s_crashes;
}

void boot_guard_tick()
{
    boot_guard_feed();
    if (s_stable || s_safe || millis() < STABLE_AFTER_MS) {
        return;
    }
    s_stable = true;
    if (s_crashes == 0) {
        return;
    }
    Preferences p;
    p.begin(NS, false);
    p.putUChar("crashes", 0);
    p.end();
    DBG("boot guard: stable, crash counter cleared\n");
}

void boot_guard_leave_safe_mode()
{
    Preferences p;
    p.begin(NS, false);
    p.putUChar("crashes", 0);
    p.end();
}
