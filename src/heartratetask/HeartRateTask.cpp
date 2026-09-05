#include "heartratetask/HeartRateTask.h"

#include <algorithm>
#include <components/heartrate/HeartRateController.h>
#ifdef ELIXIR_HR_STUDY
  #include <components/ble/ElixirHrStudyService.h>
#endif
#include <cstdlib>
#include <drivers/Bma421.h>
#include <drivers/Hrs3300.h>
#include <limits>
#include <optional>

#include "utility/Math.h"

using namespace Pinetime::Applications;
using ControllerStates = Pinetime::Controllers::HeartRateController::States;

namespace {
  constexpr TickType_t backgroundMeasurementTimeLimit = 30 * configTICK_RATE_HZ;
}

std::optional<TickType_t> HeartRateTask::BackgroundMeasurementInterval() const {
  auto interval = settings.GetHeartRateBackgroundMeasurementInterval();
  if (!interval.has_value()) {
    return std::nullopt;
  }
  return interval.value() * configTICK_RATE_HZ;
}

bool HeartRateTask::BackgroundMeasurementNeeded() const {
  auto backgroundPeriod = BackgroundMeasurementInterval();
  if (!backgroundPeriod.has_value()) {
    return false;
  }
  return xTaskGetTickCount() - lastMeasurementTime >= backgroundPeriod.value();
};

TickType_t HeartRateTask::CurrentTaskDelay() {
  auto backgroundPeriod = BackgroundMeasurementInterval();
  TickType_t currentTime = xTaskGetTickCount();
  auto CalculateSleepTicks = [&]() {
    TickType_t elapsed = currentTime - measurementStartTime;

    constexpr uint16_t deltaTms = Controllers::Ppg::sampleDuration * 1000;
    static_assert((configTICK_RATE_HZ / 2ULL) * (std::numeric_limits<decltype(count)>::max() + 1ULL) * static_cast<uint64_t>(deltaTms) <
                    std::numeric_limits<uint32_t>::max(),
                  "Overflow");
    TickType_t elapsedTarget = Utility::RoundedDiv(static_cast<uint32_t>(configTICK_RATE_HZ / 2) * (static_cast<uint32_t>(count) + 1U) *
                                                     static_cast<uint32_t>(deltaTms),
                                                   static_cast<uint32_t>(1000 / 2));

    if (count == std::numeric_limits<decltype(count)>::max()) {
      count = 0;
      measurementStartTime = currentTime;
    }
    if (elapsedTarget > elapsed) {
      return elapsedTarget - elapsed;
    }
    return static_cast<TickType_t>(0);
  };
  switch (state) {
    case States::Disabled:
      return portMAX_DELAY;
    case States::Waiting:
      if (!backgroundPeriod.has_value()) {
        return portMAX_DELAY;
      }
      if (currentTime - lastMeasurementTime < backgroundPeriod.value()) {
        return backgroundPeriod.value() - (currentTime - lastMeasurementTime);
      }
      return 0;
    case States::BackgroundMeasuring:
    case States::ForegroundMeasuring:
      return CalculateSleepTicks();
  }
  return portMAX_DELAY;
}

HeartRateTask::HeartRateTask(Drivers::Hrs3300& heartRateSensor,
                             Controllers::HeartRateController& controller,
                             Controllers::Settings& settings,
                             Drivers::Bma421& motionSensor)
  : heartRateSensor {heartRateSensor}, controller {controller}, settings {settings}, motionSensor {motionSensor} {
}

void HeartRateTask::Start() {
  messageQueue = xQueueCreate(10, 1);
  controller.SetHeartRateTask(this);

  if (xTaskCreate(HeartRateTask::Process, "HRM", 400, this, 1, &taskHandle) != pdPASS) {
    APP_ERROR_HANDLER(NRF_ERROR_NO_MEM);
  }
}

void HeartRateTask::Process(void* instance) {
  static_cast<HeartRateTask*>(instance)->Work();
}

