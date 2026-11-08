#pragma once
#include <cstdint>
#include <utility>
#include <vector>

namespace Pinetime::Applications { class HeartRateTask; }
namespace Pinetime::Controllers {
  class HeartRateController {
  public:
    enum class States { NotEnoughData, Stopped, SensorError, AmbientLight, SignalUnstable, Running };
    enum class MeasurementMode { Background, Foreground, Idle };
    std::vector<std::pair<States, uint8_t>> updates;
    void SetHeartRateTask(Applications::HeartRateTask*) {}
    void Update(States state, uint8_t bpm) { updates.emplace_back(state, bpm); }
    void SetMeasurementMode(MeasurementMode) {}
  };
}
