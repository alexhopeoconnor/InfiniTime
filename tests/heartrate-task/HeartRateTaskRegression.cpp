// Compile the production HeartRateTask against deterministic RTOS, sensor,
// estimator, and controller fakes. These check task orchestration; the real
// estimator has its own numerical regressions.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <deque>
#include <vector>
#include <heartratetask/HeartRateTask.h>
#include <components/battery/BatteryController.h>
#include <components/heartrate/HeartRateController.h>
#include <drivers/Hrs3300.h>

using Pinetime::Applications::HeartRateTask;
using Messages = HeartRateTask::Messages;
using Controller = Pinetime::Controllers::HeartRateController;

namespace {
  struct EndRun {};
  struct Event { TickType_t at; Messages message; };
  TickType_t now = 0;
  TickType_t stopAt = 700;
  bool interruptContext = false;
  unsigned taskSends = 0;
  unsigned isrSends = 0;
  unsigned isrYields = 0;
  std::deque<Event> events;

  void Reset() {
    now = 0;
    stopAt = 700;
    interruptContext = false;
    SCB->ICSR = 0;
    taskSends = isrSends = isrYields = 0;
    events.clear();
    TestPpg::results.clear();
    TestPpg::index = TestPpg::partialResets = 0;
  }

  void Run(HeartRateTask& task) {
    task.Start();
    try { task.Work(); } catch (EndRun&) {}
  }

  std::vector<TickType_t> Measure(bool messages, TickType_t readCost = 0) {
    Reset();
    events.push_back({0, Messages::Enable});
    if (messages) {
      for (TickType_t tick = 101; tick < stopAt; ++tick) events.push_back({tick, Messages::WakeUp});
    }
    Pinetime::Drivers::Hrs3300 sensor;
    sensor.readCost = readCost;
    Controller controller;
    Pinetime::Controllers::Settings settings;
    Pinetime::Controllers::Battery battery;
    HeartRateTask task {sensor, controller, settings, battery};
    Run(task);
    return sensor.reads;
  }
}

TickType_t xTaskGetTickCount() { return now; }
void vTaskDelay(TickType_t delay) { now += delay; }
void TestYieldFromIsr(BaseType_t) { ++isrYields; }
BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t delay) {
  assert(!interruptContext && delay == 0);
  ++taskSends;
  return pdPASS;
}
BaseType_t xQueueSendFromISR(QueueHandle_t, const void*, BaseType_t* woken) {
  assert(interruptContext);
  *woken = pdTRUE;
  ++isrSends;
  return pdPASS;
}
BaseType_t xQueueReceive(QueueHandle_t, void* destination, TickType_t delay) {
  const uint64_t deadline = static_cast<uint64_t>(now) + delay;
  if (!events.empty() && events.front().at <= deadline && events.front().at <= stopAt) {
    const auto event = events.front();
    events.pop_front();
    now = std::max(now, event.at);
    *static_cast<Messages*>(destination) = event.message;
    return pdTRUE;
  }
  if (deadline > stopAt) throw EndRun {};
  now = static_cast<TickType_t>(deadline);
  return pdFALSE;
}

int main() {
  const std::vector<TickType_t> expected {100, 202, 305, 407, 510, 612};
  assert(Measure(false) == expected);
  assert(Measure(true) == expected); // A message every tick must not accelerate DAQ.
  assert(Measure(true, 7) == expected); // Sensor/processing time is inside the period.
  assert((Measure(false, 200) == std::vector<TickType_t> {100, 300, 500, 700}));

  Pinetime::Applications::SamplingCadence<1024, 100> cadence;
  cadence.Start(UINT32_MAX - 10);
  cadence.Sampled(UINT32_MAX - 10);
  assert(cadence.Delay(0) == 91); // Tick wrap is unsigned elapsed time.
  assert(cadence.Delay(91) == 0);
  cadence.Sampled(91);
  assert(cadence.Delay(1000) == 0); // A long scheduler stall produces one due read.
  cadence.Sampled(1000);
  assert(cadence.Delay(1000) >= 102); // No immediate catch-up read.
  cadence.Start(0);
  uint32_t tick = 0;
  for (unsigned sample = 0; sample < 100000; ++sample) {
    cadence.Sampled(tick);
    tick += cadence.Delay(tick);
  }
  assert(tick == 10240000); // Fractional ticks retain exactly 10 Hz over time.

  Reset();
  events.push_back({0, Messages::Enable});
  TestPpg::results = {-1, -2, 0, 75};
  Pinetime::Drivers::Hrs3300 sensor;
  Controller controller;
  Pinetime::Controllers::Settings settings;
  Pinetime::Controllers::Battery battery;
  HeartRateTask task {sensor, controller, settings, battery};
  Run(task);
  unsigned accepted = 0;
  for (const auto& update : controller.updates) {
    if (update.first == Controller::States::Running) {
      assert(update.second == 75); // The -1 sentinel must never wrap to 255 BPM.
      ++accepted;
    }
  }
  assert(accepted == 1 && TestPpg::partialResets == 1);
  task.PushMessage(Messages::WakeUp);
  interruptContext = true;
  SCB->ICSR = 1;
  task.PushMessage(Messages::WakeUp);
  assert(taskSends == 1 && isrSends == 1 && isrYields == 1);

  Reset();
  stopAt = 32000;
  settings.interval = 30;
  TestPpg::results = {75};
  Pinetime::Drivers::Hrs3300 backgroundSensor;
  Controller backgroundController;
  HeartRateTask background {backgroundSensor, backgroundController, settings, battery};
  Run(background);
  assert((backgroundSensor.reads == std::vector<TickType_t> {30820}));
  assert(backgroundSensor.enables == 1 && backgroundSensor.disables == 1);

  Reset();
  stopAt = 65000;
  Pinetime::Drivers::Hrs3300 failedSensor;
  Controller failedController;
  HeartRateTask failed {failedSensor, failedController, settings, battery};
  Run(failed);
  assert(failedSensor.reads.size() == 301);
  assert(failedSensor.reads.back() - failedSensor.reads.front() == 30 * configTICK_RATE_HZ);
  assert(failedSensor.enables == 1 && failedSensor.disables == 1);
  assert(std::none_of(failedController.updates.begin(), failedController.updates.end(), [](const auto& update) {
    return update.first == Controller::States::Running;
  }));

  Reset();
  events.push_back({0, Messages::Enable});
  battery.powered = true;
  Pinetime::Drivers::Hrs3300 dockedSensor;
  Controller dockedController;
  HeartRateTask docked {dockedSensor, dockedController, settings, battery};
  Run(docked);
  assert(dockedSensor.reads.empty() && dockedSensor.enables == 0);
  std::puts("HeartRateTask regressions passed: cadence, wakeups, overruns, tick wrap, sentinel, queue context, background, timeout, dock.");
}
