#pragma once
// Host shim: just enough of FreeRTOS for the UI/display code to compile natively.
#include "FreeRTOSConfig.h"
#include "portmacro.h"
#include <stddef.h>
typedef void *SemaphoreHandle_t;
typedef void *TaskHandle_t;
typedef void *QueueHandle_t;
typedef struct { int dummy; } StaticSemaphore_t;
TickType_t xTaskGetTickCount(void);
static inline SemaphoreHandle_t xSemaphoreCreateBinaryStatic(StaticSemaphore_t *b) { return (SemaphoreHandle_t)b; }
static inline BaseType_t xSemaphoreGive(SemaphoreHandle_t) { return 1; }
static inline BaseType_t xSemaphoreTake(SemaphoreHandle_t, TickType_t) { return 1; }
static inline void vTaskDelay(TickType_t) {}
static inline void vTaskDelayUntil(TickType_t *, TickType_t) {}
#define pdTRUE  1
#define pdFALSE 0
#define pdPASS  1
#define pdMS_TO_TICKS(x) (x)
