#pragma once
#include <cstdint>
#include <vector>

namespace TestPpg {
  inline std::vector<int> results;
  inline unsigned index = 0;
  inline unsigned partialResets = 0;
}
namespace Pinetime::Controllers {
  class Ppg {
  public:
    static constexpr int deltaTms = 100;
    int8_t Preprocess(uint16_t, uint16_t) { return 0; }
    int HeartRate() { return TestPpg::index < TestPpg::results.size() ? TestPpg::results[TestPpg::index++] : 0; }
    void Reset(bool all) { if (!all) ++TestPpg::partialResets; }
  };
}
