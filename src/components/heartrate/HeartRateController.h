#pragma once

#include <cstdint>
#include <FreeRTOS.h>
#include <components/ble/HeartRateService.h>

namespace Pinetime {
  namespace Applications {
    class HeartRateTask;
  }

  namespace System {
    class SystemTask;
  }

  namespace Controllers {
    class HeartRateController {
    public:
      enum class States : uint8_t { Stopped, NotEnoughData, NoTouch, SignalUnstable, AmbientLight, SensorError, Running };
      enum class MeasurementMode : uint8_t { Idle, Background, Foreground };
      enum class ReadingStatus : uint8_t { Unavailable, Fresh, Stale };

      HeartRateController() = default;
      void Enable();
      void Disable();
      void OnBackgroundSettingsChanged();
      void Update(States newState, uint8_t heartRate);
      void SetMeasurementMode(MeasurementMode newMode);

      void SetHeartRateTask(Applications::HeartRateTask* task);

      States State() const {
        return state;
      }

      MeasurementMode GetMeasurementMode() const {
        return measurementMode;
      }

      uint8_t HeartRate() const {
        return heartRate;
      }

      bool HasValidHeartRate() const {
        return hasValidHeartRate;
      }

      TickType_t LastValidHeartRateTick() const {
        return lastValidHeartRateTick;
      }

      uint32_t LastValidHeartRateAgeSeconds(TickType_t now) const {
        if (!hasValidHeartRate) {
          return 0;
        }
        return (static_cast<uint64_t>(now - lastValidHeartRateTick) * 1000ULL) / configTICK_RATE_HZ;
      }

      ReadingStatus GetReadingStatus(TickType_t now) const {
        if (!hasValidHeartRate) {
          return ReadingStatus::Unavailable;
        }

        const auto age = now - lastValidHeartRateTick;
        if (age <= pdMS_TO_TICKS(30 * 1000)) {
          return ReadingStatus::Fresh;
        }
        if (age <= pdMS_TO_TICKS(2 * 60 * 1000)) {
          return ReadingStatus::Stale;
        }
        return ReadingStatus::Unavailable;
      }

      void SetService(Pinetime::Controllers::HeartRateService* service);

    private:
      Applications::HeartRateTask* task = nullptr;
      States state = States::Stopped;
      MeasurementMode measurementMode = MeasurementMode::Idle;
      uint8_t heartRate = 0;
      bool hasValidHeartRate = false;
      TickType_t lastValidHeartRateTick = 0;
      Pinetime::Controllers::HeartRateService* service = nullptr;
    };
  }
}
