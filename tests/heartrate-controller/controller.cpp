#include <cassert>
#include <cstdint>
#include <iostream>
#include <limits>

#include "components/heartrate/HeartRateController.h"

namespace {
  TickType_t testTick = 0;
}

TickType_t xTaskGetTickCount() {
  return testTick;
}

int main() {
  using Controller = Pinetime::Controllers::HeartRateController;
  using Status = Controller::ReadingStatus;
  using State = Controller::States;

  Controller controller;
  Pinetime::Controllers::HeartRateService service;
  controller.SetService(&service);
  assert(!controller.HasValidHeartRate());
  assert(controller.LastValidHeartRateAgeSeconds(999999) == 0);
  assert(controller.GetReadingStatus(999999) == Status::Unavailable);

  testTick = 50 * configTICK_RATE_HZ;
  controller.Update(State::Running, 72);
  const auto firstTick = testTick;
  assert(controller.HasValidHeartRate());
  assert(service.notifications == 1);
  assert(controller.LastValidHeartRateAgeSeconds(firstTick + 34 * configTICK_RATE_HZ) == 34);
  assert(controller.GetReadingStatus(firstTick + 30 * configTICK_RATE_HZ) == Status::Fresh);
  assert(controller.GetReadingStatus(firstTick + 30 * configTICK_RATE_HZ + 1) == Status::Stale);
  assert(controller.GetReadingStatus(firstTick + 120 * configTICK_RATE_HZ) == Status::Stale);
  assert(controller.GetReadingStatus(firstTick + 120 * configTICK_RATE_HZ + 1) == Status::Unavailable);

  // An equal new estimate refreshes the age even though BLE suppresses duplicates.
  testTick += 40 * configTICK_RATE_HZ;
  controller.Update(State::Running, 72);
  assert(controller.LastValidHeartRateTick() == testTick);
  assert(controller.LastValidHeartRateAgeSeconds(testTick) == 0);
  assert(controller.GetReadingStatus(testTick) == Status::Fresh);
  assert(service.notifications == 1);

  // Failed acquisitions retain a value but must not refresh its timestamp.
  const auto validTick = testTick;
  for (const auto state : {State::NotEnoughData, State::NoTouch, State::SignalUnstable, State::AmbientLight, State::SensorError}) {
    testTick += 30 * configTICK_RATE_HZ;
    controller.Update(state, 0);
    assert(controller.HeartRate() == 72);
    assert(controller.LastValidHeartRateTick() == validTick);
    assert(controller.State() == state);
  }
  assert(controller.LastValidHeartRateAgeSeconds(testTick) == 150);
  assert(controller.GetReadingStatus(testTick) == Status::Unavailable);
  assert(service.notifications == 1);

  // Tick rollover must not turn a recent reading into an expired one.
  testTick = std::numeric_limits<TickType_t>::max() - 5 * configTICK_RATE_HZ;
  controller.Update(State::Running, 73);
  const TickType_t afterWrap = testTick + 34 * configTICK_RATE_HZ;
  assert(controller.LastValidHeartRateAgeSeconds(afterWrap) == 34);
  assert(controller.GetReadingStatus(afterWrap) == Status::Stale);
  assert(service.notifications == 2);

  std::cout << "Heart-rate age, expiry, refresh, failure retention and tick rollover passed\n";
}
