#include "LineaMicraBLE.h"
#include "ShotStopperBleArbiter.h"

#if defined(LINEA_MICRA_BLE_HOST_TEST)
#include "nimble_client_platform.h"
#else
#include "ShotStopperBleRuntime.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "os/os_mbuf.h"
#endif

#include <cstring>
#include <new>

namespace lineamicra {
namespace {

constexpr uint16_t kInvalidHandle = 0xffff;
constexpr size_t kServiceCapacity = 16;
constexpr size_t kEventCapacity = 6;

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

bool elapsedAtLeast(uint32_t now, uint32_t since, uint32_t duration) {
  return static_cast<uint32_t>(now - since) >= duration;
}

struct ServiceRange {
  uint16_t start = 0;
  uint16_t end = 0;
};

enum class InternalEventType : uint8_t {
  CONNECTED,
  DISCONNECTED,
  MTU_DONE,
  SERVICES_DONE,
  CHARACTERISTICS_DONE,
  WRITE_DONE,
  READ_DONE
};

struct InternalEvent {
  InternalEventType type = InternalEventType::DISCONNECTED;
  uint32_t sessionGeneration = 0;
  uint32_t operationGeneration = 0;
  int32_t status = 0;
  uint16_t connectionHandle = kInvalidHandle;
  uint16_t mtu = 23;
};

enum class ActiveOperation : uint8_t {
  NONE,
  AUTHENTICATE,
  QUERY_WRITE,
  QUERY_READ,
  SET_TARGET,
  RESULT_READ
};

struct ClientImpl;

struct CallbackRegistry {
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  ClientImpl *owner = nullptr;
  uint32_t active = 0;
  bool accepting = false;
};

CallbackRegistry gCallbacks;

class CallbackLease {
 public:
  CallbackLease() {
    portENTER_CRITICAL(&gCallbacks.mux);
    if (gCallbacks.accepting && gCallbacks.owner != nullptr) {
      owner_ = gCallbacks.owner;
      ++gCallbacks.active;
    }
    portEXIT_CRITICAL(&gCallbacks.mux);
  }
  ~CallbackLease() {
    if (owner_ == nullptr) return;
    portENTER_CRITICAL(&gCallbacks.mux);
    --gCallbacks.active;
    portEXIT_CRITICAL(&gCallbacks.mux);
  }
  ClientImpl *owner() const { return owner_; }

 private:
  ClientImpl *owner_ = nullptr;
};

struct ClientImpl {
  ClientState state = ClientState::IDLE;
  ClientConfig config = {};
  PeerAddress peer = {};
  ClientHealth health = {};
  uint32_t requestGeneration = 0;
  uint32_t sessionGeneration = 0;
  uint32_t operationGeneration = 0;
  uint32_t linkOperationGeneration = 0;
  uint32_t hostGeneration = 0;
  ShotStopperBleLease radioLease = {};
  uint32_t sessionStartedAtMs = 0;
  uint32_t stateStartedAtMs = 0;
  uint16_t connectionHandle = kInvalidHandle;
  uint16_t readHandle = 0;
  uint16_t writeHandle = 0;
  uint16_t authHandle = 0;
  uint8_t readProperties = 0;
  uint8_t writeProperties = 0;
  uint8_t authProperties = 0;
  ServiceRange services[kServiceCapacity] = {};
  uint8_t serviceCount = 0;
  uint8_t serviceIndex = 0;
  bool serviceOverflow = false;
  bool responseOverflow = false;
  bool completionPending = false;
  bool cancelRequested = false;
  ActiveOperation operation = ActiveOperation::NONE;
  char writeBuffer[160] = {};
  uint16_t writeLength = 0;
  char response[kMaxResponseBytes + 1] = {};
  uint16_t responseLength = 0;
  InternalEvent events[kEventCapacity] = {};
  uint8_t eventHead = 0;
  uint8_t eventTail = 0;
  uint8_t eventCount = 0;
  ClientEvent completion = {};
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

