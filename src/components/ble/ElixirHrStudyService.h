#pragma once

#include <atomic>
#include <cstdint>

#define min // workaround: NimBLE's min/max macros conflict with libstdc++
#define max
#include <host/ble_gap.h>
#undef max
#undef min

#include "components/heartrate/HrStudyRecord.h"

namespace Pinetime::Controllers {
  class HeartRateController;
  class NimbleController;

  class ElixirHrStudyService {
  public:
    enum class Commands : uint8_t { Start = 0x01, Stop = 0x02 };
    enum class IndicationCompletion : uint8_t { None, Confirmed, Failed };

    ElixirHrStudyService(NimbleController& nimble, HeartRateController& heartRateController);
    void Init();
    int OnAccess(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context);

    void UpdateIndicationSubscription(uint16_t attributeHandle, bool enabled);
    void OnDisconnected();
    void OnIndicationComplete(const ble_gap_event& event);

    void SetSessionActive(bool active);
    bool TryIndicate(const HrStudyRecord& record);
    IndicationCompletion ConsumeIndicationCompletion();
    HrStudyTransportState TransportState() const;

  private:
    NimbleController& nimble;
    HeartRateController& heartRateController;

    struct ble_gatt_chr_def characteristicDefinition[3];
    struct ble_gatt_svc_def serviceDefinition[2];
    uint16_t controlHandle = 0;
    uint16_t recordHandle = 0;

    std::atomic_bool sessionActive {false};
    std::atomic_bool indicationEnabled {false};
    std::atomic_bool indicationInFlight {false};
    std::atomic<IndicationCompletion> completion {IndicationCompletion::None};
  };
}
