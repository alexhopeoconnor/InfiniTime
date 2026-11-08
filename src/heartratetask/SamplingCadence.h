#pragma once

#include <cstdint>

namespace Pinetime::Applications {
  // Schedule from actual reads, not queue wakeups. Carry fractional ticks so
  // a 100 ms period at 1024 Hz alternates between 102 and 103 ticks, without
  // accumulating rounding error or issuing catch-up reads after a delay.
  template <uint32_t TickRate, uint32_t PeriodMs>
  class SamplingCadence {
  public:
    void Start(uint32_t now) {
      lastSample = now;
      interval = 0;
      remainder = 500;
    }

    uint32_t Delay(uint32_t now) const {
      const auto elapsed = now - lastSample;
      return elapsed < interval ? interval - elapsed : 0;
    }

    void Sampled(uint32_t now) {
      lastSample = now;
      const auto ticks = TickRate * PeriodMs + remainder;
      interval = ticks / 1000;
      remainder = ticks % 1000;
    }

  private:
    static_assert(static_cast<uint64_t>(TickRate) * PeriodMs >= 1000, "Sampling period must be at least one tick");
    static_assert(static_cast<uint64_t>(TickRate) * PeriodMs + 999 <= UINT32_MAX, "Sampling period overflows");
    uint32_t lastSample = 0;
    uint32_t interval = 0;
    uint32_t remainder = 500;
  };
}
