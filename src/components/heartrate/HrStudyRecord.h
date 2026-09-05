#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {

  // Stable wire format for the temporary ElixirTime HR-study service. It fits
  // in the 20-byte payload available before an ATT MTU exchange, so a record
  // can never require a fragmented BLE indication.
  struct __attribute__((packed)) HrStudyRecord {
    uint32_t sequence = 0;
    uint32_t watchTick = 0;
    uint16_t ppgMean = 0;
    uint16_t ppgRange = 0;
    uint16_t motionLevel = 0;
    uint8_t bpm = 0;
    uint8_t outcome = 0;
    uint8_t flags = 0;
    uint8_t stepDelta = 0;
    uint8_t ambientLevel = 0;
    uint8_t profileDrive = 0;
  };

  static_assert(sizeof(HrStudyRecord) == 20);

  enum class HrStudyOutcome : uint8_t {
    Accepted = 1,
    NotEnoughData = 2,
    SignalUnstable = 3,
    AmbientLight = 4,
    NoTouch = 5,
    SensorError = 6,
    Interrupted = 7,
  };

  enum class HrStudyTransportState : uint8_t {
    Off,
    Buffering,
    Sending,
    Connected,
  };

  template <size_t Capacity>
  class HrStudyBuffer {
  public:
    void Push(const HrStudyRecord& record) {
      const size_t tail = (head + count) % Capacity;
      records[tail] = record;
      if (count == Capacity) {
        head = (head + 1) % Capacity;
      } else {
        count++;
      }
    }

    const HrStudyRecord* Front() const {
      return count == 0 ? nullptr : &records[head];
    }

    void PopFront() {
      if (count != 0) {
        head = (head + 1) % Capacity;
        count--;
      }
    }

    void Clear() {
      head = 0;
      count = 0;
    }

    bool Empty() const {
      return count == 0;
    }

  private:
    std::array<HrStudyRecord, Capacity> records {};
    size_t head = 0;
    size_t count = 0;
  };
}
