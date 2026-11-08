#include "components/heartrate/Ppg.h"

#include <cmath>
#include <cstdlib>
#include <iostream>

using Pinetime::Controllers::Ppg;

namespace {
  constexpr double pi = 3.14159265358979323846;
  constexpr uint16_t offset = 10000;

  void Require(bool condition, const char* message) {
    if (!condition) {
      std::cerr << "FAIL: " << message << '\n';
      std::exit(EXIT_FAILURE);
    }
  }

  int Feed(Ppg& ppg, uint16_t sample) {
    ppg.Preprocess(sample, 0);
    return ppg.HeartRate();
  }

  uint16_t PulseSample(int index) {
    // 72 BPM, sampled at the baseline estimator's 10 Hz rate.
    const double time = index * Ppg::deltaTms / 1000.0;
    return static_cast<uint16_t>(std::lround(offset + 100.0 * std::sin(2.0 * pi * 1.2 * time)));
  }
}

int main() {
  Ppg ppg;
  ppg.Reset(true);

  for (int sample = 0; sample < 500; ++sample) {
    const int bpm = Feed(ppg, offset);
    Require(bpm <= 0, "constant ADC input must not produce a heart rate");
    if (sample < Ppg::dataLength - 1) {
      Require(bpm == -2, "the estimator must wait for its full first window");
    }
  }

  // A bad first spectrum must not contaminate every later spectrum. Before
  // the reset-order fix this clean sine never acquired in the 30 s window.
  ppg.Reset(true);
  int firstAccepted = -1;
  for (int sample = 0; sample < 300; ++sample) {
    const int bpm = Feed(ppg, PulseSample(sample));
    if (bpm > 0) {
      if (firstAccepted < 0) {
        firstAccepted = sample;
      }
      Require(std::abs(bpm - 72) <= 4, "the clean 72 BPM pulse must remain near its reference");
    }
  }
  Require(firstAccepted >= 0 && firstAccepted < 100, "the clean pulse must acquire within ten seconds");

  // The baseline smooths several estimates, so allow its existing history
  // to drain after signal loss. A reset request is not a positive BPM.
  bool resetRequested = false;
  for (int sample = 0; sample < 400; ++sample) {
    const int bpm = Feed(ppg, offset);
    if (bpm == -1) {
      resetRequested = true;
      ppg.Reset(false);
    }
    if (sample >= 200) {
      Require(bpm <= 0, "a lost pulse must stop producing heart-rate estimates");
    }
  }
  Require(resetRequested, "signal loss after a valid pulse must request acquisition reset");

  ppg.Reset(true);
  bool reacquired = false;
  for (int sample = 0; sample < 100; ++sample) {
    const int bpm = Feed(ppg, PulseSample(sample));
    if (bpm > 0) {
      Require(std::abs(bpm - 72) <= 4, "a restarted acquisition must retain the pulse frequency");
      reacquired = true;
    }
  }
  Require(reacquired, "a fresh acquisition must recover after signal loss");
  std::cout << "PPG regression passed: flat input, 72 BPM acquisition, signal loss, and reacquisition\n";
}
