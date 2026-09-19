#include "machine/ShotStopperMicraService.h"

#include <Arduino.h>

#include <atomic>
#include <cassert>
#include <cstring>
#include <thread>

uint32_t fakeMillis = 0;
FakeSerialClass Serial;

namespace {

void resetPlatform() {
  fakeMillis = 100;
  testNowMs = 100;
  testRuntimeReady = true;
  testSyncGeneration = 1;
  testSubmitStatus = 0;
  testGattSubmitStatus = 0;
  testConnectSubmitStatus = 0;
  testTerminateStatus = 0;
  testConnectCancelStatus = 0;
  testTerminations = testConnectCancels = testConnects = testWrites = 0;
  testMbufAllocFails = false;
  testMtuCallback = nullptr;
  testServiceCallback = nullptr;
  testCharacteristicCallback = nullptr;
  testReadCallback = nullptr;
  testWriteCallback = nullptr;
  testGapCallback = nullptr;
  testScaleLeaseAvailable = true;
  testScaleLeaseActive = false;
  testScaleLeaseId = 0;
  testBleCritical = false;
  testBleCriticalEpoch = 0;
  testMachineObserver = nullptr;
  testMachineObserverContext = nullptr;
  testObservationWindowAvailable = true;
  testMachineProcedureAvailable = true;
}

void emitCharacteristic(const char *uuid, uint16_t handle,
                        uint8_t properties) {
  ble_gatt_chr characteristic = {};
  characteristic.val_handle = handle;
  characteristic.properties = properties;
  ble_uuid_from_str(&characteristic.uuid, uuid);
  const ble_gatt_error ok = {0, 0};
  assert(testCharacteristicCallback(7, &ok, &characteristic,
                                    testCharacteristicArg) == 0);
}

void serviceReady(shotstopper::ShotStopperMicraService &service) {
  ble_gap_event connected = {};
  connected.type = BLE_GAP_EVENT_CONNECT;
  connected.connect.status = 0;
  connected.connect.conn_handle = 7;
  assert(testGapCallback(&connected, testGapArg) == 0);
  service.service();
  const ble_gatt_error ok = {0, 0};
  assert(testMtuCallback(7, &ok, 185, testMtuArg) == 0);
  service.service();
  ble_gatt_svc range = {1, 40};
  assert(testServiceCallback(7, &ok, &range, testServiceArg) == 0);
  const ble_gatt_error done = {BLE_HS_EDONE, 0};
  assert(testServiceCallback(7, &done, nullptr, testServiceArg) == 0);
  service.service();
  constexpr uint8_t readWrite = BLE_GATT_CHR_PROP_READ |
                                BLE_GATT_CHR_PROP_WRITE;
  emitCharacteristic(lineamicra::kReadCharacteristicUuid, 10, readWrite);
  emitCharacteristic(lineamicra::kWriteCharacteristicUuid, 11, readWrite);
  emitCharacteristic(lineamicra::kAuthCharacteristicUuid, 13,
                     BLE_GATT_CHR_PROP_WRITE);
  assert(testCharacteristicCallback(7, &done, nullptr,
                                    testCharacteristicArg) == 0);
  service.service();
}

void completeWrite(shotstopper::ShotStopperMicraService &service) {
  const ble_gatt_error ok = {0, 0};
  assert(testWriteCallback(7, &ok, nullptr, testWriteArg) == 0);
  service.service();
}

void completeRead(shotstopper::ShotStopperMicraService &service,
                  const char *payload) {
  const ble_gatt_error ok = {0, 0};
  os_mbuf fragment = {};
  fragment.length = static_cast<uint16_t>(std::strlen(payload));
  std::memcpy(fragment.data, payload, fragment.length);
  ble_gatt_attr attribute = {10, 0, &fragment};
  assert(testReadCallback(7, &ok, &attribute, testReadArg) == 0);
  const ble_gatt_error done = {BLE_HS_EDONE, 0};
  assert(testReadCallback(7, &done, nullptr, testReadArg) == 0);
  service.service();
}

void completeDisconnect(shotstopper::ShotStopperMicraService &service) {
  ble_gap_event disconnected = {};
  disconnected.type = BLE_GAP_EVENT_DISCONNECT;
  disconnected.disconnect.conn.conn_handle = 7;
  assert(testGapCallback(&disconnected, testGapArg) == 0);
  service.service();
}

void testReadOnlyAssociationFlow() {
  resetPlatform();
  shotstopper::ShotStopperMicraService service;
  assert(service.begin());
  shotstopper::LineaMicraPersistedSettings config;
  char token[64];
  std::memset(token, 'T', sizeof(token));
  assert(shotstopper::setLineaMicraToken(config, token, sizeof(token)));
  service.publishConfig(config, 7);
  service.service();
  assert(service.status().phase == shotstopper::LineaMicraPhase::IDLE);

  shotstopper::LineaMicraRequest unsupported;
  unsupported.requestId = 40;
  unsupported.configGeneration = 7;
  unsupported.type = static_cast<shotstopper::LineaMicraRequestType>(0xff);
  assert(!service.queue(unsupported));
  unsupported.type = shotstopper::LineaMicraRequestType::OBSERVE_STATE;
  assert(!service.queue(unsupported));

  shotstopper::LineaMicraRequest request;
  request.requestId = 41;
  request.configGeneration = 7;
  assert(service.queue(request));
  assert(!service.queue(request));
  service.service();
  assert(service.status().phase == shotstopper::LineaMicraPhase::QUEUED);

  uint8_t payload[] = {11, 0x09, 'M', 'I', 'C', 'R', 'A', '_', 'U', 'N', 'I', 'T'};
  ShotStopperBleAdvertisement advertisement;
  advertisement.addressType = 1;
  advertisement.address[0] = 0xaa;
  advertisement.rssi = -40;
  advertisement.connectable = true;
  advertisement.payload = payload;
  advertisement.payloadLength = sizeof(payload);
  shotStopperBleArbiterPublishAdvertisement(advertisement);
  fakeMillis += shotstopper::micra_timing::kDiscoverySliceMs;
  testNowMs = fakeMillis;
  service.service();
  assert(testGapCallback != nullptr);
  serviceReady(service);
  assert(testLastWriteHandle == 13 && testLastWriteLength == 64);
  completeWrite(service);
  assert(std::memcmp(testLastWriteData, "machineCapabilities\0", 20) == 0);
  completeWrite(service);
  completeRead(service, "[{\"family\":\"MICRA\"}]");
  assert(std::memcmp(testLastWriteData, "boilers\0", 8) == 0);
  completeWrite(service);
  completeRead(service,
               "[{\"id\":\"CoffeeBoiler1\",\"current\":92.5,\"target\":93.0}]");
  const shotstopper::LineaMicraStatus status = service.status();
  assert(status.phase == shotstopper::LineaMicraPhase::CONFIRMED);
  assert(status.measuredDeciC == 925 && status.targetDeciC == 930);
  assert(testWrites == 3);  // auth and two read-only query writes
  shotstopper::LineaMicraRequest overlapping = request;
  overlapping.requestId = 42;
  assert(!service.queue(overlapping));
  shotstopper::LineaMicraBindingResult binding;
  assert(service.takeBinding(binding));
  assert(binding.requestId == request.requestId);
  assert(std::strcmp(binding.identity, "MICRA_UNIT") == 0);
}

void testPairingRetryGetsANewDiscoverySlice() {
  resetPlatform();
  shotstopper::ShotStopperMicraService service;
  assert(service.begin());
  shotstopper::LineaMicraPersistedSettings config;
  char token[64];
  std::memset(token, 'T', sizeof(token));
  assert(shotstopper::setLineaMicraToken(config, token, sizeof(token)));
  service.publishConfig(config, 8);
  service.service();
  shotstopper::LineaMicraRequest request;
  request.requestId = 77;
  request.configGeneration = 8;
  assert(service.queue(request));
  service.service();

  uint8_t invalidPayload[] = {11, 0x09, 'M', 'I', 'C', 'R', 'A', '_',
                              'B',  'A',  'D', 0x01};
  ShotStopperBleAdvertisement advertisement;
  advertisement.addressType = 1;
  advertisement.address[0] = 0xaa;
  advertisement.rssi = -30;
  advertisement.connectable = true;
  advertisement.payload = invalidPayload;
  advertisement.payloadLength = sizeof(invalidPayload);
  shotStopperBleArbiterPublishAdvertisement(advertisement);
  fakeMillis += shotstopper::micra_timing::kDiscoverySliceMs;
  testNowMs = fakeMillis;
  service.service();
  assert(service.status().phase == shotstopper::LineaMicraPhase::BACKOFF);
  assert(testConnects == 0);

  fakeMillis += shotstopper::micra_timing::kRetryDelaysMs[0] +
                shotstopper::micra_timing::kJitterMaxMs + 1;
  testNowMs = fakeMillis;
  service.service();
  assert(service.status().phase == shotstopper::LineaMicraPhase::QUEUED);
  assert(testConnects == 0);
}

void testConcurrentConfigAndStatusPublication() {
  resetPlatform();
  shotstopper::ShotStopperMicraService service;
  assert(service.begin());
  std::atomic<bool> running{true};
  std::thread reader([&] {
    while (running.load(std::memory_order_acquire)) (void)service.status();
  });
  for (uint32_t generation = 1; generation <= 100; ++generation) {
    shotstopper::LineaMicraPersistedSettings config;
    service.publishConfig(config, generation);
    service.service();
  }
  running.store(false, std::memory_order_release);
  reader.join();
  assert(service.status().configGeneration == 100);
}

void testAutomaticStateFreshnessAndPostActivityRefresh() {
  resetPlatform();
  shotstopper::ShotStopperMicraService service;
  assert(service.begin());
  shotstopper::LineaMicraPersistedSettings config;
  char token[64];
  std::memset(token, 'T', sizeof(token));
  const uint8_t address[6] = {1, 2, 3, 4, 5, 6};
  assert(shotstopper::setLineaMicraToken(config, token, sizeof(token)));
  assert(shotstopper::setLineaMicraBinding(config, 1, address, "MICRA_TEST"));
  assert(shotstopper::setLineaMicraOptions(config, false, true));
  service.publishConfig(config, 9);
  service.service();
  serviceReady(service);
  completeWrite(service);
  assert(std::memcmp(testLastWriteData, "machineMode\0", 12) == 0);
  completeWrite(service);
  completeRead(service, "\"EcoMode\"");
  auto status = service.status();
  assert(status.powerState == shotstopper::LineaMicraPowerState::UNKNOWN);
  assert(status.observedMode == shotstopper::LineaMicraObservedMode::ECO);
  assert(status.quality ==
         shotstopper::LineaMicraObservationQuality::UNSUPPORTED);
  completeDisconnect(service);

  fakeMillis += shotstopper::micra_timing::kStateFreshnessMs;
  testNowMs = fakeMillis;
  status = service.status();
  assert(status.powerState == shotstopper::LineaMicraPowerState::UNKNOWN);
  assert(status.quality == shotstopper::LineaMicraObservationQuality::STALE);

  testBleCritical = true;
  ++testBleCriticalEpoch;
  service.service();
  testBleCritical = false;
  fakeMillis += shotstopper::micra_timing::kMinDisconnectedMs;
  testNowMs = fakeMillis;
  service.service();
  assert(service.status().phase == shotstopper::LineaMicraPhase::QUEUED ||
         service.status().phase == shotstopper::LineaMicraPhase::RUNNING);
}

void testPostActivityRefreshPreservesExhaustedCooldown() {
  resetPlatform();
  testConnectSubmitStatus = BLE_HS_EBUSY;
  shotstopper::ShotStopperMicraService service;
  assert(service.begin());
  shotstopper::LineaMicraPersistedSettings config;
  char token[64];
  std::memset(token, 'T', sizeof(token));
  const uint8_t address[6] = {1, 2, 3, 4, 5, 6};
  assert(shotstopper::setLineaMicraToken(config, token, sizeof(token)));
  assert(shotstopper::setLineaMicraBinding(config, 1, address, "MICRA_TEST"));
  assert(shotstopper::setLineaMicraOptions(config, false, true));
  service.publishConfig(config, 10);
  service.service();
  for (unsigned retry = 1; retry < shotstopper::micra_timing::kMaxAttempts;
       ++retry) {
    fakeMillis += 11000;
    testNowMs = fakeMillis;
    service.service();
  }
  assert(service.status().phase == shotstopper::LineaMicraPhase::FAILED);
  assert(testConnects == shotstopper::micra_timing::kMaxAttempts);

  testBleCritical = true;
  ++testBleCriticalEpoch;
  service.service();
  testBleCritical = false;
  fakeMillis += shotstopper::micra_timing::kMinDisconnectedMs;
  testNowMs = fakeMillis;
  service.service();
  assert(testConnects == shotstopper::micra_timing::kMaxAttempts);

  fakeMillis += shotstopper::micra_timing::kExhaustedCooldownMs +
                shotstopper::micra_timing::kJitterMaxMs;
  testNowMs = fakeMillis;
  service.service();
  assert(testConnects == shotstopper::micra_timing::kMaxAttempts + 1);
}

}  // namespace

int main() {
  testReadOnlyAssociationFlow();
  testPairingRetryGetsANewDiscoverySlice();
  testAutomaticStateFreshnessAndPostActivityRefresh();
  testPostActivityRefreshPreservesExhaustedCooldown();
  testConcurrentConfigAndStatusPublication();
  return 0;
}
