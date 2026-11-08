#pragma once
#include <cstdint>
#include <vector>
#include <task.h>

namespace Pinetime::Drivers {
  class Hrs3300 {
  public:
    struct Data { bool valid; uint16_t hrs; uint16_t als; };
    std::vector<TickType_t> reads;
    TickType_t readCost = 0;
    unsigned enables = 0;
    unsigned disables = 0;
    void Enable() { ++enables; }
    void Disable() { ++disables; }
    Data ReadHrsAls() {
      reads.push_back(xTaskGetTickCount());
      vTaskDelay(readCost);
      return {true, 4000, 0};
    }
  };
}
