// data.h: network fetch tasks.
#pragma once
#include <Arduino.h>

// FUN_4200b888: walks the task table and starts, resumes or suspends each task
// depending on which pages are enabled. Called after WiFi is up and whenever the
// page settings change.
void tasks_apply_page_flags();

bool wifi_connected();   // WiFi.status() == WL_CONNECTED (FUN_4206ef18() == 3)

// individual task bodies (entries of the task table at 0x3fc95718)
void task_fetch_price(void *);     // FUN_4201fc6c
void task_fetch_chart(void *);     // FUN_42014a70
void task_fetch_mempool(void *);   // FUN_4201770c
void task_bitaxe_status(void *);   // FUN_420113d0
void task_stream_status(void *);   // FUN_42023190
void task_fetch_news(void *);      // FUN_4201b28c

// FUN_42035094: derives chartChange[] from the stored chart JSON
void chart_update_changes(uint32_t min_age_ms);
