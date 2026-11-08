#pragma once

#include <cstdint>

using TickType_t = uint32_t;
#define configTICK_RATE_HZ 1024
#define pdMS_TO_TICKS(ms) static_cast<TickType_t>((static_cast<uint64_t>(ms) * configTICK_RATE_HZ) / 1000)

TickType_t xTaskGetTickCount();
