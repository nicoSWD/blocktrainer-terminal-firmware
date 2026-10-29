// Mutex shim backed by an error-checking pthread mutex: relocking from the same thread aborts,
// which is what a (non-recursive) FreeRTOS mutex would turn into on the device (a deadlock).
#pragma once
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include "FreeRTOS.h"

typedef pthread_mutex_t *SemaphoreHandle_t;

inline SemaphoreHandle_t xSemaphoreCreateMutex()
{
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_ERRORCHECK);
    pthread_mutex_t *m = (pthread_mutex_t *)malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    return m;
}

inline BaseType_t xSemaphoreTake(SemaphoreHandle_t m, TickType_t)
{
    if (!m) return pdFALSE;
    int r = pthread_mutex_lock(m);
    if (r != 0) {
        fprintf(stderr, "HOST: mutex relock / lock error %d (would deadlock on the device)\n", r);
        abort();
    }
    return pdTRUE;
}

inline BaseType_t xSemaphoreGive(SemaphoreHandle_t m)
{
    if (!m) return pdFALSE;
    return pthread_mutex_unlock(m) == 0 ? pdTRUE : pdFALSE;
}
