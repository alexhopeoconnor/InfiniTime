#pragma once
#include <FreeRTOS.h>
#include <cstdint>
#include <optional>
#include <task.h>
#include <queue.h>
#include <components/heartrate/Ppg.h>
#ifdef ELIXIR_HR_STUDY
  #include <components/heartrate/HrStudyRecord.h>
#endif
#include "components/settings/Settings.h"

namespace Pinetime {
  namespace Drivers {
    class Hrs3300;
  }

  namespace Controllers {
    class Battery;
    class HeartRateController;
#ifdef ELIXIR_HR_STUDY
    class MotionController;
#endif
  }

  namespace Applications {
    class HeartRateTask {
    public:
      enum class Messages : uint8_t {
        GoToSleep,
        WakeUp,
        Enable,
        Disable,
        BackgroundSettingsChanged,
        PowerStateChanged,
#ifdef ELIXIR_HR_STUDY
        StudyStart,
        StudyStop,
        StudyFlush,
        StudyIndicationComplete,
#endif
      };

      explicit HeartRateTask(Drivers::Hrs3300& heartRateSensor,
                             Controllers::HeartRateController& controller,
                             Controllers::Settings& settings,
                             const Controllers::Battery& battery
#ifdef ELIXIR_HR_STUDY
                             , Controllers::MotionController& motionController
#endif
                             );
      void Start();
      void Work();
      void PushMessage(Messages msg);

    private:
      enum class States : uint8_t { Disabled, Waiting, BackgroundMeasuring, ForegroundMeasuring };
      static void Process(void* instance);
      void HandleSensorData();
      void HandleMeasurementTimeout(bool reportSignalFailure);
      void StartMeasurement();
      void StopMeasurement();
      void UpdateMeasurementMode();

#ifdef ELIXIR_HR_STUDY
      void ResetStudyMeasurementStats();
      void CaptureStudySensorSample(uint16_t hrs, uint16_t als);
      void ReportStudyOutcome(Controllers::HrStudyOutcome outcome, uint8_t bpm);
      void DrainStudyBuffer();
#endif

      [[nodiscard]] bool BackgroundMeasurementNeeded() const;
      [[nodiscard]] std::optional<TickType_t> BackgroundMeasurementInterval() const;
      TickType_t CurrentTaskDelay();

      TaskHandle_t taskHandle;
      QueueHandle_t messageQueue;
      bool valueCurrentlyShown;
      bool measurementSucceeded;
      bool manualMeasurementRequested = false;
      States state = States::Disabled;
      uint16_t count;
      Drivers::Hrs3300& heartRateSensor;
      Controllers::HeartRateController& controller;
      Controllers::Settings& settings;
      const Controllers::Battery& battery;
      Controllers::Ppg ppg;
      TickType_t lastMeasurementTime;
      TickType_t measurementStartTime;
#ifdef ELIXIR_HR_STUDY
      Controllers::MotionController& motionController;
      Controllers::HrStudyBuffer<128> studyBuffer;
      uint32_t studySequence = 0;
      uint32_t studyPpgSum = 0;
      uint32_t studyStepStart = 0;
      uint16_t studyPpgMin = 0;
      uint16_t studyPpgMax = 0;
      uint16_t studyPpgSamples = 0;
      uint16_t studyMotionLevel = 0;
      uint8_t studyAmbientLevel = 0;
      bool studySessionActive = false;
      bool studyWindowReported = false;
      Controllers::HrStudyOutcome studyLastOutcome = Controllers::HrStudyOutcome::NotEnoughData;
#endif
    };

  }
}
