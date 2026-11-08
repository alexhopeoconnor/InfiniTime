#pragma once
#include <FreeRTOS.h>
TickType_t xTaskGetTickCount();
void vTaskDelay(TickType_t);
inline BaseType_t xTaskCreate(void (*)(void*), const char*, unsigned, void*, unsigned, TaskHandle_t*) { return pdPASS; }
