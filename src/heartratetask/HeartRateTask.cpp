#include "heartratetask/HeartRateTask.h"
#include <components/heartrate/HeartRateController.h>
#include <drivers/Hrs3300.h>
#include <drivers/Bma421.h>
#include <algorithm>
#include <cstdlib>
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

    // Target system tick is the elapsed sensor ticks multiplied by the sensor tick duration (i.e. the elapsed time)
    // multiplied by the system tick rate
    // Since the sensor tick duration is a whole number of milliseconds, we compute in milliseconds and then divide by 1000
    // To avoid the number of milliseconds overflowing a u32, we take a factor of 2 out of the divisor and dividend
    // (1024 / 2) * 65536 * 100 = 3355443200 which is less than 2^32

    constexpr uint16_t deltaTms = Controllers::Ppg::sampleDuration * 1000;
    // Guard against future tick rate changes
    static_assert((configTICK_RATE_HZ / 2ULL) * (std::numeric_limits<decltype(count)>::max() + 1ULL) * static_cast<uint64_t>((deltaTms)) <
                    std::numeric_limits<uint32_t>::max(),
                  "Overflow");
    TickType_t elapsedTarget = Utility::RoundedDiv(static_cast<uint32_t>(configTICK_RATE_HZ / 2) * (static_cast<uint32_t>(count) + 1U) *
                                                     static_cast<uint32_t>((deltaTms)),
                                                   static_cast<uint32_t>(1000 / 2));

    // On count overflow, reset both count and start time
    // Count is 16bit to avoid overflow in elapsedTarget
    // Count overflows every 100ms * u16 max = ~2 hours, much more often than the tick count (~48 days)
    // So no need to check for tick count overflow
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
      // Sleep until a new event if background measuring disabled
      if (!backgroundPeriod.has_value()) {
        return portMAX_DELAY;
      }
      // Sleep until the next background measurement
      if (currentTime - lastMeasurementTime < backgroundPeriod.value()) {
        return backgroundPeriod.value() - (currentTime - lastMeasurementTime);
      }
      // If one is due now, go straight away
      return 0;
    case States::BackgroundMeasuring:
    case States::ForegroundMeasuring:
      return CalculateSleepTicks();
  }
  // Needed to keep dumb compiler happy, this is unreachable
  // Any new additions to States will cause the above switch statement not to compile, so this is safe
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
  auto* app = static_cast<HeartRateTask*>(instance);
  app->Work();
}

