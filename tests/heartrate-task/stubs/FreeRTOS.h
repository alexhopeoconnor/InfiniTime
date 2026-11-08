#pragma once
#include <cstdint>

using TickType_t = uint32_t;
using BaseType_t = int;
using TaskHandle_t = void*;
using QueueHandle_t = void*;
constexpr TickType_t configTICK_RATE_HZ = 1024;
constexpr TickType_t portMAX_DELAY = UINT32_MAX;
constexpr BaseType_t pdTRUE = 1;
constexpr BaseType_t pdFALSE = 0;
constexpr BaseType_t pdPASS = 1;
constexpr int NRF_ERROR_NO_MEM = 1;
inline void APP_ERROR_HANDLER(int) { throw "task allocation failed"; }
struct TestScb { uint32_t ICSR = 0; };
inline TestScb testScb;
inline TestScb* SCB = &testScb;
constexpr uint32_t SCB_ICSR_VECTACTIVE_Msk = 0x1ff;
void TestYieldFromIsr(BaseType_t);
#define portYIELD_FROM_ISR(value) TestYieldFromIsr(value)
