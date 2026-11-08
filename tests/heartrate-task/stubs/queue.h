#pragma once
#include <FreeRTOS.h>
inline QueueHandle_t xQueueCreate(unsigned, unsigned) { return reinterpret_cast<void*>(1); }
BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t);
BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t);
BaseType_t xQueueSendFromISR(QueueHandle_t, const void*, BaseType_t*);