  ClientImpl() {
    portENTER_CRITICAL(&gCallbacks.mux);
    if (gCallbacks.owner == nullptr) {
      gCallbacks.owner = this;
      gCallbacks.accepting = true;
    }
    portEXIT_CRITICAL(&gCallbacks.mux);
  }

  ~ClientImpl() {
    portENTER_CRITICAL(&gCallbacks.mux);
    if (gCallbacks.owner == this) gCallbacks.accepting = false;
    portEXIT_CRITICAL(&gCallbacks.mux);
    while (true) {
      portENTER_CRITICAL(&gCallbacks.mux);
      const bool done = gCallbacks.active == 0;
      portEXIT_CRITICAL(&gCallbacks.mux);
      if (done) break;
      vTaskDelay(1);
    }
    disconnectNow();
    releaseRadio();
    portENTER_CRITICAL(&gCallbacks.mux);
    if (gCallbacks.owner == this) gCallbacks.owner = nullptr;
    portEXIT_CRITICAL(&gCallbacks.mux);
  }

  static void *callbackArg(uint32_t operation) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(operation));
  }

  static uint32_t callbackOperation(void *argument) {
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(argument));
  }

  uint32_t nextOperation() {
    portENTER_CRITICAL(&mux);
    if (++operationGeneration == 0) ++operationGeneration;
    const uint32_t next = operationGeneration;
    portEXIT_CRITICAL(&mux);
    return next;
  }

  void enter(ClientState next) {
    state = next;
    stateStartedAtMs = nowMs();
  }

  bool push(const InternalEvent &event) {
    portENTER_CRITICAL(&mux);
    if (eventCount == kEventCapacity) {
      ++health.droppedEvents;
      portEXIT_CRITICAL(&mux);
      return false;
    }
    events[eventTail] = event;
    eventTail = static_cast<uint8_t>((eventTail + 1U) % kEventCapacity);
    ++eventCount;
    portEXIT_CRITICAL(&mux);
    return true;
  }

  bool pop(InternalEvent &event) {
    portENTER_CRITICAL(&mux);
    if (eventCount == 0) {
      portEXIT_CRITICAL(&mux);
      return false;
    }
    event = events[eventHead];
    eventHead = static_cast<uint8_t>((eventHead + 1U) % kEventCapacity);
    --eventCount;
    portEXIT_CRITICAL(&mux);
    return true;
  }

  void emit(EventType type, int32_t status = 0) {
    if (completionPending) {
      portENTER_CRITICAL(&mux);
      ++health.droppedEvents;
      portEXIT_CRITICAL(&mux);
      return;
    }
    completion = {};
    completion.type = type;
    completion.requestGeneration = requestGeneration;
    completion.sessionGeneration = sessionGeneration;
    completion.operationGeneration = operationGeneration;
    completion.status = status;
    if (type == EventType::RESPONSE) {
      completion.payloadLength = responseLength;
      std::memcpy(completion.payload, response,
                  static_cast<size_t>(responseLength) + 1U);
    }
    completionPending = true;
  }

  bool callbackContext(uint32_t actual, bool link, uint32_t &session) {
    portENTER_CRITICAL(&mux);
    const uint32_t expected = link ? linkOperationGeneration
                                   : operationGeneration;
    const bool matches = actual != 0 && actual == expected;
    if (matches) session = sessionGeneration;
    else ++health.staleCallbacks;
    portEXIT_CRITICAL(&mux);
    return matches;
  }

  static int gapCallback(ble_gap_event *event, void *argument) {
    CallbackLease lease;
    ClientImpl *self = lease.owner();
    if (self == nullptr || event == nullptr) return 0;
    const uint32_t operation = callbackOperation(argument);
    uint32_t session = 0;
    if (!self->callbackContext(operation, true, session)) return 0;
    InternalEvent result = {};
    result.sessionGeneration = session;
    result.operationGeneration = operation;
    if (event->type == BLE_GAP_EVENT_CONNECT) {
      result.type = InternalEventType::CONNECTED;
      result.status = event->connect.status;
      result.connectionHandle = event->connect.conn_handle;
      self->push(result);
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT) {
      result.type = InternalEventType::DISCONNECTED;
      result.status = event->disconnect.reason;
      result.connectionHandle = event->disconnect.conn.conn_handle;
      self->push(result);
    }
    return 0;
  }

  static int mtuCallback(uint16_t connection, const ble_gatt_error *error,
                         uint16_t mtu, void *argument) {
    CallbackLease lease;
    ClientImpl *self = lease.owner();
    if (self == nullptr) return 0;
    const uint32_t operation = callbackOperation(argument);
    uint32_t session = 0;
    if (!self->callbackContext(operation, false, session)) return 0;
    self->push({InternalEventType::MTU_DONE, session, operation,
                error == nullptr ? BLE_HS_EUNKNOWN : error->status,
                connection, mtu});
    return 0;
  }

  static int serviceCallback(uint16_t connection, const ble_gatt_error *error,
                             const ble_gatt_svc *service, void *argument) {
    CallbackLease lease;
    ClientImpl *self = lease.owner();
    if (self == nullptr || error == nullptr) return 0;
    const uint32_t operation = callbackOperation(argument);
    uint32_t session = 0;
    if (!self->callbackContext(operation, false, session)) return 0;
    if (error->status == 0 && service != nullptr) {
      portENTER_CRITICAL(&self->mux);
      if (self->serviceCount < kServiceCapacity) {
        self->services[self->serviceCount++] =
            {service->start_handle, service->end_handle};
      } else {
        self->serviceOverflow = true;
      }
      portEXIT_CRITICAL(&self->mux);
    } else {
      self->push({InternalEventType::SERVICES_DONE, session,
                  operation, error->status, connection, 0});
    }
    return 0;
  }

  static int characteristicCallback(uint16_t connection,
                                    const ble_gatt_error *error,
                                    const ble_gatt_chr *characteristic,
                                    void *argument) {
    CallbackLease lease;
    ClientImpl *self = lease.owner();
    if (self == nullptr || error == nullptr) return 0;
    const uint32_t operation = callbackOperation(argument);
    uint32_t session = 0;
    if (!self->callbackContext(operation, false, session)) return 0;
    if (error->status == 0 && characteristic != nullptr) {
      ble_uuid_any_t readUuid = {}, writeUuid = {}, authUuid = {};
      ble_uuid_from_str(&readUuid, kReadCharacteristicUuid);
      ble_uuid_from_str(&writeUuid, kWriteCharacteristicUuid);
      ble_uuid_from_str(&authUuid, kAuthCharacteristicUuid);
      portENTER_CRITICAL(&self->mux);
      if (ble_uuid_cmp(&readUuid.u, &characteristic->uuid.u) == 0) {
        self->readHandle = characteristic->val_handle;
        self->readProperties = characteristic->properties;
      } else if (ble_uuid_cmp(&writeUuid.u, &characteristic->uuid.u) == 0) {
        self->writeHandle = characteristic->val_handle;
        self->writeProperties = characteristic->properties;
      } else if (ble_uuid_cmp(&authUuid.u, &characteristic->uuid.u) == 0) {
        self->authHandle = characteristic->val_handle;
        self->authProperties = characteristic->properties;
      }
      portEXIT_CRITICAL(&self->mux);
    } else {
      self->push({InternalEventType::CHARACTERISTICS_DONE,
                  session, operation, error->status,
                  connection, 0});
    }
    return 0;
  }

  static int writeCallback(uint16_t connection, const ble_gatt_error *error,
                           ble_gatt_attr *, void *argument) {
    CallbackLease lease;
    ClientImpl *self = lease.owner();
    if (self == nullptr) return 0;
    const uint32_t operation = callbackOperation(argument);
    uint32_t session = 0;
    if (!self->callbackContext(operation, false, session)) return 0;
    self->push({InternalEventType::WRITE_DONE, session,
                operation, error == nullptr ? BLE_HS_EUNKNOWN : error->status,
                connection, 0});
    return 0;
  }

  static int readCallback(uint16_t connection, const ble_gatt_error *error,
                          ble_gatt_attr *attribute, void *argument) {
    CallbackLease lease;
    ClientImpl *self = lease.owner();
    if (self == nullptr || error == nullptr) return 0;
    const uint32_t operation = callbackOperation(argument);
    uint32_t session = 0;
    if (!self->callbackContext(operation, false, session)) return 0;
    if (error->status == 0 && attribute != nullptr && attribute->om != nullptr) {
      const uint16_t bytes = OS_MBUF_PKTLEN(attribute->om);
      portENTER_CRITICAL(&self->mux);
      const bool fits = bytes <= kMaxResponseBytes - self->responseLength;
      portEXIT_CRITICAL(&self->mux);
      if (!fits || os_mbuf_copydata(attribute->om, 0, bytes,
                                    self->response + self->responseLength) != 0) {
        portENTER_CRITICAL(&self->mux);
        self->responseOverflow = true;
        ++self->health.rejectedResponses;
        portEXIT_CRITICAL(&self->mux);
      } else {
        portENTER_CRITICAL(&self->mux);
        self->responseLength = static_cast<uint16_t>(self->responseLength + bytes);
        self->response[self->responseLength] = '\0';
        portEXIT_CRITICAL(&self->mux);
      }
    } else {
      self->push({InternalEventType::READ_DONE, session,
                  operation, error->status, connection, 0});
    }
    return 0;
  }

  void fail(int32_t status) {
    const ClientState previous = state;
    operation = ActiveOperation::NONE;
    emit(EventType::ERROR, status);
    if (connectionHandle != kInvalidHandle) {
      cancelRequested = true;
      const int rc = ble_gap_terminate(connectionHandle,
                                       BLE_ERR_REM_USER_CONN_TERM);
      if (rc == 0 || rc == BLE_HS_EALREADY) enter(ClientState::DISCONNECTING);
      else {
        enter(ClientState::FAILED);
        releaseRadio();
      }
    } else if (previous == ClientState::CONNECTING) {
      cancelRequested = true;
      const int rc = ble_gap_conn_cancel();
      if (rc == 0 || rc == BLE_HS_EALREADY) enter(ClientState::DISCONNECTING);
      else {
        enter(ClientState::FAILED);
        releaseRadio();
      }
    } else {
      enter(ClientState::FAILED);
      releaseRadio();
    }
  }

  void releaseRadio() {
    if (radioLease.id == 0) return;
    shotStopperBleArbiterRelease(radioLease);
    radioLease = {};
  }

  void beginMtu() {
    enter(ClientState::DISCOVERING);
    const uint32_t operationId = nextOperation();
    const int rc = ble_gattc_exchange_mtu(connectionHandle, mtuCallback,
                                           callbackArg(operationId));
    if (rc != 0) beginServices();
  }

  void beginServices() {
    serviceCount = 0;
    serviceIndex = 0;
    serviceOverflow = false;
    const uint32_t operationId = nextOperation();
    const int rc = ble_gattc_disc_all_svcs(connectionHandle, serviceCallback,
                                           callbackArg(operationId));
    if (rc != 0) fail(rc);
  }

  void beginCharacteristics() {
    if (serviceIndex >= serviceCount) {
      constexpr uint8_t kRead = BLE_GATT_CHR_PROP_READ;
      constexpr uint8_t kWrite = BLE_GATT_CHR_PROP_WRITE;
      if (readHandle == 0 || writeHandle == 0 || authHandle == 0 ||
          (readProperties & (kRead | kWrite)) != (kRead | kWrite) ||
          (writeProperties & (kRead | kWrite)) != (kRead | kWrite) ||
          (authProperties & kWrite) == 0) {
        fail(BLE_HS_ENOENT);
        return;
      }
      enter(ClientState::READY);
      emit(EventType::READY);
      return;
    }
    const ServiceRange range = services[serviceIndex];
    const uint32_t operationId = nextOperation();
    const int rc = ble_gattc_disc_all_chrs(connectionHandle, range.start,
                                            range.end, characteristicCallback,
                                            callbackArg(operationId));
    if (rc != 0) fail(rc);
  }

  bool beginWrite(uint16_t handle, const char *data, uint16_t length,
                  ActiveOperation nextOperationKind,
                  uint32_t nextRequestGeneration) {
    if (state != ClientState::READY || completionPending || data == nullptr ||
        length == 0 || length > sizeof(writeBuffer)) {
      return false;
    }
    requestGeneration = nextRequestGeneration;
    operation = nextOperationKind;
    std::memcpy(writeBuffer, data, length);
    writeLength = length;
    enter(ClientState::OPERATING);
    const uint32_t operationId = nextOperation();
    os_mbuf *buffer = ble_hs_mbuf_from_flat(writeBuffer, writeLength);
    if (buffer == nullptr) {
      portENTER_CRITICAL(&mux);
      ++health.mbufFailures;
      portEXIT_CRITICAL(&mux);
      fail(BLE_HS_ENOMEM);
      return false;
    }
    const int rc = ble_gattc_write_long(connectionHandle, handle, 0, buffer,
                                         writeCallback,
                                         callbackArg(operationId));
    if (rc != 0) {
      fail(rc);
      return false;
    }
    return true;
  }

  bool beginRead(uint16_t handle, ActiveOperation nextOperationKind,
                 uint32_t nextRequestGeneration) {
    if (state != ClientState::READY || completionPending || handle == 0) return false;
    requestGeneration = nextRequestGeneration;
    operation = nextOperationKind;
    responseLength = 0;
    responseOverflow = false;
    response[0] = '\0';
    enter(ClientState::OPERATING);
    const uint32_t operationId = nextOperation();
    const int rc = ble_gattc_read_long(connectionHandle, handle, 0,
                                        readCallback, callbackArg(operationId));
    if (rc != 0) {
      fail(rc);
      return false;
    }
    return true;
  }

  void service() {
    if (state != ClientState::IDLE && state != ClientState::FAILED &&
        state != ClientState::DISCONNECTING &&
        !shotStopperBleArbiterLeaseCurrent(radioLease)) {
      fail(BLE_HS_EBUSY);
      return;
    }
    if (state != ClientState::IDLE && state != ClientState::FAILED &&
        hostGeneration != shotStopperBleRuntimeSyncGeneration()) {
      fail(BLE_HS_ENOTSYNCED);
      return;
    }
    InternalEvent event;
    while (!completionPending && pop(event)) {
      if (event.sessionGeneration != sessionGeneration) {
        portENTER_CRITICAL(&mux);
        ++health.staleCallbacks;
        portEXIT_CRITICAL(&mux);
        continue;
      }
      switch (event.type) {
        case InternalEventType::CONNECTED:
          if (event.status != 0) {
            operation = ActiveOperation::NONE;
            releaseRadio();
            enter(ClientState::IDLE);
            emit(cancelRequested ? EventType::DISCONNECTED : EventType::ERROR,
                 event.status);
          } else if (cancelRequested) {
            connectionHandle = event.connectionHandle;
            disconnectNow();
          } else {
            connectionHandle = event.connectionHandle;
            beginMtu();
          }
          break;
        case InternalEventType::DISCONNECTED:
          connectionHandle = kInvalidHandle;
          operation = ActiveOperation::NONE;
          releaseRadio();
          enter(ClientState::IDLE);
          emit(EventType::DISCONNECTED, event.status);
          break;
        case InternalEventType::MTU_DONE:
          if (event.status == 0 && event.mtu >= 23) {
            portENTER_CRITICAL(&mux);
            health.negotiatedMtu = event.mtu;
            portEXIT_CRITICAL(&mux);
          }
          beginServices();
          break;
        case InternalEventType::SERVICES_DONE:
          if (event.status != BLE_HS_EDONE || serviceOverflow || serviceCount == 0) {
            fail(event.status == BLE_HS_EDONE ? BLE_HS_ENOMEM : event.status);
          } else {
            serviceIndex = 0;
            beginCharacteristics();
          }
          break;
        case InternalEventType::CHARACTERISTICS_DONE:
          if (event.status != BLE_HS_EDONE) {
            fail(event.status);
          } else {
            ++serviceIndex;
            beginCharacteristics();
          }
          break;
        case InternalEventType::WRITE_DONE:
          if (event.status != 0) {
            fail(event.status);
          } else if (operation == ActiveOperation::QUERY_WRITE) {
            enter(ClientState::READY);
            beginRead(readHandle, ActiveOperation::QUERY_READ, requestGeneration);
          } else {
            const EventType type = operation == ActiveOperation::AUTHENTICATE
                                       ? EventType::AUTHENTICATED
                                       : EventType::WRITE_COMPLETE;
            operation = ActiveOperation::NONE;
            enter(ClientState::READY);
            emit(type);
          }
          break;
        case InternalEventType::READ_DONE:
          if (event.status != BLE_HS_EDONE || responseOverflow || responseLength == 0) {
            fail(responseOverflow ? BLE_HS_EBADDATA : event.status);
          } else {
            operation = ActiveOperation::NONE;
            enter(ClientState::READY);
            emit(EventType::RESPONSE);
          }
          break;
      }
    }
    // Preserve output ordering: the caller consumes one completion at a time,
    // then a later service pass may advance queued native callbacks.
    if (completionPending) return;
    const uint32_t now = nowMs();
    if (state == ClientState::DISCONNECTING &&
        elapsedAtLeast(now, stateStartedAtMs, config.operationTimeoutMs)) {
      if (!completionPending) emit(EventType::ERROR, BLE_HS_ETIMEOUT);
      enter(ClientState::FAILED);
      releaseRadio();
    } else if ((state == ClientState::CONNECTING &&
         elapsedAtLeast(now, stateStartedAtMs, config.connectTimeoutMs)) ||
        ((state == ClientState::DISCOVERING || state == ClientState::OPERATING) &&
         elapsedAtLeast(now, stateStartedAtMs, config.operationTimeoutMs)) ||
        (state != ClientState::IDLE &&
         elapsedAtLeast(now, sessionStartedAtMs, config.sessionTimeoutMs))) {
      fail(BLE_HS_ETIMEOUT);
    }
  }

  void disconnectNow() {
    cancelRequested = true;
    if (connectionHandle != kInvalidHandle) {
      const int rc = ble_gap_terminate(connectionHandle,
                                       BLE_ERR_REM_USER_CONN_TERM);
      if (rc == 0 || rc == BLE_HS_EALREADY) enter(ClientState::DISCONNECTING);
      else {
        if (!completionPending) emit(EventType::ERROR, rc);
        enter(ClientState::FAILED);
        releaseRadio();
      }
    } else if (state == ClientState::CONNECTING) {
      const int rc = ble_gap_conn_cancel();
      if (rc == 0 || rc == BLE_HS_EALREADY) enter(ClientState::DISCONNECTING);
      else {
        if (!completionPending) emit(EventType::ERROR, rc);
        enter(ClientState::FAILED);
        releaseRadio();
      }
    } else {
      enter(ClientState::IDLE);
      releaseRadio();
    }
  }
};

