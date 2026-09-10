#pragma once

#include "drivers/TwiMaster.h"
#include <cstdint>

namespace Pinetime {
  namespace Drivers {
    class Hrs3300 {
    public:
      enum class Registers : uint8_t {
        Id = 0x00,
        Enable = 0x01,
        C1DataM = 0x08,
        C0DataM = 0x09,
        C0DataH = 0x0a,
        PDriver = 0x0c,
        C1DataH = 0x0d,
        C1DataL = 0x0e,
        C0DataL = 0x0f,
        Res = 0x16,
        HGain = 0x17
      };
      // Reset is returned to the task when contact resumes after low-power
      // no-touch sampling, so the estimator can discard its tracking state.
      enum class PPGState : uint8_t { Off, Running, NoTouch, Reset };

      struct PackedHrsAls {
        uint16_t hrs = 0;
        uint16_t als = 0;
        bool valid = false;
      };

      Hrs3300(TwiMaster& twiMaster, uint8_t twiAddress);
      Hrs3300(const Hrs3300&) = delete;
      Hrs3300& operator=(const Hrs3300&) = delete;
      Hrs3300(Hrs3300&&) = delete;
      Hrs3300& operator=(Hrs3300&&) = delete;

      void Init();
      void Enable();
      void Disable();
      PackedHrsAls ReadHrsAls();
      PPGState AutoGain(uint16_t hrsAvg, uint16_t currentAls);
      // Compact current state for temporary study telemetry: drive (bits
      // 0-2), gain (bits 3-4), and PPG state (bits 5-7).
      [[nodiscard]] uint8_t StudyConfiguration() const;

    private:
      enum class PPGDrive : uint8_t {
        NoTouchPowerSave,
        // Special registers set
        Current13mALow,
        Current13mA,
        Current20mA,
        Current30mA,
        Current40mA,
        // Special registers set
        Current40mAHigh,
      };
      enum class PPGGain : uint8_t { Gain1x, Gain2x, Gain4x, Gain8x };
      TwiMaster& twiMaster;
      uint8_t twiAddress;

      uint8_t r7f = 0;
      uint8_t r80 = 0;
      uint8_t r81 = 0;
      uint8_t r82 = 0;
      PPGDrive ppgDrive = PPGDrive::NoTouchPowerSave;
      PPGGain ppgGain = PPGGain::Gain8x;
      PPGState ppgState = PPGState::Off;

      uint8_t actionDelay = 0;

      uint8_t hrsLowCount = 0;
      uint8_t hrsHighCount = 0;
      uint16_t alsLowCount = 0;
      uint8_t alsHighCount = 0;

      void WriteRegister(uint8_t reg, uint8_t data);
      uint8_t ReadRegister(uint8_t reg);
      void SetPower();
    };
  }
}
