// Diagnostics that work without a serial connection: breadcrumbs and a log ring buffer kept in RTC
// memory (they survive a panic or watchdog reset), served as text at http://<ip>/diag.
#pragma once
#include <Arduino.h>

void diag_init();                  // first thing in setup(): rescues the previous boot's log
void diag_stage(const char *name); // boot milestone, remembered across a crash (also feeds the boot watchdog)
void diag_printf(const char *fmt, ...)
    __attribute__((format(printf, 1, 2))); // ring buffer always, Serial when debug is on
String diag_report();                      // the /diag text