static_assert(sizeof(ClientImpl) <= 1536, "fixed client exceeded its reviewed storage");

ClientImpl *implementation(uint8_t *storage) {
  return reinterpret_cast<ClientImpl *>(storage);
}
const ClientImpl *implementation(const uint8_t *storage) {
  return reinterpret_cast<const ClientImpl *>(storage);
}

}  // namespace

LineaMicraBLE::LineaMicraBLE() { new (storage_) ClientImpl(); }
LineaMicraBLE::~LineaMicraBLE() { implementation(storage_)->~ClientImpl(); }

bool LineaMicraBLE::connect(const PeerAddress &peer, const ClientConfig &config,
                            uint32_t requestGeneration) {
  ClientImpl &self = *implementation(storage_);
  self.service();
  if (self.state != ClientState::IDLE || self.completionPending ||
      !shotStopperBleRuntimeReady()) {
    return false;
  }
  if (!shotStopperBleArbiterTryAcquire(ShotStopperBleOwner::Machine,
                                       self.radioLease)) {
    return false;
  }
  self.peer = peer;
  self.config = config;
  self.requestGeneration = requestGeneration;
  portENTER_CRITICAL(&self.mux);
  if (++self.sessionGeneration == 0) ++self.sessionGeneration;
  self.eventHead = self.eventTail = self.eventCount = 0;
  portEXIT_CRITICAL(&self.mux);
  self.hostGeneration = shotStopperBleRuntimeSyncGeneration();
  self.sessionStartedAtMs = nowMs();
  self.cancelRequested = false;
  self.readHandle = self.writeHandle = self.authHandle = 0;
  self.readProperties = self.writeProperties = self.authProperties = 0;
  self.responseLength = 0;
  self.enter(ClientState::CONNECTING);
  ble_addr_t address = {};
  address.type = peer.type;
  std::memcpy(address.val, peer.value, sizeof(address.val));
  const uint32_t linkOperation = self.nextOperation();
  portENTER_CRITICAL(&self.mux);
  self.linkOperationGeneration = linkOperation;
  portEXIT_CRITICAL(&self.mux);
  const int rc = ble_gap_connect(shotStopperBleRuntimeOwnAddressType(), &address,
                                 config.connectTimeoutMs, nullptr,
                                 ClientImpl::gapCallback,
                                 ClientImpl::callbackArg(linkOperation));
  if (rc != 0) {
    self.enter(ClientState::FAILED);
    self.fail(rc);
    return false;
  }
  return true;
}

