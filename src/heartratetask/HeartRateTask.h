#pragma once
#include <atomic>
#include <FreeRTOS.h>
#include <cstdint>
#include <optional>
#include <task.h>
#include <queue.h>
#include <drivers/Bma421.h>
#include <components/heartrate/Ppg.h>
#ifdef ELIXIR_HR_STUDY
  #include <components/heartrate/HrStudyRecord.h>
#endif
#include "components/settings/Settings.h"

namespace Pinetime {
  namespace Drivers {
    class Hrs3300;
    class Bma421;
  }

  namespace Controllers {
    class Battery;
    class HeartRateController;
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
        StudyNextFrame,
        StudyAcknowledgement,
#endif
      };

      explicit HeartRateTask(Drivers::Hrs3300& heartRateSensor,
                             Controllers::HeartRateController& controller,
                             Controllers::Settings& settings,
                             const Controllers::Battery& battery,
                             Drivers::Bma421& motionSensor);
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
      void CaptureStudySensorSample(uint16_t hrs, uint16_t als, const Drivers::Bma421::Values& motionValues);
      void CaptureStudyPpgDiagnostics();
      void CaptureStudySensorConfiguration();
      void ReportStudyOutcome(Controllers::HrStudyOutcome outcome, uint8_t bpm);
      void DrainStudyBuffer();
      void ExpireStudyCaptureIfNeeded(TickType_t now);
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
      uint16_t lastHrs = 0;
      Drivers::Hrs3300& heartRateSensor;
      Controllers::HeartRateController& controller;
      Controllers::Settings& settings;
      const Controllers::Battery& battery;
      Drivers::Bma421& motionSensor;
      Controllers::Ppg ppg;
      TickType_t lastMeasurementTime;
      TickType_t measurementStartTime;
#ifdef ELIXIR_HR_STUDY
      Controllers::HrStudyBuffer<128> studyBuffer;
      uint32_t studySequence = 0;
      uint32_t studyPpgSum = 0;
      uint32_t studyStepStart = 0;
      uint32_t studyCurrentSteps = 0;
      uint16_t studyPpgMin = 0;
      uint16_t studyPpgMax = 0;
      uint16_t studyPpgSamples = 0;
      uint16_t studyMotionLevel = 0;
      uint16_t studyAmbientMaximum = 0;
      uint16_t studyPpgAnalyses = 0;
      uint16_t studyPpgLastAnalysisGeneration = 0;
      uint8_t studySensorConfiguration = 0;
      uint8_t studyLastSensorConfiguration = 0;
      uint8_t studySensorTransitions = 0;
      int16_t studyPreviousX = 0;
      int16_t studyPreviousY = 0;
      int16_t studyPreviousZ = 0;
      bool studyHasPreviousMotion = false;
      bool studyHasSensorConfiguration = false;
      bool studySessionActive = false;
      bool studyCaptureActive = false;
      bool studyWindowReported = false;
      TickType_t studyCaptureStartedAt = 0;
      TickType_t studyCaptureDuration = 0;
      Controllers::HrStudyOutcome studyLastOutcome = Controllers::HrStudyOutcome::NotEnoughData;
      Controllers::Ppg::Diagnostics studyPpgDiagnostics {};
      std::atomic_uint16_t studyQueueDrops {0};
#endif
    };

  }
}
