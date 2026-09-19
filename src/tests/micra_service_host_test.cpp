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
  testTerminations = testConnectCancels = testWrites = 0;
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
  testMachineObserver = nullptr;
  testMachineObserverContext = nullptr;
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
  unsupported.type = shotstopper::LineaMicraRequestType::RECONCILE_TARGET;
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

}  // namespace

int main() {
  testReadOnlyAssociationFlow();
  testConcurrentConfigAndStatusPublication();
  return 0;
}