bool LineaMicraBLE::authenticate(const char *token, size_t length,
                                 uint32_t requestGeneration) {
  ClientImpl &self = *implementation(storage_);
  return validToken(token, length) &&
         self.beginWrite(self.authHandle, token, static_cast<uint16_t>(length),
                         ActiveOperation::AUTHENTICATE, requestGeneration);
}

bool LineaMicraBLE::query(Query query, uint32_t requestGeneration) {
  ClientImpl &self = *implementation(storage_);
  size_t length = 0;
  if (!buildQuery(query, self.writeBuffer, sizeof(self.writeBuffer), length)) return false;
  return self.beginWrite(self.readHandle, self.writeBuffer,
                         static_cast<uint16_t>(length),
                         ActiveOperation::QUERY_WRITE, requestGeneration);
}

bool LineaMicraBLE::setBrewTarget(uint16_t targetDeciC,
                                  uint32_t requestGeneration) {
  ClientImpl &self = *implementation(storage_);
  size_t length = 0;
  if (!buildSetBrewTarget(targetDeciC, self.writeBuffer,
                          sizeof(self.writeBuffer), length)) {
    return false;
  }
  return self.beginWrite(self.writeHandle, self.writeBuffer,
                         static_cast<uint16_t>(length),
                         ActiveOperation::SET_TARGET, requestGeneration);
}