void HeartRateTask::Work() {
  lastMeasurementTime = xTaskGetTickCount();
  valueCurrentlyShown = false;
  if (BackgroundMeasurementInterval().has_value()) {
    state = States::Waiting;
  }

  while (true) {
    const TickType_t delay = CurrentTaskDelay();
    Messages msg;
    States newState = state;

    if (xQueueReceive(messageQueue, &msg, delay) == pdTRUE) {
      switch (msg) {
        case Messages::GoToSleep:
          if (state != States::Disabled && state == States::ForegroundMeasuring) {
            manualMeasurementRequested = false;
            newState = BackgroundMeasurementNeeded() ? States::BackgroundMeasuring : States::Waiting;
          }
          break;
        case Messages::WakeUp:
          if (state != States::Disabled && manualMeasurementRequested) {
            newState = States::ForegroundMeasuring;
          }
          break;
        case Messages::Enable:
          manualMeasurementRequested = true;
          newState = States::ForegroundMeasuring;
          valueCurrentlyShown = false;
          break;
        case Messages::Disable:
          manualMeasurementRequested = false;
          if (BackgroundMeasurementInterval().has_value()) {
            lastMeasurementTime = xTaskGetTickCount();
            newState = States::Waiting;
          } else {
            newState = States::Disabled;
          }
          break;
        case Messages::BackgroundSettingsChanged:
          if (BackgroundMeasurementInterval().has_value()) {
            if (state == States::Disabled && !manualMeasurementRequested) {
              lastMeasurementTime = xTaskGetTickCount();
              newState = States::Waiting;
            }
          } else if (!manualMeasurementRequested) {
            newState = States::Disabled;
          }
          break;
#ifdef ELIXIR_HR_STUDY
        case Messages::StudyStart:
          studyBuffer.Clear();
          studySequence = 0;
          studySessionActive = true;
          ResetStudyMeasurementStats();
          if (auto* studyService = controller.StudyService(); studyService != nullptr) {
            studyService->SetSessionActive(true);
          }
          break;
        case Messages::StudyStop:
          studySessionActive = false;
          studyBuffer.Clear();
          if (auto* studyService = controller.StudyService(); studyService != nullptr) {
            studyService->SetSessionActive(false);
          }
          break;
        case Messages::StudyFlush:
          DrainStudyBuffer();
          break;
        case Messages::StudyIndicationComplete:
          if (auto* studyService = controller.StudyService(); studyService != nullptr &&
                                                        studyService->ConsumeIndicationCompletion() ==
                                                          Controllers::ElixirHrStudyService::IndicationCompletion::Confirmed) {
            studyBuffer.PopFront();
            DrainStudyBuffer();
          }
          break;
#endif
      }
    }

    if (newState == States::Waiting && BackgroundMeasurementNeeded()) {
      newState = States::BackgroundMeasuring;
    } else if (newState == States::BackgroundMeasuring && !BackgroundMeasurementNeeded()) {
      newState = States::Waiting;
    }

    if ((newState == States::ForegroundMeasuring || newState == States::BackgroundMeasuring) &&
        (state == States::Waiting || state == States::Disabled)) {
      StartMeasurement();
    } else if ((newState == States::Waiting || newState == States::Disabled) &&
               (state == States::ForegroundMeasuring || state == States::BackgroundMeasuring)) {
      StopMeasurement();
      controller.UpdateState(ControllerStates::Stopped);
    }
    if (newState == States::Disabled) {
      controller.UpdateState(ControllerStates::Disabled);
    }
    state = newState;
    UpdateMeasurementMode();

    if (state == States::ForegroundMeasuring || state == States::BackgroundMeasuring) {
      HandleSensorData();
      count++;
    }
  }
}

