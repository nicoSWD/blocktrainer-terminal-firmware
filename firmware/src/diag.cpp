#include <stdarg.h>
#include <esp_system.h>
#include <esp_attr.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include "diag.h"
#include "bootguard.h"
#include "config.h"
#include "net.h"
#include "ota.h"
#include "ui.h"
#include "hw.h"

static const uint32_t MAGIC = 0xD1A60001;
static const size_t LOG_SIZE = 3072;

// RTC memory is kept over software, panic and watchdog resets, but not over a power cycle.
struct RtcState {
    uint32_t magic;
    uint32_t boots;
    char stage[40];
    uint32_t log_len;
    char log[LOG_SIZE];
};
static RTC_NOINIT_ATTR RtcState s_rtc;

static char s_prev_log[LOG_SIZE];
static char s_prev_stage[40];
static uint32_t s_prev_boots;
static bool s_prev_valid = false;
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

void diag_init()
{
    s_prev_valid = s_rtc.magic == MAGIC;
    if (s_prev_valid) {
        s_rtc.log[LOG_SIZE - 1] = 0;
        s_rtc.stage[sizeof(s_rtc.stage) - 1] = 0;
        strlcpy(s_prev_log, s_rtc.log, sizeof(s_prev_log));
        strlcpy(s_prev_stage, s_rtc.stage, sizeof(s_prev_stage));
        s_prev_boots = s_rtc.boots;
        s_rtc.boots++;
    } else {
        s_prev_log[0] = s_prev_stage[0] = 0;
        s_prev_boots = 0;
        s_rtc.magic = MAGIC;
        s_rtc.boots = 1;
    }
    s_rtc.log_len = 0;
    s_rtc.log[0] = 0;
    strlcpy(s_rtc.stage, "boot", sizeof(s_rtc.stage));
}

static void append(const char *line, size_t n)
{
    portENTER_CRITICAL(&s_lock);
    if (s_rtc.log_len + n >= LOG_SIZE - 1) { // drop the oldest half
        size_t keep = LOG_SIZE / 2;
        memmove(s_rtc.log, s_rtc.log + (s_rtc.log_len - keep), keep);
        s_rtc.log_len = keep;
    }
    memcpy(s_rtc.log + s_rtc.log_len, line, n);
    s_rtc.log_len += n;
    s_rtc.log[s_rtc.log_len] = 0;
    portEXIT_CRITICAL(&s_lock);
}

void diag_printf(const char *fmt, ...)
{
    char body[144];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);

    char line[176];
    int n = snprintf(line, sizeof(line), "[%lu] %s", (unsigned long)millis(), body);
    if (n < 0) {
        return;
    }
    if (n >= (int)sizeof(line)) {
        n = sizeof(line) - 1;
    }
    if (n == 0 || line[n - 1] != '\n') {
        if (n < (int)sizeof(line) - 1) {
            line[n++] = '\n';
        }
        line[n] = 0;
    }
    append(line, n);
    if (cfg.debugEnabled) {
        Serial.write((const uint8_t *)line, n);
    }
}

void diag_stage(const char *name)
{
    strlcpy(s_rtc.stage, name, sizeof(s_rtc.stage));
    boot_guard_feed();
    diag_printf("stage: %s\n", name);
}

static const char *reset_reason_name(esp_reset_reason_t r)
{
    switch (r) {
    case ESP_RST_POWERON:
        return "power on";
    case ESP_RST_SW:
        return "software restart";
    case ESP_RST_PANIC:
        return "PANIC";
    case ESP_RST_INT_WDT:
        return "interrupt watchdog";
    case ESP_RST_TASK_WDT:
        return "task watchdog";
    case ESP_RST_WDT:
        return "other watchdog";
    case ESP_RST_BROWNOUT:
        return "BROWNOUT";
    case ESP_RST_DEEPSLEEP:
        return "deep sleep";
    default:
        return "other";
    }
}

static void stack_line(String &out, const char *name, TaskHandle_t h)
{
    if (!h) {
        return;
    }
    out += String("  ") + name + ": " + (unsigned)uxTaskGetStackHighWaterMark(h) + " bytes never used\n";
}

String diag_report()
{
    String r;
    r.reserve(8192);
    r += String("firmware ") + FIRMWARE_VERSION + "\n";
    r += String("uptime ") + (millis() / 1000) + " s, boot #" + s_rtc.boots +
         ", reset: " + reset_reason_name(esp_reset_reason()) + "\n";
    r += String("safe mode: ") + (boot_guard_safe_mode() ? "YES" : "no") +
         ", crashes counted: " + boot_guard_crashes() + "\n";
    r += String("display ready: ") + (ui_is_ready() ? "yes" : "NO") +
         ", hardware drivers: " + (hw_is_ready() ? "yes" : "NO") + "\n";
    r += String("stage now: ") + s_rtc.stage + "\n";
    if (s_prev_valid) {
        r += String("stage when the previous run ended: ") + s_prev_stage + "\n";
    }
    r += String("heap: internal free ") + (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL) +
         ", minimum ever " + (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) +
         ", largest block " + (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) +
         "; psram free " + (unsigned)ESP.getFreePsram() + " of " + (unsigned)ESP.getPsramSize() + "\n";
    r += String("wifi: ") +
         (WiFi.status() == WL_CONNECTED
              ? WiFi.SSID() + " " + WiFi.localIP().toString() + " rssi " + WiFi.RSSI()
              : String("not connected")) +
         "\n";
    r += String("ota: stage ") + (int)ota.stage + ", remote '" + ota.remoteVersion + "', error '" +
         ota.error + "'\n";
    r += "stack space never used (small numbers are dangerous):\n";
    stack_line(r, "loopTask (this handler)", xTaskGetCurrentTaskHandle());
    ui_stack_report(r);
    net_stack_report(r);
    if (s_prev_valid) {
        r += "\n--- log of the previous run (kept over a crash) ---\n" + String(s_prev_log);
    }
    r += "\n--- log of this run ---\n" + String(s_rtc.log);
    return r;
}
