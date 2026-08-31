#include "components/heartrate/HeartRateController.h"
#include <heartratetask/HeartRateTask.h>
#include <systemtask/SystemTask.h>

using namespace Pinetime::Controllers;

void HeartRateController::Update(HeartRateController::States newState, uint8_t heartRate) {
  this->state = newState;
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

void HeartRateController::Enable() {
  if (task != nullptr) {
    state = States::NotEnoughData;
    task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::Enable);
  }
}

void HeartRateController::Disable() {
  if (task != nullptr) {
    state = States::Stopped;
    task->PushMessage(Pinetime::Applications::HeartRateTask::Messages::Disable);
  }
}

void HeartRateController::SetHeartRateTask(Pinetime::Applications::HeartRateTask* task) {
  this->task = task;
}

void HeartRateController::SetService(Pinetime::Controllers::HeartRateService* service) {
  this->service = service;
}
