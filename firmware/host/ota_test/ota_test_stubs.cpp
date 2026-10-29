// Everything the real ota.cpp needs from the platform, for the host OTA test: real clock and
// sleeping, threads for task bodies, the Update recorder, a connected Wi-Fi.
#include <pthread.h>
#include <stdarg.h>
#include <time.h>
#include <unistd.h>
#include "WiFi.h"
#include "Update.h"
#include "diag.h"

extern "C" uint32_t millis(void)
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ull + ts.tv_nsec / 1000000ull);
}
extern "C" void delay(uint32_t ms) { usleep(ms * 1000u); }
extern "C" time_t host_time(time_t *t) { return time(t); }

bool g_host_wifi_connected = true;
int g_host_scan_count = 0;
WiFiClass WiFi;
UpdateClass Update;

void diag_printf(const char *fmt, ...)
{
    if (!getenv("OTA_TEST_VERBOSE")) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

struct TaskStart { TaskFunction_t fn; void *param; };
static void *thread_main(void *p)
{
    TaskStart t = *(TaskStart *)p;
    delete (TaskStart *)p;
    t.fn(t.param);
    return nullptr;
}

BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *, uint32_t, void *param, UBaseType_t,
                                   TaskHandle_t *handle, BaseType_t)
{
    pthread_t th;
    pthread_create(&th, nullptr, thread_main, new TaskStart{ fn, param });
    pthread_detach(th);
    if (handle) *handle = nullptr;
    return pdPASS;
}

void vTaskDelete(TaskHandle_t) { pthread_exit(nullptr); }     // only ever called as the last statement of a task