void HeartRateTask::PushMessage(HeartRateTask::Messages msg) {
  BaseType_t xHigherPriorityTaskWoken = pdFALSE;
  xQueueSendFromISR(messageQueue, &msg, &xHigherPriorityTaskWoken);
  portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

void HeartRateTask::StartMeasurement() {
  controller.UpdateState(ControllerStates::NotEnoughData);
  heartRateSensor.Enable();
  ppg.Reset();
  lastHrs = 0;
  count = 0;
  measurementStartTime = xTaskGetTickCount();
#ifdef ELIXIR_HR_STUDY
  ResetStudyMeasurementStats();
#endif
}

void HeartRateTask::StopMeasurement() {
#ifdef ELIXIR_HR_STUDY
  if (!studyWindowReported) {
    ReportStudyOutcome(Controllers::HrStudyOutcome::Interrupted, 0);
  }
#endif
  heartRateSensor.Disable();
}

void HeartRateTask::UpdateMeasurementMode() {
  switch (state) {
    case States::BackgroundMeasuring:
      controller.SetMeasurementMode(Controllers::HeartRateController::MeasurementMode::Background);
      break;
    case States::ForegroundMeasuring:
      controller.SetMeasurementMode(Controllers::HeartRateController::MeasurementMode::Foreground);
      break;
    case States::Disabled:
    case States::Waiting:
      controller.SetMeasurementMode(Controllers::HeartRateController::MeasurementMode::Idle);
      break;
  }
}

void HeartRateTask::HandleSensorData() {
  const auto sensorData = heartRateSensor.ReadHrsAls();
  if (!sensorData.valid) {
    ppg.Reset();
    controller.UpdateState(ControllerStates::SensorError);
#ifdef ELIXIR_HR_STUDY
    studyLastOutcome = Controllers::HrStudyOutcome::SensorError;
#endif
    valueCurrentlyShown = false;
    HandleMeasurementTimeout(false);
    return;
  }

  const auto motionValues = motionSensor.Process();
#ifdef ELIXIR_HR_STUDY
  CaptureStudySensorSample(sensorData.hrs, sensorData.als, motionValues);
#endif
  if (sensorData.hrs != 0 && lastHrs == 0) {
    ppg.Reset();
  }
  static constexpr float discontinuityThreshold = 0.2f;
  if (lastHrs != 0 && std::abs(static_cast<int32_t>(sensorData.hrs) - static_cast<int32_t>(lastHrs)) >
                        std::min(lastHrs, sensorData.hrs) * discontinuityThreshold) {
    ppg.ScaleHrs(static_cast<float>(sensorData.hrs) / static_cast<float>(lastHrs));
  }
  lastHrs = sensorData.hrs;
  ppg.Ingest(sensorData.hrs, motionValues.x, motionValues.y, motionValues.z);

  std::optional<uint8_t> bpm = std::nullopt;
  switch (heartRateSensor.AutoGain(sensorData.hrs, sensorData.als)) {
    case Drivers::Hrs3300::PPGState::NoTouch:
      SendHeartRate(ControllerStates::NoTouch, 0);
#ifdef ELIXIR_HR_STUDY
      studyLastOutcome = Controllers::HrStudyOutcome::NoTouch;
#endif
      break;
    case Drivers::Hrs3300::PPGState::Reset:
      ppg.Reset();
      SendHeartRate(ControllerStates::NotEnoughData, 0);
#ifdef ELIXIR_HR_STUDY
      studyLastOutcome = Controllers::HrStudyOutcome::NotEnoughData;
#endif
      break;
    case Drivers::Hrs3300::PPGState::Running:
      bpm = ppg.HeartRate();
      if (bpm.has_value()) {
        SendHeartRate(ControllerStates::Measuring, bpm.value());
#ifdef ELIXIR_HR_STUDY
        ReportStudyOutcome(Controllers::HrStudyOutcome::Accepted, bpm.value());
#endif
      } else if (ppg.SufficientData()) {
        controller.UpdateState(ControllerStates::Searching);
#ifdef ELIXIR_HR_STUDY
        studyLastOutcome = Controllers::HrStudyOutcome::SignalUnstable;
#endif
      } else {
        controller.UpdateState(ControllerStates::NotEnoughData);
#ifdef ELIXIR_HR_STUDY
        studyLastOutcome = Controllers::HrStudyOutcome::NotEnoughData;
#endif
      }
      break;
    case Drivers::Hrs3300::PPGState::Off:
#ifdef ELIXIR_HR_STUDY
      ReportStudyOutcome(Controllers::HrStudyOutcome::Interrupted, 0);
#endif
      controller.UpdateState(ControllerStates::Stopped);
      return;
  }

  if (bpm.has_value()) {
    if (state == States::BackgroundMeasuring && xTaskGetTickCount() - measurementStartTime < backgroundMeasurementTimeLimit) {
      lastMeasurementTime = measurementStartTime;
    } else {
      lastMeasurementTime = xTaskGetTickCount();
    }
    return;
  }
  HandleMeasurementTimeout(true);
}

void HeartRateTask::HandleMeasurementTimeout(bool reportSignalFailure) {
  const auto now = xTaskGetTickCount();
  if (now - measurementStartTime >= backgroundMeasurementTimeLimit) {
    if (!valueCurrentlyShown && reportSignalFailure) {
      controller.UpdateState(ControllerStates::SignalUnstable);
#ifdef ELIXIR_HR_STUDY
      if (studyLastOutcome == Controllers::HrStudyOutcome::NotEnoughData) {
        studyLastOutcome = Controllers::HrStudyOutcome::SignalUnstable;
      }
#endif
      valueCurrentlyShown = false;
    }
#ifdef ELIXIR_HR_STUDY
    if (!valueCurrentlyShown) {
      ReportStudyOutcome(studyLastOutcome, 0);
    }
#endif
    if (state == States::BackgroundMeasuring) {
      const auto backgroundPeriod = BackgroundMeasurementInterval();
      lastMeasurementTime = backgroundPeriod.has_value() && backgroundPeriod.value() > backgroundMeasurementTimeLimit ? measurementStartTime : now;
    } else {
      lastMeasurementTime = now;
    }
  }
}

void HeartRateTask::SendHeartRate(ControllerStates state, int bpm) {
  valueCurrentlyShown = bpm != 0;
  controller.UpdateState(state);
  controller.UpdateHeartRate(bpm);
}

#ifdef ELIXIR_HR_STUDY
void HeartRateTask::ResetStudyMeasurementStats() {
  studyPpgSum = 0;
  studyPpgMin = std::numeric_limits<uint16_t>::max();
  studyPpgMax = 0;
  studyPpgSamples = 0;
  studyMotionLevel = 0;
  studyAmbientLevel = 0;
  studyHasPreviousMotion = false;
  studyStepStart = 0;
  studyCurrentSteps = 0;
  studyWindowReported = false;
  studyLastOutcome = Controllers::HrStudyOutcome::NotEnoughData;
}

void HeartRateTask::CaptureStudySensorSample(uint16_t hrs, uint16_t als, const Drivers::Bma421::Values& motionValues) {
  studyPpgSum += hrs;
  studyPpgMin = std::min(studyPpgMin, hrs);
  studyPpgMax = std::max(studyPpgMax, hrs);
  studyPpgSamples++;
  studyAmbientLevel = std::max(studyAmbientLevel, static_cast<uint8_t>(std::min<uint16_t>(255, als >> 8)));
  studyCurrentSteps = motionValues.steps;
  if (!studyHasPreviousMotion) {
    studyPreviousX = motionValues.x;
    studyPreviousY = motionValues.y;
    studyPreviousZ = motionValues.z;
    studyStepStart = motionValues.steps;
    studyHasPreviousMotion = true;
    return;
  }
  const int32_t delta = std::abs(static_cast<int32_t>(motionValues.x) - studyPreviousX) +
                        std::abs(static_cast<int32_t>(motionValues.y) - studyPreviousY) +
                        std::abs(static_cast<int32_t>(motionValues.z) - studyPreviousZ);
  studyMotionLevel = std::max(studyMotionLevel,
                              static_cast<uint16_t>(std::min<int32_t>(delta, std::numeric_limits<uint16_t>::max())));
  studyPreviousX = motionValues.x;
  studyPreviousY = motionValues.y;
  studyPreviousZ = motionValues.z;
}

void HeartRateTask::ReportStudyOutcome(Controllers::HrStudyOutcome outcome, uint8_t bpm) {
  if (!studySessionActive || studyWindowReported) {
    return;
  }
  Controllers::HrStudyRecord record {};
  record.sequence = studySequence++;
  record.watchTick = xTaskGetTickCount();
  record.ppgMean = studyPpgSamples == 0 ? 0 : static_cast<uint16_t>(studyPpgSum / studyPpgSamples);
  record.ppgRange = studyPpgSamples == 0 ? 0 : static_cast<uint16_t>(studyPpgMax - studyPpgMin);
  record.motionLevel = studyMotionLevel;
  record.bpm = bpm;
  record.outcome = static_cast<uint8_t>(outcome);
  record.flags = state == States::BackgroundMeasuring ? 0x01 : 0x02;
  record.stepDelta = static_cast<uint8_t>(std::min<uint32_t>(studyCurrentSteps - studyStepStart, std::numeric_limits<uint8_t>::max()));
  record.ambientLevel = studyAmbientLevel;
  record.profileDrive = 1; // PPGv2 estimator.
  studyBuffer.Push(record);
  studyWindowReported = true;
  DrainStudyBuffer();
}

void HeartRateTask::DrainStudyBuffer() {
  if (!studySessionActive || studyBuffer.Empty()) {
    return;
  }
  if (auto* studyService = controller.StudyService(); studyService != nullptr) {
    if (const auto* record = studyBuffer.Front(); record != nullptr) {
      studyService->TryIndicate(*record);
    }
  }
}
#endif
