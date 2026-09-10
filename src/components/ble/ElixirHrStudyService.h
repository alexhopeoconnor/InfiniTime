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
    enum class Commands : uint8_t { Start = 0x01, Stop = 0x02, Flush = 0x03, Acknowledge = 0x04 };
    static constexpr uint16_t MaxCaptureDurationSeconds = 45 * 60;

    ElixirHrStudyService(NimbleController& nimble, HeartRateController& heartRateController);
    void Init();
    int OnAccess(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context);

    void UpdateIndicationSubscription(uint16_t attributeHandle, bool enabled);
    void OnDisconnected();
    void OnIndicationComplete(const ble_gap_event& event);

    void SetSessionActive(bool active);
    void SetCaptureComplete(bool complete);
    bool TryIndicate(const HrStudySample& sample);
    bool ConsumeAcknowledgement(uint32_t& sequence);
    void CompleteAcknowledgement(bool accepted);
    HrStudyTransportState TransportState() const;

  private:
    NimbleController& nimble;
    HeartRateController& heartRateController;

    struct ble_gatt_chr_def characteristicDefinition[4];
    struct ble_gatt_svc_def serviceDefinition[2];
    uint16_t controlHandle = 0;
    uint16_t recordHandle = 0;
    uint16_t diagnosticsHandle = 0;

    std::atomic_bool sessionActive {false};
    std::atomic_bool captureComplete {false};
    std::atomic_bool recordIndicationEnabled {false};
    std::atomic_bool diagnosticsIndicationEnabled {false};
    std::atomic_bool indicationInFlight {false};
    std::atomic_bool diagnosticsFramePending {false};
    std::atomic_bool awaitingHostAcknowledgement {false};
    std::atomic_bool acknowledgementPending {false};
    std::atomic_uint32_t pendingSequence {0};
    std::atomic_uint32_t acknowledgedSequence {0};
    std::atomic_uint16_t indicationConnectionHandle {BLE_HS_CONN_HANDLE_NONE};
    std::atomic<HrStudyFrameKind> indicationFrameKind {HrStudyFrameKind::Summary};
  };
}
