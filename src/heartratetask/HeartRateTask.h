#pragma once

#include <FreeRTOS.h>
#include <cstdint>
#include <optional>
#include <queue.h>
#include <task.h>

#include <drivers/Bma421.h>
#include "components/heartrate/HeartRateController.h"
#include "components/heartrate/Ppg.h"
#include "components/settings/Settings.h"
#ifdef ELIXIR_HR_STUDY
  #include "components/heartrate/HrStudyRecord.h"
#endif

namespace Pinetime {
  namespace Drivers {
    class Hrs3300;
    class Bma421;
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

      [[nodiscard]] bool BackgroundMeasurementNeeded() const;
      [[nodiscard]] std::optional<TickType_t> BackgroundMeasurementInterval() const;
      TickType_t CurrentTaskDelay();
      void SendHeartRate(Controllers::HeartRateController::States state, int bpm);

#ifdef ELIXIR_HR_STUDY
      void ResetStudyMeasurementStats();
      void CaptureStudySensorSample(uint16_t hrs, uint16_t als, const Drivers::Bma421::Values& motionValues);
      void ReportStudyOutcome(Controllers::HrStudyOutcome outcome, uint8_t bpm);
      void DrainStudyBuffer();
#endif

      TaskHandle_t taskHandle;
      QueueHandle_t messageQueue;
      bool valueCurrentlyShown;
      bool manualMeasurementRequested = false;
      States state = States::Disabled;
      uint16_t count;
      uint16_t lastHrs = 0;
      Drivers::Hrs3300& heartRateSensor;
      Controllers::HeartRateController& controller;
      Controllers::Settings& settings;
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
      uint8_t studyAmbientLevel = 0;
      int16_t studyPreviousX = 0;
      int16_t studyPreviousY = 0;
      int16_t studyPreviousZ = 0;
      bool studyHasPreviousMotion = false;
      bool studySessionActive = false;
      bool studyWindowReported = false;
      Controllers::HrStudyOutcome studyLastOutcome = Controllers::HrStudyOutcome::NotEnoughData;
#endif
    };
  }
}
