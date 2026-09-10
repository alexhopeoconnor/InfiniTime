#pragma once

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>

namespace Pinetime::Controllers {

  // Protocol revision 4 for the temporary ElixirTime HR-study service. Every
  // frame fits in the 20-byte payload available before an ATT MTU exchange,
  // and carries an integrity check. A completed measurement is sent as a
  // summary followed by its matching diagnostic frame; the host only ACKs
  // after it has durably recorded both.
  constexpr uint8_t HrStudyProtocolMarker = 0xe4;
  constexpr uint8_t HrStudyFrameKindMask = 0xe0;
  constexpr uint8_t HrStudyFrameFlagsMask = 0x1f;

  enum class HrStudyFrameKind : uint8_t {
    Summary = 0x01,
    Diagnostics = 0x02,
  };

  enum HrStudyFlags : uint8_t {
    HrStudyBackgroundMeasurement = 0x01,
    HrStudyForegroundMeasurement = 0x02,
    HrStudyPowerPresent = 0x04,
    HrStudyPpgV2 = 0x08,
  };

  inline uint8_t HrStudyFrameHeader(HrStudyFrameKind kind, uint8_t flags = 0) {
    return (static_cast<uint8_t>(kind) << 5U) | (flags & HrStudyFrameFlagsMask);
  }

  // CRC-8/ATM, with the checksum byte itself treated as zero. This guards
  // against accepting a 20-byte stale or corrupted ATT payload as study data.
  inline uint8_t HrStudyChecksum(const uint8_t* bytes, size_t length) {
    uint8_t crc = 0;
    for (size_t index = 0; index < length; index++) {
      const uint8_t value = index == 2 ? 0 : bytes[index];
      crc ^= value;
      for (uint8_t bit = 0; bit < 8; bit++) {
        crc = (crc & 0x80U) == 0 ? static_cast<uint8_t>(crc << 1U) : static_cast<uint8_t>((crc << 1U) ^ 0x07U);
      }
    }
    return crc;
  }

  // Summary frame for one completed measurement window.
  struct __attribute__((packed)) HrStudyRecord {
    uint8_t marker = HrStudyProtocolMarker;
    uint8_t kindAndFlags = HrStudyFrameHeader(HrStudyFrameKind::Summary);
    uint8_t checksum = 0;
    uint16_t sequence = 0;
    uint32_t watchTick = 0;
    uint16_t ppgMean = 0;
    uint16_t ppgRange = 0;
    uint16_t motionLevel = 0;
    uint8_t bpm = 0;
    uint8_t outcome = 0;
    // HRS3300 state: drive (bits 0-2), gain (bits 3-4), and PPG state (bits
    // 5-6). Bit 7 is reserved.
    uint8_t sensorConfiguration = 0;
    uint16_t ambientMaximum = 0;
  };

  static_assert(sizeof(HrStudyRecord) == 20);

  // Diagnostic frame paired with HrStudyRecord by sequence. Numeric PPG
  // energies are signed tenths of a natural-log energy; INT8_MIN represents
  // a non-finite or unavailable value. `lockedBin` is 0xff when unlocked.
  struct __attribute__((packed)) HrStudyDiagnostics {
    uint8_t marker = HrStudyProtocolMarker;
    uint8_t kindAndFlags = HrStudyFrameHeader(HrStudyFrameKind::Diagnostics);
    uint8_t checksum = 0;
    uint16_t sequence = 0;
    uint16_t sampleCount = 0;
    uint8_t analysisCount = 0;
    uint8_t strongestBin = 0;
    uint8_t lockedBin = 0xff;
    int8_t strongestEnergyTenths = INT8_MIN;
    int8_t lockedEnergyTenths = INT8_MIN;
    int8_t rollingEnergyTenths = INT8_MIN;
    // bit 0: enough samples; bit 1: strong candidate; bit 2: lock active.
    uint8_t ppgFlags = 0;
    uint8_t sensorTransitions = 0;
    uint8_t queueDrops = 0;
    uint16_t stackHighWaterWords = 0;
    uint16_t minimumFreeHeapBytes = 0;
  };

  static_assert(sizeof(HrStudyDiagnostics) == 20);

  struct HrStudySample {
    HrStudyRecord record {};
    HrStudyDiagnostics diagnostics {};
  };

  inline void FinalizeHrStudyRecord(HrStudyRecord& record) {
    record.checksum = 0;
    record.checksum = HrStudyChecksum(reinterpret_cast<const uint8_t*>(&record), sizeof(record));
  }

  inline void FinalizeHrStudyDiagnostics(HrStudyDiagnostics& diagnostics) {
    diagnostics.checksum = 0;
    diagnostics.checksum = HrStudyChecksum(reinterpret_cast<const uint8_t*>(&diagnostics), sizeof(diagnostics));
  }

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
    CaptureComplete,
    Sending,
    AwaitingAcknowledgement,
    Connected,
  };

  template <size_t Capacity>
  class HrStudyBuffer {
  public:
    void Push(const HrStudySample& sample) {
      const size_t tail = (head + count) % Capacity;
      records[tail] = sample;
      if (count == Capacity) {
        head = (head + 1) % Capacity;
      } else {
        count++;
      }
    }

    const HrStudySample* Front() const {
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
    std::array<HrStudySample, Capacity> records {};
    size_t head = 0;
    size_t count = 0;
  };
}