void HeartRateTask::Work() {
  // measurementStartTime is always initialised before use by StartMeasurement
  // Need to initialise lastMeasurementTime so that the first background measurement happens at a reasonable time
  lastMeasurementTime = xTaskGetTickCount();
  valueCurrentlyShown = false;

  // A selected interval is an autonomous schedule. It must not require a
  // hidden first press of Start in the Heart Rate app.
  if (BackgroundMeasurementInterval().has_value()) {
    state = States::Waiting;
  }

  while (true) {
    TickType_t delay = CurrentTaskDelay();
    Messages msg;
    States newState = state;

    if (xQueueReceive(messageQueue, &msg, delay) == pdTRUE) {
      switch (msg) {
        case Messages::GoToSleep:
          // Ignore power state changes when disabled
          if (state == States::Disabled) {
            break;
          }
          if (state == States::ForegroundMeasuring) {
            // A manual measurement belongs to the visible Heart Rate screen.
            // Screen sleep ends that request; a configured interval remains
            // independently armed for later background work.
            manualMeasurementRequested = false;
            if (BackgroundMeasurementNeeded()) {
              newState = States::BackgroundMeasuring;
            } else {
              newState = States::Waiting;
            }
          }
          break;
        case Messages::WakeUp:
          // Ignore power state changes when disabled
          if (state == States::Disabled) {
            break;
          }
          // A scheduled sample must not turn into continuous foreground
          // sampling whenever the screen wakes. Only a manual Start does so.
          if (manualMeasurementRequested) {
            newState = States::ForegroundMeasuring;
          }
          break;
        case Messages::Enable:
          // Can only be enabled when the screen is on
          // If this constraint is somehow violated, the unexpected state
          // will self-resolve at the next screen on event
          manualMeasurementRequested = true;
          newState = States::ForegroundMeasuring;
          valueCurrentlyShown = false;
          break;
        case Messages::Disable:
          manualMeasurementRequested = false;
          // The setting owns the autonomous schedule. Stop the current manual
          // sample, but keep a configured interval armed; choose Off in
          // Settings to disable automatic sampling.
          if (BackgroundMeasurementInterval().has_value()) {
            lastMeasurementTime = xTaskGetTickCount();
            newState = States::Waiting;
          } else {
            newState = States::Disabled;
          }
          break;
        case Messages::BackgroundSettingsChanged:
          if (BackgroundMeasurementInterval().has_value()) {
            // Apply Off -> interval immediately, but let a new schedule wait
            // one full period before its first background sample.
            if (state == States::Disabled && !manualMeasurementRequested) {
              lastMeasurementTime = xTaskGetTickCount();
              newState = States::Waiting;
            }
          } else if (!manualMeasurementRequested) {
            newState = States::Disabled;
          }
          break;
      }
    }
    if (newState == States::Waiting && BackgroundMeasurementNeeded()) {
      newState = States::BackgroundMeasuring;
    } else if (newState == States::BackgroundMeasuring && !BackgroundMeasurementNeeded()) {
      newState = States::Waiting;
    }

    // Apply state transition (switch sensor on/off)
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
}

void HeartRateTask::StopMeasurement() {
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
  auto sensorData = heartRateSensor.ReadHrsAls();
  if (!sensorData.valid) {
    ppg.Reset();
    controller.UpdateState(ControllerStates::SensorError);
    valueCurrentlyShown = false;
    HandleMeasurementTimeout(false);
    return;
  }

  auto motionValues = motionSensor.Process();
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
      break;
    case Drivers::Hrs3300::PPGState::Reset:
      ppg.Reset();
      SendHeartRate(ControllerStates::NotEnoughData, 0);
      break;
    case Drivers::Hrs3300::PPGState::Running:
      bpm = ppg.HeartRate();
      if (bpm.has_value()) {
        SendHeartRate(ControllerStates::Measuring, bpm.value());
      } else if (ppg.SufficientData()) {
        controller.UpdateState(ControllerStates::Searching);
      } else {
        controller.UpdateState(ControllerStates::NotEnoughData);
      }
      break;
    case Drivers::Hrs3300::PPGState::Off:
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
  // If been measuring for longer than the time limit, set the last measurement time.
  // This allows giving up on background measurement after a while
  // and also means that background measurement won't begin immediately after
  // an unsuccessful long foreground measurement
  const auto now = xTaskGetTickCount();
  if (now - measurementStartTime >= backgroundMeasurementTimeLimit) {
    // When measuring, propagate failure if no value within the time limit
    // Prevents stale heart rates from being displayed for >1 background period
    // Or more than the time limit after switching to screen on (where the last background measurement was successful)
    // Note: Once a successful measurement is recorded in screen on it will never be cleared
    // without some other state change e.g. ambient light reset
    if (!valueCurrentlyShown && reportSignalFailure) {
      controller.UpdateState(ControllerStates::SignalUnstable);
      valueCurrentlyShown = false;
    }
    if (state == States::BackgroundMeasuring) {
      const auto backgroundPeriod = BackgroundMeasurementInterval();
      // Preserve start-to-start cadence for intervals longer than one sample
      // window. At 30 seconds (or Continuous), using the original start time
      // would make a failed sample immediately restart forever.
      lastMeasurementTime = backgroundPeriod.has_value() && backgroundPeriod.value() > backgroundMeasurementTimeLimit
                              ? measurementStartTime
                              : now;
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
