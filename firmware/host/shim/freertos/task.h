#pragma once
#include "FreeRTOS.h"

#ifdef HOST_REAL_TASKS
// The OTA test runs task bodies on real threads (implemented in ota_test/ota_test_stubs.cpp).
BaseType_t xTaskCreatePinnedToCore(TaskFunction_t fn, const char *name, uint32_t stack, void *param, UBaseType_t prio,
                                   TaskHandle_t *handle, BaseType_t core);
void vTaskDelete(TaskHandle_t);
inline void vTaskDelay(TickType_t) {}
#else
inline BaseType_t xTaskCreatePinnedToCore(TaskFunction_t, const char *, uint32_t, void *, UBaseType_t,
                                          TaskHandle_t *handle, BaseType_t)
{
    if (handle) *handle = nullptr;
    return pdPASS;                      // does nothing: the driver calls the task bodies itself
}
inline void vTaskDelete(TaskHandle_t) {}
inline void vTaskDelay(TickType_t) {}
#endif
inline UBaseType_t uxTaskGetStackHighWaterMark(TaskHandle_t) { return 1234; }
