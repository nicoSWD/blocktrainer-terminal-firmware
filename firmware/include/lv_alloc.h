// LVGL allocates its objects through these: PSRAM first, so the scarce internal heap stays free
// for Wi-Fi and TLS. Falls back to internal RAM when there is no PSRAM or it is full.
#pragma once
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
void *ui_alloc(size_t size);
void ui_free(void *ptr);
void *ui_realloc(void *ptr, size_t size);
#ifdef __cplusplus
}
#endif
