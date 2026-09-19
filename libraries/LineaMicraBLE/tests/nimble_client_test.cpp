#include "LineaMicraBLE.h"
#include "ShotStopperBleArbiter.h"
#include "nimble_client_platform.h"

#include <cassert>
#include <cstring>

using lineamicra::ClientEvent;
using lineamicra::EventType;

static void resetPlatform() {
  testNowMs = 100;
  testRuntimeReady = true;
  testSyncGeneration = 1;
  testSubmitStatus = 0;
  testGattSubmitStatus = 0;
  testConnectSubmitStatus = 0;
  testTerminateStatus = 0;
  testTerminations = testWrites = 0;
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
}

static void emitCharacteristic(const char *uuid, uint16_t handle,
                               uint8_t properties) {
  ble_gatt_chr characteristic = {};
  characteristic.val_handle = handle;
  characteristic.properties = properties;
  ble_uuid_from_str(&characteristic.uuid, uuid);
  const ble_gatt_error ok = {0, 0};
  assert(testCharacteristicCallback(7, &ok, &characteristic,
                                    testCharacteristicArg) == 0);
}

static void connectReady(lineamicra::LineaMicraBLE &client, uint16_t mtu) {
  lineamicra::PeerAddress peer = {};
  peer.type = 1;
  peer.value[0] = 0xaa;
  assert(client.connect(peer, {3000, 1000, 6000}, 41));
  ble_gap_event connected = {};
  connected.type = BLE_GAP_EVENT_CONNECT;
  connected.connect.status = 0;
  connected.connect.conn_handle = 7;
  assert(testGapCallback(&connected, testGapArg) == 0);
  client.service();
  assert(testMtuCallback != nullptr);
  const ble_gatt_error ok = {0, 0};
  assert(testMtuCallback(7, &ok, mtu, testMtuArg) == 0);
  client.service();
  ble_gatt_svc service = {1, 40};
  assert(testServiceCallback(7, &ok, &service, testServiceArg) == 0);
  const ble_gatt_error done = {BLE_HS_EDONE, 0};
  assert(testServiceCallback(7, &done, nullptr, testServiceArg) == 0);
  client.service();
  constexpr uint8_t rw = BLE_GATT_CHR_PROP_READ | BLE_GATT_CHR_PROP_WRITE;
  emitCharacteristic(lineamicra::kReadCharacteristicUuid, 10, rw);
  emitCharacteristic(lineamicra::kWriteCharacteristicUuid, 11, rw);
  emitCharacteristic(lineamicra::kAuthCharacteristicUuid, 13,
                     BLE_GATT_CHR_PROP_WRITE);
  assert(testCharacteristicCallback(7, &done, nullptr,
                                    testCharacteristicArg) == 0);
  client.service();
  ClientEvent event;
  assert(client.takeEvent(event));
  assert(event.type == EventType::READY && event.requestGeneration == 41);
  assert(client.health().negotiatedMtu == mtu);
}

static void completeWrite(lineamicra::LineaMicraBLE &client) {
  const ble_gatt_error ok = {0, 0};
  assert(testWriteCallback(7, &ok, nullptr, testWriteArg) == 0);
  client.service();
}

static void testSession(uint16_t mtu) {
  resetPlatform();
  lineamicra::LineaMicraBLE client;
  connectReady(client, mtu);

  char token[64];
  std::memset(token, 'T', sizeof(token));
  assert(client.authenticate(token, sizeof(token), 42));
  assert(testLastWriteHandle == 13 && testLastWriteLength == 64);
  assert(std::memcmp(testLastWriteData, token, 64) == 0);
  completeWrite(client);
  ClientEvent event;
  assert(client.takeEvent(event) && event.type == EventType::AUTHENTICATED);

  assert(client.query(lineamicra::Query::MACHINE_MODE, 43));
  assert(testLastWriteHandle == 10 && testLastWriteLength == 12);
  assert(std::memcmp(testLastWriteData, "machineMode\0", 12) == 0);
  const auto staleWrite = testWriteCallback;
  void *const staleArg = testWriteArg;
  completeWrite(client);
  assert(testReadCallback != nullptr);
  const ble_gatt_error ok = {0, 0};
  os_mbuf fragment = {};
  const char response[] = "\"BrewingMode\"";
  fragment.length = sizeof(response) - 1;
  std::memcpy(fragment.data, response, fragment.length);
  ble_gatt_attr attribute = {10, 0, &fragment};
  assert(testReadCallback(7, &ok, &attribute, testReadArg) == 0);
  const ble_gatt_error done = {BLE_HS_EDONE, 0};
  assert(testReadCallback(7, &done, nullptr, testReadArg) == 0);
  client.service();
  assert(client.takeEvent(event) && event.type == EventType::RESPONSE);
  assert(event.payloadLength == sizeof(response) - 1);
  assert(std::strcmp(event.payload, response) == 0);

  assert(client.setBrewTarget(935, 44));
  assert(std::strstr(reinterpret_cast<const char *>(testLastWriteData),
                     "\"value\":93.5") != nullptr);
  assert(staleWrite(7, &ok, nullptr, staleArg) == 0);
  assert(client.health().staleCallbacks == 1);
  completeWrite(client);
  assert(client.takeEvent(event) && event.type == EventType::WRITE_COMPLETE);

  client.disconnect();
  ble_gap_event disconnected = {};
  disconnected.type = BLE_GAP_EVENT_DISCONNECT;
  disconnected.disconnect.reason = 0x13;
  disconnected.disconnect.conn.conn_handle = 7;
  assert(testGapCallback(&disconnected, testGapArg) == 0);
  client.service();
  assert(client.takeEvent(event) && event.type == EventType::DISCONNECTED);
}

static void testFailures() {
  resetPlatform();
  {
    lineamicra::LineaMicraBLE client;
    lineamicra::PeerAddress peer = {};
    testScaleLeaseAvailable = false;
    assert(!client.connect(peer, {3000, 1000, 6000}, 49));
    assert(testGapCallback == nullptr);
  }
  resetPlatform();
  lineamicra::LineaMicraBLE client;
  connectReady(client, 23);
  char token[64];
  std::memset(token, 'T', sizeof(token));
  testMbufAllocFails = true;
  assert(!client.authenticate(token, sizeof(token), 50));
  ClientEvent event;
  assert(client.takeEvent(event) && event.type == EventType::ERROR);
  assert(event.status == BLE_HS_ENOMEM);
  assert(client.health().mbufFailures == 1);
}

static void testTimeoutAndReset() {
  resetPlatform();
  lineamicra::LineaMicraBLE client;
  lineamicra::PeerAddress peer = {};
  assert(client.connect(peer, {10, 10, 100}, 60));
  testNowMs += 11;
  client.service();
  ClientEvent event;
  assert(client.takeEvent(event) && event.type == EventType::ERROR);
  assert(event.status == BLE_HS_ETIMEOUT);

  ble_gap_event failed = {};
  failed.type = BLE_GAP_EVENT_CONNECT;
  failed.connect.status = BLE_HS_ETIMEOUT;
  failed.connect.conn_handle = 0xffff;
  testGapCallback(&failed, testGapArg);
  client.service();
}

int main() {
  testSession(23);
  testSession(185);
  testFailures();
  testTimeoutAndReset();
  return 0;
}