bool LineaMicraBLE::readCommandResult(uint32_t requestGeneration) {
  ClientImpl &self = *implementation(storage_);
  return self.beginRead(self.writeHandle, ActiveOperation::RESULT_READ,
                        requestGeneration);
}

void LineaMicraBLE::service() { implementation(storage_)->service(); }

bool LineaMicraBLE::takeEvent(ClientEvent &event) {
  ClientImpl &self = *implementation(storage_);
  self.service();
  if (!self.completionPending) return false;
  event = self.completion;
  self.completion = {};
  self.completionPending = false;
  return true;
}

void LineaMicraBLE::abort() { implementation(storage_)->disconnectNow(); }
void LineaMicraBLE::disconnect() { implementation(storage_)->disconnectNow(); }
ClientState LineaMicraBLE::state() const { return implementation(storage_)->state; }
bool LineaMicraBLE::connected() const {
  return implementation(storage_)->connectionHandle != kInvalidHandle;
}
ClientHealth LineaMicraBLE::health() const {
  ClientImpl &self = *implementation(const_cast<uint8_t *>(storage_));
  portENTER_CRITICAL(&self.mux);
  const ClientHealth snapshot = self.health;
  portEXIT_CRITICAL(&self.mux);
  return snapshot;
}

}  // namespace lineamicra
