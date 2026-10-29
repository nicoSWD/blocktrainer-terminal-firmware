// Host shim for ESP-IDF heap_caps_*: plain malloc with live-block accounting, so the test driver
// can show that the LVGL heap (ui_alloc/ui_free/ui_realloc go through here) does not grow.
#pragma once
#include <stddef.h>
#include <stdint.h>

#define MALLOC_CAP_EXEC     (1 << 0)
#define MALLOC_CAP_32BIT    (1 << 1)
#define MALLOC_CAP_8BIT     (1 << 2)
#define MALLOC_CAP_DMA      (1 << 3)
#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_DEFAULT  (1 << 12)

#ifdef __cplusplus
extern "C" {
#endif

void *heap_caps_malloc(size_t size, uint32_t caps);
void *heap_caps_calloc(size_t n, size_t size, uint32_t caps);
void *heap_caps_realloc(void *ptr, size_t size, uint32_t caps);
void heap_caps_free(void *ptr);
size_t heap_caps_get_free_size(uint32_t caps);
size_t heap_caps_get_largest_free_block(uint32_t caps);
size_t heap_caps_get_minimum_free_size(uint32_t caps);

// host-only accounting
size_t host_heap_live_blocks(void);
size_t host_heap_live_bytes(void);
size_t host_heap_peak_bytes(void);

#ifdef __cplusplus
}
#endif
