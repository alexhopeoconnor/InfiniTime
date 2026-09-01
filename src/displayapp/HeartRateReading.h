#pragma once

#include <cstdint>

#include <FreeRTOS.h>
#include "components/heartrate/HeartRateController.h"

namespace Pinetime::Applications {

  enum class HeartRateReadingStatus : uint8_t { Unavailable, Fresh, Stale };
  enum class HeartRateMeasurementMode : uint8_t { Idle, Background, Foreground };
  enum class HeartRateAcquisitionStatus : uint8_t { Stopped, Acquiring, NoTouch, SignalUnstable, AmbientLight, SensorError, Running };

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

  // Keep the full InfiniSim source tree buildable while allowing the device
  // UI to distinguish its scheduled optical sampling from an explicit user
  // measurement. Older simulator controllers only expose State().
  template <typename Controller>
  HeartRateMeasurementMode GetHeartRateMeasurementMode(const Controller& controller) {
    if constexpr (requires {
                    typename Controller::MeasurementMode;
                    controller.GetMeasurementMode();
                  }) {
      switch (controller.GetMeasurementMode()) {
        case Controller::MeasurementMode::Background:
          return HeartRateMeasurementMode::Background;
        case Controller::MeasurementMode::Foreground:
          return HeartRateMeasurementMode::Foreground;
        case Controller::MeasurementMode::Idle:
          return HeartRateMeasurementMode::Idle;
      }
    }

    return controller.State() == Controller::States::Stopped ? HeartRateMeasurementMode::Idle
                                                               : HeartRateMeasurementMode::Foreground;
  }

  // InfiniSim intentionally uses an older, smaller controller API. Map the
  // device's richer diagnostic states here so screens do not depend on enum
  // values which do not exist in that simulator implementation.
  template <typename Controller>
  HeartRateAcquisitionStatus GetHeartRateAcquisitionStatus(const Controller& controller) {
    const auto state = controller.State();
    if (state == Controller::States::Stopped) {
      return HeartRateAcquisitionStatus::Stopped;
    }
    if (state == Controller::States::NotEnoughData) {
      return HeartRateAcquisitionStatus::Acquiring;
    }
    if (state == Controller::States::NoTouch) {
      return HeartRateAcquisitionStatus::NoTouch;
    }
    if constexpr (requires { Controller::States::SignalUnstable; }) {
      if (state == Controller::States::SignalUnstable) {
        return HeartRateAcquisitionStatus::SignalUnstable;
      }
    }
    if constexpr (requires { Controller::States::AmbientLight; }) {
      if (state == Controller::States::AmbientLight) {
        return HeartRateAcquisitionStatus::AmbientLight;
      }
    }
    if constexpr (requires { Controller::States::SensorError; }) {
      if (state == Controller::States::SensorError) {
        return HeartRateAcquisitionStatus::SensorError;
      }
    }
    return HeartRateAcquisitionStatus::Running;
  }
}
