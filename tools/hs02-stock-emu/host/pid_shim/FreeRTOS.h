#pragma once
#include <stddef.h>
#include <stdint.h>
typedef uint32_t TickType_t;
typedef void    *TaskHandle_t;
#define pdTRUE       1
#define TICKS_SECOND 1000 // configTICK_RATE_HZ on the HS-02
#define TICKS_100MS  (TICKS_SECOND / 10)
TickType_t   xTaskGetTickCount();
TaskHandle_t xTaskGetCurrentTaskHandle();
uint32_t     ulTaskNotifyTake(int clearOnExit, TickType_t ticksToWait);
