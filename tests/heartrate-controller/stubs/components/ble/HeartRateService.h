#pragma once

#include <cstdint>

namespace Pinetime::Controllers {
  class HeartRateService {
  public:
    unsigned notifications = 0;

    void OnNewHeartRateValue(uint8_t) {
      ++notifications;
    }
  };
}
