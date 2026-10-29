// Small interface between the host stubs and the driver.
#pragma once
#include <stdint.h>
#include <time.h>

struct HostStats {
    int clicks = 0, alerts = 0, led_calls = 0, ota_starts = 0, reconnects = 0, page_flags_calls = 0;
    uint32_t led_rgb = 0;
};
extern HostStats g_host_stat;
void host_clock_advance(uint32_t ms);
void host_set_wall_time(time_t t);
