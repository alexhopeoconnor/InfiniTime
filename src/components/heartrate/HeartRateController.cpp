#include "components/heartrate/HeartRateController.h"
#include <heartratetask/HeartRateTask.h>
#include <systemtask/SystemTask.h>

using namespace Pinetime::Controllers;

void HeartRateController::UpdateState(HeartRateController::States newState) {
  this->state = newState;
}

void HeartRateController::UpdateHeartRate(uint8_t heartRate) {
  // A zero is a failed PPG estimate, not a physiological reading.  Preserve
  // the last verified value and let screens present it as stale or unavailable.
  if (heartRate != 0 && this->heartRate != heartRate) {
    this->heartRate = heartRate;
    hasValidHeartRate = true;
    lastValidHeartRateTick = xTaskGetTickCount();
    if (service != nullptr) {
      service->OnNewHeartRateValue(heartRate);
    }
  } else if (heartRate != 0) {
    hasValidHeartRate = true;
    lastValidHeartRateTick = xTaskGetTickCount();
  }
}

void HeartRateController::SetMeasurementMode(HeartRateController::MeasurementMode newMode) {
  measurementMode = newMode;
}

void HeartRateController::Enable() {
  if (task != nullptr) {
    state = States::NotEnoughData;
    measurementMode = MeasurementMode::Foreground;
    task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::Enable);
  }
}

void HeartRateController::Disable() {
  if (task != nullptr) {
    state = States::Disabled;
    measurementMode = MeasurementMode::Idle;
    task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::Disable);
  }
}

void HeartRateController::OnBackgroundSettingsChanged() {
  if (task != nullptr) {
    task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::BackgroundSettingsChanged);
  }
}

void HeartRateController::SetHeartRateTask(Pinetime::Applications::HeartRateTask* task) {
  this->task = task;
}

void HeartRateController::SetService(Pinetime::Controllers::HeartRateService* service) {
  this->service = service;
}
