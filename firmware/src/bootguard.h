// Crash-loop protection. A firmware that panics before it has run stably for a while is
// started again in "safe mode" (no display, no sound or LED drivers) so that Wi-Fi, the web
// server and OTA keep working and a fixed image can still be installed.
#pragma once
#include <Arduino.h>

void boot_guard_begin();            // call first thing in setup()
bool boot_guard_safe_mode();        // true when this boot runs in safe mode
int  boot_guard_crashes();          // consecutive crashes counted before this boot
void boot_guard_feed();             // progress marker for the hang watchdog (diag_stage() and the wait loops call it)
void boot_guard_tick();             // call from loop(): marks the boot as stable after a while
void boot_guard_leave_safe_mode();  // clears the counter; the caller restarts
