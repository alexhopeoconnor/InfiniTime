#include "components/ble/ElixirHrStudyService.h"

#include <host/ble_hs.h>
#include <nrf_log.h>

#include "components/ble/NimbleController.h"
#include "components/heartrate/HeartRateController.h"

using namespace Pinetime::Controllers;

namespace {
  // b1e3c2d0-6f14-4d72-9b91-48c4d1a0e401: private, temporary study service.
  constexpr ble_uuid128_t studyServiceUuid {
    .u = {.type = BLE_UUID_TYPE_128},
    .value = {0x01, 0xe4, 0xa0, 0xd1, 0xc4, 0x48, 0x91, 0x9b, 0x72, 0x4d, 0x14, 0x6f, 0xd0, 0xc2, 0xe3, 0xb1},
  };
  constexpr ble_uuid128_t controlUuid {
    .u = {.type = BLE_UUID_TYPE_128},
    .value = {0x02, 0xe4, 0xa0, 0xd1, 0xc4, 0x48, 0x91, 0x9b, 0x72, 0x4d, 0x14, 0x6f, 0xd0, 0xc2, 0xe3, 0xb1},
  };
  constexpr ble_uuid128_t recordUuid {
    .u = {.type = BLE_UUID_TYPE_128},
    .value = {0x03, 0xe4, 0xa0, 0xd1, 0xc4, 0x48, 0x91, 0x9b, 0x72, 0x4d, 0x14, 0x6f, 0xd0, 0xc2, 0xe3, 0xb1},
  };

  int StudyServiceCallback(uint16_t connectionHandle, uint16_t attributeHandle, ble_gatt_access_ctxt* context, void* arg) {
    auto* service = static_cast<ElixirHrStudyService*>(arg);
    return service->OnAccess(connectionHandle, attributeHandle, context);
  }
}

ElixirHrStudyService::ElixirHrStudyService(NimbleController& nimble, HeartRateController& heartRateController)
  : nimble {nimble},
    heartRateController {heartRateController},
    characteristicDefinition {
      {.uuid = &controlUuid.u,
       .access_cb = StudyServiceCallback,
       .arg = this,
       .flags = BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_ENC,
       .val_handle = &controlHandle},
      {.uuid = &recordUuid.u,
       .access_cb = StudyServiceCallback,
       .arg = this,
       .flags = BLE_GATT_CHR_F_INDICATE,
       .val_handle = &recordHandle},
      {0},
    },
    serviceDefinition {
      {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &studyServiceUuid.u, .characteristics = characteristicDefinition},
      {0},
    } {
  heartRateController.SetStudyService(this);
}

void ElixirHrStudyService::Init() {
  int result = ble_gatts_count_cfg(serviceDefinition);
  ASSERT(result == 0);
  result = ble_gatts_add_svcs(serviceDefinition);
  ASSERT(result == 0);
}

int ElixirHrStudyService::OnAccess(uint16_t /*connectionHandle*/, uint16_t attributeHandle, ble_gatt_access_ctxt* context) {
  if (attributeHandle != controlHandle || context->op != BLE_GATT_ACCESS_OP_WRITE_CHR) {
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (!indicationEnabled) {
    // This older NimBLE snapshot has no symbolic constant for the Bluetooth
    // SIG's "CCCD improperly configured" application error. An unlikely
    // error still rejects the write until the client subscribes, without
    // introducing a private ATT error code into the temporary protocol.
    return BLE_ATT_ERR_UNLIKELY;
  }
  if (OS_MBUF_PKTLEN(context->om) != 1) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }

  uint8_t command = 0;
  if (os_mbuf_copydata(context->om, 0, sizeof(command), &command) != 0) {
    return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
  }
  switch (static_cast<Commands>(command)) {
    case Commands::Start:
      heartRateController.RequestStudyStart();
      return 0;
    case Commands::Stop:
      heartRateController.RequestStudyStop();
      return 0;
  }
  return BLE_ATT_ERR_UNLIKELY;
}

void ElixirHrStudyService::UpdateIndicationSubscription(uint16_t attributeHandle, bool enabled) {
  if (attributeHandle != recordHandle) {
    return;
  }
  indicationEnabled = enabled;
  if (!enabled) {
    indicationInFlight = false;
    completion = IndicationCompletion::None;
    return;
  }
  heartRateController.RequestStudyFlush();
}

void ElixirHrStudyService::OnDisconnected() {
  indicationEnabled = false;
  indicationInFlight = false;
  completion = IndicationCompletion::None;
}

void ElixirHrStudyService::OnIndicationComplete(const ble_gap_event& event) {
  if (!event.notify_tx.indication || event.notify_tx.attr_handle != recordHandle) {
    return;
  }
  // NimBLE first reports a successful send (status 0), then reports either
  // BLE_HS_EDONE for the peer's ATT confirmation or an error/timeout.
  if (event.notify_tx.status == 0) {
    return;
  }

  indicationInFlight = false;
  completion = event.notify_tx.status == BLE_HS_EDONE ? IndicationCompletion::Confirmed : IndicationCompletion::Failed;
  if (event.notify_tx.status != BLE_HS_EDONE) {
    indicationEnabled = false;
  }
  heartRateController.RequestStudyIndicationComplete();
}

void ElixirHrStudyService::SetSessionActive(bool active) {
  sessionActive = active;
  if (!active) {
    indicationInFlight = false;
    completion = IndicationCompletion::None;
  }
}

bool ElixirHrStudyService::TryIndicate(const HrStudyRecord& record) {
  if (!sessionActive || !indicationEnabled || indicationInFlight) {
    return false;
  }
  const uint16_t connectionHandle = nimble.connHandle();
  // Connection handle zero is valid for the first BLE connection.
  if (connectionHandle == BLE_HS_CONN_HANDLE_NONE) {
    return false;
  }

  auto* om = ble_hs_mbuf_from_flat(&record, sizeof(record));
  if (om == nullptr) {
    return false;
  }
  const int result = ble_gattc_indicate_custom(connectionHandle, recordHandle, om);
  if (result != 0) {
    NRF_LOG_WARNING("HR study indication queue failed: %d", result);
    return false;
  }
  indicationInFlight = true;
  return true;
}

ElixirHrStudyService::IndicationCompletion ElixirHrStudyService::ConsumeIndicationCompletion() {
  const auto value = completion.load();
  completion = IndicationCompletion::None;
  return value;
}

HrStudyTransportState ElixirHrStudyService::TransportState() const {
  if (!sessionActive) {
    return HrStudyTransportState::Off;
  }
  if (indicationInFlight) {
    return HrStudyTransportState::Sending;
  }
  return indicationEnabled ? HrStudyTransportState::Connected : HrStudyTransportState::Buffering;
}
