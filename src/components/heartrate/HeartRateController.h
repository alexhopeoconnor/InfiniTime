#pragma once

#include <cstdint>
#include <FreeRTOS.h>
#include <components/ble/HeartRateService.h>
#ifdef ELIXIR_HR_STUDY
  #include <components/heartrate/HrStudyRecord.h>
#endif

namespace Pinetime {
  namespace Applications {
    class HeartRateTask;
  }

  namespace System {
    class SystemTask;
  }

  namespace Controllers {
#ifdef ELIXIR_HR_STUDY
    class ElixirHrStudyService;
#endif
    class HeartRateController {
    public:
      enum class States : uint8_t {
        Disabled,
        Stopped,
        NotEnoughData,
        Searching,
        Measuring,
        NoTouch,
        SignalUnstable,
        AmbientLight,
        SensorError,
      };
      enum class MeasurementMode : uint8_t { Idle, Background, Foreground };
      enum class ReadingStatus : uint8_t { Unavailable, Fresh, Stale };

      HeartRateController() = default;
      void Enable();
      void Disable();
      void OnBackgroundSettingsChanged();
      void UpdateState(States newState);
      void UpdateHeartRate(uint8_t heartRate);
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
        return (static_cast<uint64_t>(now - lastValidHeartRateTick) * portTICK_PERIOD_MS) / 1000;
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

#ifdef ELIXIR_HR_STUDY
      void SetStudyService(Pinetime::Controllers::ElixirHrStudyService* service);
      Pinetime::Controllers::ElixirHrStudyService* StudyService() const;
      HrStudyTransportState GetStudyTransportState() const;
      void RequestStudyStart();
      void RequestStudyStop();
      void RequestStudyFlush();
      void RequestStudyIndicationComplete();
#endif

    private:
      Applications::HeartRateTask* task = nullptr;
      States state = States::Disabled;
      MeasurementMode measurementMode = MeasurementMode::Idle;
      uint8_t heartRate = 0;
      bool hasValidHeartRate = false;
      TickType_t lastValidHeartRateTick = 0;
      Pinetime::Controllers::HeartRateService* service = nullptr;
#ifdef ELIXIR_HR_STUDY
      Pinetime::Controllers::ElixirHrStudyService* studyService = nullptr;
#endif
    };
  }
}
