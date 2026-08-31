#pragma once

#include <cstdint>

#include <FreeRTOS.h>
#include "components/heartrate/HeartRateController.h"

namespace Pinetime::Applications {

  enum class HeartRateReadingStatus : uint8_t { Unavailable, Fresh, Stale };

  // The production controller retains timing information for its last valid
  // optical reading. InfiniSim's lightweight controller predates that API, so
  // this adapter gives the simulator a safe fresh-or-unavailable fallback
  // without weakening the firmware's richer stale-reading behaviour.
  template <typename Controller>
  HeartRateReadingStatus GetHeartRateReadingStatus(const Controller& controller, TickType_t now) {
    if constexpr (requires {
                    typename Controller::ReadingStatus;
                    controller.GetReadingStatus(now);
                  }) {
      switch (controller.GetReadingStatus(now)) {
        case Controller::ReadingStatus::Fresh:
          return HeartRateReadingStatus::Fresh;
        case Controller::ReadingStatus::Stale:
          return HeartRateReadingStatus::Stale;
        case Controller::ReadingStatus::Unavailable:
          return HeartRateReadingStatus::Unavailable;
      }
    }

    if (controller.HeartRate() != 0 && controller.State() != Controller::States::Stopped) {
      return HeartRateReadingStatus::Fresh;
    }
    return HeartRateReadingStatus::Unavailable;
  }

  template <typename Controller>
  uint32_t LastHeartRateAgeSeconds(const Controller& controller, TickType_t now) {
    if constexpr (requires { controller.LastValidHeartRateAgeSeconds(now); }) {
      return controller.LastValidHeartRateAgeSeconds(now);
    }
    return 0;
  }
}
