/*
  Native ESP-IDF NimBLE implementation of the EspressoScaleBLE facade.
*/
#include "EspressoScaleBLE.h"

#include "nimble/NimbleAdvertisement.h"
#include "nimble/NimbleResilience.h"

#if defined(ESPRESSO_SCALE_BLE_HOST_TEST)
#include "nimble_client_platform.h"
#else
#include "ShotStopperBleRuntime.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "host/ble_gatt.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/ble_hs_adv.h"
#include "host/ble_uuid.h"
#include "nimble/ble.h"
#include "os/os_mbuf.h"
#endif

#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Shot Stopper retains scale diagnostics in its bounded WebUI log. Keep the
// bridge weak so this library remains reusable by standalone IDF projects.
extern "C" void shotStopperScaleLog(uint8_t severity, const char *message)
    __attribute__((weak));

namespace {

constexpr char kTag[] = "scale.nimble";
constexpr uint16_t kInvalidHandle = 0xffff;
constexpr size_t kCandidateCount = 8;
constexpr size_t kServiceCount = 24;
constexpr size_t kEventCount = 12;
constexpr size_t kCriticalEventCount = 6;
constexpr size_t kRxFrameCount = 16;
constexpr size_t kProtocolCapacity = 11;
constexpr uint32_t kScanCancelTimeoutMs = 1000;
constexpr uint32_t kUnsupportedCooldownMs = 60000;
constexpr uint32_t kConnectCallbackMarginMs = 50;

void scaleLogDebug(const char *format, ...) {
  if (format == nullptr) {
    return;
  }
  char message[128] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  if (shotStopperScaleLog != nullptr) {
    shotStopperScaleLog(0, message);
  } else {
    ESP_LOGD(kTag, "%s", message);
  }
}

uint32_t nowMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

uint32_t elapsedMs(uint32_t since) {
  return static_cast<uint32_t>(nowMs() - since);
}

bool addressEqual(const uint8_t left[6], const uint8_t right[6]) {
  return memcmp(left, right, 6) == 0;
}

void formatAddress(const uint8_t address[6], char *output, size_t capacity) {
  if (output == nullptr || capacity < SCALE_MAC_CAPACITY) {
    return;
  }
  snprintf(output, capacity, "%02X:%02X:%02X:%02X:%02X:%02X", address[5],
           address[4], address[3], address[2], address[1], address[0]);
}

int hexNibble(char value) {
  if (value >= '0' && value <= '9') {
    return value - '0';
  }
  if (value >= 'a' && value <= 'f') {
    return value - 'a' + 10;
  }
  if (value >= 'A' && value <= 'F') {
    return value - 'A' + 10;
  }
  return -1;
}

bool parseAddress(const char *text, uint8_t output[6]) {
  if (text == nullptr || strlen(text) != 17) {
    return false;
  }
  uint8_t displayOrder[6] = {};
  for (size_t index = 0; index < 6; ++index) {
    const size_t offset = index * 3;
    const int high = hexNibble(text[offset]);
    const int low = hexNibble(text[offset + 1]);
    if (high < 0 || low < 0 || (index < 5 && text[offset + 2] != ':')) {
      return false;
    }
    displayOrder[index] = static_cast<uint8_t>((high << 4) | low);
  }
  for (size_t index = 0; index < 6; ++index) {
    output[5 - index] = displayOrder[index];
  }
  return true;
}

const char *disconnectReasonName(ScaleDisconnectReason reason) {
  switch (reason) {
    case ScaleDisconnectReason::NONE: return "none";
    case ScaleDisconnectReason::USER_REQUEST: return "user request";
    case ScaleDisconnectReason::SCAN_START_FAILED: return "scan start failed";
    case ScaleDisconnectReason::SCAN_TIMEOUT: return "scan timeout";
    case ScaleDisconnectReason::CONNECT_FAILED: return "connect failed";
    case ScaleDisconnectReason::DISCOVERY_FAILED: return "discovery failed";
    case ScaleDisconnectReason::UNSUPPORTED_SCALE: return "unsupported scale";
    case ScaleDisconnectReason::SUBSCRIBE_FAILED: return "subscribe failed";
    case ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED:
      return "initialization write failed";
    case ScaleDisconnectReason::REMOTE_DISCONNECTED:
      return "remote disconnected";
    case ScaleDisconnectReason::FIRST_PACKET_TIMEOUT:
      return "first packet timeout";
    case ScaleDisconnectReason::PACKET_TIMEOUT: return "packet timeout";
    case ScaleDisconnectReason::INVALID_PACKET_STREAM:
      return "invalid packet stream";
    case ScaleDisconnectReason::COMMAND_WRITE_FAILED:
      return "command write failed";
    case ScaleDisconnectReason::SUPERVISION_TIMEOUT:
      return "supervision timeout";
    case ScaleDisconnectReason::CONNECTION_FAILED_TO_ESTABLISH:
      return "connection failed to be established";
    case ScaleDisconnectReason::RX_QUEUE_OVERFLOW:
      return "RX queue overflow";
    case ScaleDisconnectReason::EVENT_QUEUE_OVERFLOW:
      return "event queue overflow";
    case ScaleDisconnectReason::HOST_RESET: return "host reset";
    case ScaleDisconnectReason::OPERATION_TIMEOUT:
      return "operation timeout";
    case ScaleDisconnectReason::MBUF_ALLOCATION_FAILED:
      return "mbuf allocation failed";
  }
  return "unknown";
}

ScaleDisconnectReason mapRawDisconnectReason(int status) {
  if (status == 0x08 || status == BLE_HS_HCI_ERR(0x08)) {
    return ScaleDisconnectReason::SUPERVISION_TIMEOUT;
  }
  if (status == 0x3e || status == BLE_HS_HCI_ERR(0x3e)) {
    return ScaleDisconnectReason::CONNECTION_FAILED_TO_ESTABLISH;
  }
  return ScaleDisconnectReason::REMOTE_DISCONNECTED;
}

// NimBLE owns the timing of its callbacks, so clearing a bare singleton
// pointer is not enough to make destruction safe: a callback may already have
// copied the pointer. This registry lives for the lifetime of the firmware.
// A callback takes a lease before touching the client; destruction first
// prevents new leases and then waits for every existing lease to drain.
struct CallbackRegistry {
  portMUX_TYPE mux;
  void *owner;
  uint32_t activeCallbacks;
  uint32_t nextOperationId;
  bool accepting;
};

// Constant initialization is required because the facade itself is a global
// object in another translation unit.
CallbackRegistry callbackRegistry = {
    portMUX_INITIALIZER_UNLOCKED, nullptr, 0, 0, false};

bool registerCallbackOwner(void *owner) {
  portENTER_CRITICAL(&callbackRegistry.mux);
  const bool available = callbackRegistry.owner == nullptr &&
                         callbackRegistry.activeCallbacks == 0;
  if (available) {
    callbackRegistry.owner = owner;
    callbackRegistry.accepting = true;
  }
  portEXIT_CRITICAL(&callbackRegistry.mux);
  return available;
}

bool quiesceCallbackOwner(void *owner) {
  portENTER_CRITICAL(&callbackRegistry.mux);
  const bool registered = callbackRegistry.owner == owner;
  if (registered) {
    callbackRegistry.accepting = false;
  }
  portEXIT_CRITICAL(&callbackRegistry.mux);
  if (!registered) {
    return false;
  }

  // Callbacks execute on the NimBLE host task, not in an ISR. Yielding here
  // lets an in-flight callback finish without imposing a teardown timeout
  // that could reintroduce a use-after-free.
  while (true) {
    portENTER_CRITICAL(&callbackRegistry.mux);
    const bool quiescent = callbackRegistry.activeCallbacks == 0;
    portEXIT_CRITICAL(&callbackRegistry.mux);
    if (quiescent) {
      return true;
    }
    vTaskDelay(1);
  }
}

void releaseCallbackOwner(void *owner) {
  portENTER_CRITICAL(&callbackRegistry.mux);
  if (callbackRegistry.owner == owner &&
      callbackRegistry.activeCallbacks == 0) {
    callbackRegistry.owner = nullptr;
  }
  portEXIT_CRITICAL(&callbackRegistry.mux);
}

class CallbackLease {
 public:
  CallbackLease() {
    portENTER_CRITICAL(&callbackRegistry.mux);
    if (callbackRegistry.accepting && callbackRegistry.owner != nullptr) {
      owner_ = callbackRegistry.owner;
      ++callbackRegistry.activeCallbacks;
    }
    portEXIT_CRITICAL(&callbackRegistry.mux);
  }

  ~CallbackLease() {
    if (owner_ == nullptr) {
      return;
    }
    portENTER_CRITICAL(&callbackRegistry.mux);
    --callbackRegistry.activeCallbacks;
    portEXIT_CRITICAL(&callbackRegistry.mux);
  }

  void *owner() const { return owner_; }

  CallbackLease(const CallbackLease &) = delete;
  CallbackLease &operator=(const CallbackLease &) = delete;

 private:
  void *owner_ = nullptr;
};

uint32_t nextCallbackOperationId() {
  portENTER_CRITICAL(&callbackRegistry.mux);
  ++callbackRegistry.nextOperationId;
  if (callbackRegistry.nextOperationId == 0) {
    callbackRegistry.nextOperationId = 1;
  }
  const uint32_t operationId = callbackRegistry.nextOperationId;
  portEXIT_CRITICAL(&callbackRegistry.mux);
  return operationId;
}

class NimbleScaleClient {
  friend struct NimbleScaleClientTest;
 public:
  explicit NimbleScaleClient(bool debug) : debug_(debug) {
    writeSignal_ = xSemaphoreCreateBinaryStatic(&writeSignalStorage_);
    callbackOwner_ = registerCallbackOwner(this);
  }

  ~NimbleScaleClient() {
    // Unpublish before cancellation. Cancellation and termination may cause
    // more host events, which must be discarded rather than enter an object
    // whose teardown has started.
    if (callbackOwner_) {
      (void)quiesceCallbackOwner(this);
      finishLink(true, ScaleDisconnectReason::USER_REQUEST, 0);
      releaseCallbackOwner(this);
      callbackOwner_ = false;
    } else {
      finishLink(true, ScaleDisconnectReason::USER_REQUEST, 0);
    }
    vSemaphoreDelete(writeSignal_);
  }

  bool startScan(const char *mac, bool forceRestart, uint16_t interval,
                 uint16_t window, bool addressScan) {
    service();
    if (!callbackOwner_) {
      lastReason_ = ScaleDisconnectReason::SCAN_START_FAILED;
      lastRawStatus_ = BLE_HS_EBUSY;
      return false;
    }
    if (scaleProtocolCount() > kProtocolCapacity) {
      lastReason_ = ScaleDisconnectReason::UNSUPPORTED_SCALE;
      lastRawStatus_ = BLE_HS_EINVAL;
      return false;
    }
    if (!shotStopperBleRuntimeReady()) {
      lastReason_ = ScaleDisconnectReason::SCAN_START_FAILED;
      lastRawStatus_ = BLE_HS_ENOTSYNCED;
      return false;
    }
    if (window == 0 || window > interval) {
      interval = BLE_SCAN_NORMAL_INTERVAL;
      window = BLE_SCAN_NORMAL_WINDOW;
    }

    uint8_t parsedFilter[6] = {};
    const bool filtered = mac != nullptr && mac[0] != '\0';
    if (filtered && !parseAddress(mac, parsedFilter)) {
      lastReason_ = ScaleDisconnectReason::SCAN_START_FAILED;
      lastRawStatus_ = BLE_HS_EINVAL;
      return false;
    }
    const bool useAddressScan = filtered && addressScan;
    const bool sameTarget =
        scanAddressFilter_ == useAddressScan && filterPresent_ == filtered &&
        (!filtered || addressEqual(filterAddress_, parsedFilter));
    const bool sameScan = scanInterval_ == interval && scanWindow_ == window;
    if ((state_ == State::Scanning || state_ == State::Backoff) &&
        sameTarget && ((!forceRestart && sameScan) || state_ == State::Backoff)) {
      return true;
    }
    if (state_ == State::Backoff && !sameTarget) {
      backoff_.reset();
    }

    if (state_ != State::Idle) {
      finishLink(true, ScaleDisconnectReason::NONE, 0);
    }
    scanInterval_ = interval;
    scanWindow_ = window;
    portENTER_CRITICAL(&mux_);
    filterPresent_ = filtered;
    if (filtered) {
      memcpy(filterAddress_, parsedFilter, sizeof(filterAddress_));
    } else {
      memset(filterAddress_, 0, sizeof(filterAddress_));
    }
    portEXIT_CRITICAL(&mux_);
    scanAddressFilter_ = useAddressScan;
    if (backoff_.active(nowMs())) {
      enterState(State::Backoff, backoff_.remainingMs(nowMs()));
      return true;
    }
    return beginConfiguredScan(forceRestart);
  }

  bool poll() {
    service();
    return state_ == State::Ready;
  }

  bool isScanning() const {
    return state_ == State::Scanning || state_ == State::Backoff;
  }

  bool isConnecting() const {
    return state_ == State::CancelPending || state_ == State::Settling ||
           state_ == State::Connecting ||
           state_ == State::DiscoveringServices ||
           state_ == State::DiscoveringCharacteristics ||
           state_ == State::DiscoveringDescriptors ||
           state_ == State::Subscribing || state_ == State::Initializing;
  }

  void disconnect() {
    finishLink(true, ScaleDisconnectReason::USER_REQUEST, 0);
  }

  bool isConnected() {
    service();
    return state_ == State::Ready;
  }

  bool isLinkUp() const { return state_ == State::Ready; }

  bool newWeightAvailable() {
    service();
    if (state_ != State::Ready || protocol_ == nullptr) {
      return false;
    }
    if (!hasValidPacket_ && elapsedMs(connectedAt_) >= FIRST_PACKET_TIMEOUT_MS) {
      finishLink(true, ScaleDisconnectReason::FIRST_PACKET_TIMEOUT,
                 BLE_HS_ETIMEOUT);
      return false;
    }
    if (hasValidPacket_ && elapsedMs(lastPacket_) >= maxPacketPeriodMs()) {
      finishLink(true, ScaleDisconnectReason::PACKET_TIMEOUT,
                 BLE_HS_ETIMEOUT);
      return false;
    }

    RxFrame frame = {};
    while (popRx(frame)) {
      if (!supportedPacketLength(frame.length)) {
        rejectPacket();
        continue;
      }
      float weight = 0.0f;
      uint32_t timerMs = 0;
      const bool hasWeight = protocol_->parseWeight != nullptr &&
                             protocol_->parseWeight(frame.data, frame.length,
                                                    &weight);
      const bool hasTimer = protocol_->parseTimer != nullptr &&
                            protocol_->parseTimer(frame.data, frame.length,
                                                  &timerMs);
      if (!hasWeight && !hasTimer) {
        rejectPacket();
        continue;
      }
      if (hasValidPacket_) {
        packetPeriod_ = frame.receivedAtMs - lastPacket_;
      }
      lastPacket_ = frame.receivedAtMs;
      hasValidPacket_ = true;
      consecutiveRejectedPackets_ = 0;
      if (hasTimer) {
        currentTimerMs_ = timerMs;
        lastTimerPacket_ = frame.receivedAtMs;
        hasTimer_ = true;
      }
      if (hasWeight) {
        backoff_.reset();
        portENTER_CRITICAL(&mux_);
        if (!timing_.has(ScaleBleTimingFirstWeight)) {
          timing_.firstWeightMs = frame.receivedAtMs;
          timing_.recordedFlags |= ScaleBleTimingFirstWeight;
          if (timing_.has(ScaleBleTimingFirstCompatibleAdvertisement)) {
            lastAdvertisementToFirstWeightMs_ =
                timing_.firstWeightMs - timing_.firstCompatibleAdvertisementMs;
          }
          if (timing_.has(ScaleBleTimingReady)) {
            lastReadyToFirstWeightMs_ = timing_.firstWeightMs - timing_.readyMs;
          }
        }
        portEXIT_CRITICAL(&mux_);
        portENTER_CRITICAL(&advertMux_);
        negativeCache_.erase(peerKey(selectedAddress_));
        portEXIT_CRITICAL(&advertMux_);
        currentWeight_ = weight;
        return true;
      }
    }
    return false;
  }

  ScaleCommandResult writeOp(ScaleOp op, uint8_t arg = 0) {
    if (!isConnected()) {
      return ScaleCommandResult::NotConnected;
    }
    if (protocol_ == nullptr || protocol_->encodeCommand == nullptr) {
      return ScaleCommandResult::Unsupported;
    }
    uint8_t command[SCALE_MAX_COMMAND_LENGTH] = {};
    int length = 0;
    if (!protocol_->encodeCommand(op, arg, command, &length) || length <= 0 ||
        length > SCALE_MAX_COMMAND_LENGTH) {
      return ScaleCommandResult::Unsupported;
    }
    commandStartedAt_ = nowMs();
    activeCommand_ = static_cast<uint8_t>(op);
    const ScaleCommandResult result =
        writeCommand(command, static_cast<uint16_t>(length));
    activeCommand_ = 0xff;
    return result;
  }

  ScaleFeatureSet features() const {
    return state_ == State::Ready && protocol_ != nullptr
               ? protocol_->features
               : scaleFeatureSetNone();
  }

  bool heartbeatRequired() const {
    if (!features().has(ScaleFeatureHeartbeat)) {
      return false;
    }
    const uint16_t period = protocol_->features.heartbeatPeriodMs != 0
                                ? protocol_->features.heartbeatPeriodMs
                                : HEARTBEAT_PERIOD_MS;
    return elapsedMs(lastHeartbeat_) >= period;
  }

  void noteHeartbeat() { lastHeartbeat_ = nowMs(); }

  bool hasTimer() const { return state_ == State::Ready && hasTimer_; }
  uint32_t timerMs() const { return hasTimer_ ? currentTimerMs_ : 0; }

  uint32_t timerAgeMs() const {
    return hasTimer_ ? elapsedMs(lastTimerPacket_) : 0xffffffffUL;
  }

  const char *protocolName() const {
    return state_ == State::Ready && protocol_ != nullptr ? protocol_->id
                                                          : "none";
  }

  const char *address() const { return identityPresent_ ? address_ : ""; }
  const char *name() const { return identityPresent_ ? name_ : ""; }
  bool directedScan() const {
    return (state_ == State::Scanning || state_ == State::Backoff) &&
           filterPresent_;
  }

  bool takeSeenAdvertisement(char *macOut, size_t macCapacity, char *nameOut,
                             size_t nameCapacity) {
    portENTER_CRITICAL(&mux_);
    const bool pending = seenPending_;
    if (pending) {
      if (macOut != nullptr && macCapacity != 0) {
        strncpy(macOut, seenAddress_, macCapacity - 1);
        macOut[macCapacity - 1] = '\0';
      }
      if (nameOut != nullptr && nameCapacity != 0) {
        strncpy(nameOut, seenName_, nameCapacity - 1);
        nameOut[nameCapacity - 1] = '\0';
      }
      seenPending_ = false;
    }
    portEXIT_CRITICAL(&mux_);
    return pending;
  }

  ScaleDisconnectReason lastReason() const { return lastReason_; }
  int32_t lastRawStatus() const { return lastRawStatus_; }
  ScaleBleDiagnostics diagnostics() const { return diagnostics_; }
  uint8_t connectAttempts() const { return connectAttempts_; }
  uint8_t stateId() const { return static_cast<uint8_t>(state_); }
  uint32_t lastPacketAgeMs() const {
    return hasValidPacket_ ? elapsedMs(lastPacket_) : 0xffffffffUL;
  }
  uint32_t rejectedPackets() const {
    portENTER_CRITICAL(&mux_);
    const uint32_t result = rejectedPackets_;
    portEXIT_CRITICAL(&mux_);
    return result;
  }
  uint32_t reconnects() const { return reconnects_; }
  ScaleBleTimingSnapshot timing() const {
    portENTER_CRITICAL(&mux_);
    const ScaleBleTimingSnapshot snapshot = timing_;
    portEXIT_CRITICAL(&mux_);
    return snapshot;
  }
  float weight() const { return currentWeight_; }

  int rssi() const {
    if (state_ != State::Ready || connectionHandle_ == kInvalidHandle) {
      return SCALE_LINK_RSSI_UNAVAILABLE;
    }
    int8_t value = 0;
    return ble_gap_conn_rssi(connectionHandle_, &value) == 0
               ? static_cast<int>(value)
               : SCALE_LINK_RSSI_UNAVAILABLE;
  }

  uint16_t rxHighWater() const {
    portENTER_CRITICAL(&mux_);
    const uint16_t result = rxHighWater_;
    portEXIT_CRITICAL(&mux_);
    return result;
  }
  uint32_t rxDrops() const {
    portENTER_CRITICAL(&mux_);
    const uint32_t result = rxDrops_;
    portEXIT_CRITICAL(&mux_);
    return result;
  }

  ScaleBleBackendHealth health() const {
    ScaleBleBackendHealth result = {};
    portENTER_CRITICAL(&mux_);
    result.generation = generation_;
    result.operationId = operationId_;
    result.stateAgeMs = elapsedMs(stateEnteredAtMs_);
    result.advertisementsSeen = advertisementsSeen_;
    result.compatibleAdvertisements = compatibleAdvertisements_;
    result.discardedAdvertisements = discardedAdvertisements_;
    result.malformedAdvertisements = malformedAdvertisements_;
    result.negativeCacheHits = negativeCacheHits_;
    result.negativeCacheInsertions = negativeCacheInsertions_;
    result.scanStarts = scanStarts_;
    result.scanCancels = scanCancels_;
    result.scanRestarts = scanRestarts_;
    result.connectAttempts = connectAttemptsTotal_;
    result.connectionFailures = connectionFailures_;
    result.discoveryFailures = discoveryFailures_;
    result.subscriptionFailures = subscriptionFailures_;
    result.writeFailures = writeFailures_;
    result.staleCallbacks = staleCallbacks_;
    result.criticalEventDrops = criticalEvents_.drops();
    result.controlEventDrops = controlEvents_.drops();
    result.rxDrops = rxDrops_;
    result.mbufFailures = mbufFailures_;
    result.cleanupCount = cleanupCount_;
    result.duplicateCleanups = duplicateCleanups_;
    result.teardownFailures = teardownFailures_;
    result.backoffCount = backoffCount_;
    result.lastAdvertisementToConnectMs = lastAdvertisementToConnectMs_;
    result.lastAdvertisementToReadyMs = lastAdvertisementToReadyMs_;
    result.lastAdvertisementToFirstWeightMs =
        lastAdvertisementToFirstWeightMs_;
    result.lastReadyToFirstWeightMs = lastReadyToFirstWeightMs_;
    result.criticalEventHighWater =
        static_cast<uint16_t>(criticalEvents_.highWater());
    result.controlEventHighWater =
        static_cast<uint16_t>(controlEvents_.highWater());
    result.rxHighWater = rxHighWater_;
    result.state = static_cast<uint8_t>(state_);
    result.backoffFailures = backoff_.failureCount();
    portEXIT_CRITICAL(&mux_);
    portENTER_CRITICAL(&advertMux_);
    result.negativeCacheEntries = static_cast<uint8_t>(
        negativeCache_.activeCount(nowMs()));
    portEXIT_CRITICAL(&advertMux_);
    return result;
  }

 private:
  enum class State : uint8_t {
    Idle,
    Scanning,
    CancelPending,
    Settling,
    Connecting,
    DiscoveringServices,
    DiscoveringCharacteristics,
    DiscoveringDescriptors,
    Subscribing,
    Initializing,
    Ready,
    Backoff
  };

  enum class EventType : uint8_t {
    Candidate,
    ScanComplete,
    ConnectComplete,
    Disconnected,
    ServicesComplete,
    CharacteristicsComplete,
    DescriptorsComplete,
    WriteComplete
  };

  enum class WritePurpose : uint8_t { None, Subscribe, Initialize, Command };

  enum class CallbackDomain : uint8_t { Scan, Link, Gatt };

  struct Event {
    EventType type;
    uint32_t generation;
    uint32_t operationId;
    int32_t status;
    uint16_t connectionHandle;
    ble_addr_t address;
    char name[SCALE_NAME_CAPACITY];
  };

  struct Candidate {
    ble_addr_t address;
    NimbleAdvertisementData advertisement;
    uint32_t sequence;
    bool used;
    bool connectable;
  };

  struct ServiceRange {
    uint16_t start;
    uint16_t end;
  };

  struct ProtocolHandles {
    uint16_t read;
    uint16_t readEnd;
    uint16_t write;
    uint8_t readProperties;
    uint8_t writeProperties;
    uint8_t readService;
  };

  struct RxFrame {
    uint32_t generation;
    uint32_t operationId;
    uint32_t receivedAtMs;
    uint8_t length;
    uint8_t data[MAX_BLE_PACKET_LENGTH];
  };

  static void *callbackArg(uint32_t operationId) {
    return reinterpret_cast<void *>(static_cast<uintptr_t>(operationId));
  }

  void noteStaleCallback() {
    portENTER_CRITICAL(&mux_);
    ++staleCallbacks_;
    portEXIT_CRITICAL(&mux_);
  }

  bool callbackMatches(uint32_t actual, CallbackDomain domain) {
    portENTER_CRITICAL(&mux_);
    const uint32_t expected =
        domain == CallbackDomain::Scan
            ? scanOperationId_
            : (domain == CallbackDomain::Link ? linkOperationId_
                                               : gattOperationId_);
    const bool matches = actual != 0 && actual == expected;
    if (!matches) {
      ++staleCallbacks_;
    }
    portEXIT_CRITICAL(&mux_);
    return matches;
  }

  static int gapCallback(ble_gap_event *event, void *arg) {
    CallbackLease lease;
    auto *client = static_cast<NimbleScaleClient *>(lease.owner());
    return client == nullptr
               ? 0
               : client->onGapEvent(
                     event, static_cast<uint32_t>(
                                reinterpret_cast<uintptr_t>(arg)));
  }

  static int serviceCallback(uint16_t connectionHandle,
                             const ble_gatt_error *error,
                             const ble_gatt_svc *service, void *arg) {
    CallbackLease lease;
    auto *client = static_cast<NimbleScaleClient *>(lease.owner());
    return client == nullptr
               ? 0
               : client->onService(
                     connectionHandle, error, service,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)));
  }

  static int characteristicCallback(uint16_t connectionHandle,
                                    const ble_gatt_error *error,
                                    const ble_gatt_chr *characteristic,
                                    void *arg) {
    CallbackLease lease;
    auto *client = static_cast<NimbleScaleClient *>(lease.owner());
    return client == nullptr
               ? 0
               : client->onCharacteristic(
                     connectionHandle, error, characteristic,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)));
  }

  static int descriptorCallback(uint16_t connectionHandle,
                                const ble_gatt_error *error,
                                uint16_t characteristicValueHandle,
                                const ble_gatt_dsc *descriptor, void *arg) {
    CallbackLease lease;
    auto *client = static_cast<NimbleScaleClient *>(lease.owner());
    return client == nullptr
               ? 0
               : client->onDescriptor(
                     connectionHandle, error, characteristicValueHandle,
                     descriptor,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)));
  }

  static int writeCallback(uint16_t connectionHandle,
                           const ble_gatt_error *error, ble_gatt_attr *,
                           void *arg) {
    CallbackLease lease;
    auto *client = static_cast<NimbleScaleClient *>(lease.owner());
    return client == nullptr
               ? 0
               : client->onWrite(
                     connectionHandle, error,
                     static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg)));
  }

  int onGapEvent(ble_gap_event *event, uint32_t operationId) {
    if (event == nullptr) {
      return 0;
    }
    switch (event->type) {
      case BLE_GAP_EVENT_DISC:
        if (callbackMatches(operationId, CallbackDomain::Scan)) {
          onAdvertisement(event->disc, operationId);
        }
        return 0;
      case BLE_GAP_EVENT_DISC_COMPLETE:
        if (callbackMatches(operationId, CallbackDomain::Scan)) {
          pushCriticalEvent(EventType::ScanComplete,
                            event->disc_complete.reason, kInvalidHandle,
                            operationId);
        }
        return 0;
      case BLE_GAP_EVENT_CONNECT:
        if (callbackMatches(operationId, CallbackDomain::Link)) {
          pushCriticalEvent(EventType::ConnectComplete, event->connect.status,
                            event->connect.conn_handle, operationId);
        }
        return 0;
      case BLE_GAP_EVENT_DISCONNECT:
        if (callbackMatches(operationId, CallbackDomain::Link)) {
          pushCriticalEvent(EventType::Disconnected, event->disconnect.reason,
                            event->disconnect.conn.conn_handle, operationId);
        }
        return 0;
      case BLE_GAP_EVENT_NOTIFY_RX:
        if (callbackMatches(operationId, CallbackDomain::Link)) {
          onNotification(event->notify_rx.conn_handle,
                         event->notify_rx.attr_handle, event->notify_rx.om,
                         operationId);
        }
        return 0;
      default:
        return 0;
    }
  }

  Candidate *candidateFor(const ble_addr_t &address) {
    Candidate *oldest = &candidates_[0];
    for (auto & candidate : candidates_) {
      if (candidate.used && candidate.address.type == address.type &&
          addressEqual(candidate.address.val, address.val)) {
        return &candidate;
      }
      if (!candidate.used) {
        oldest = &candidate;
        break;
      }
      if (candidate.sequence < oldest->sequence) {
        oldest = &candidate;
      }
    }
    *oldest = {};
    oldest->used = true;
    oldest->address = address;
    return oldest;
  }

  void onAdvertisement(const ble_gap_disc_desc &discovery,
                       uint32_t operationId) {
    portENTER_CRITICAL(&mux_);
    const bool scanning =
        (state_ == State::Scanning || state_ == State::Backoff) &&
        operationId == scanOperationId_;
    portEXIT_CRITICAL(&mux_);
    if (!scanning) {
      return;
    }
    portENTER_CRITICAL(&mux_);
    ++advertisementsSeen_;
    portEXIT_CRITICAL(&mux_);
    ble_hs_adv_fields fields = {};
    if (ble_hs_adv_parse_fields(&fields, discovery.data,
                                discovery.length_data) != 0) {
      portENTER_CRITICAL(&mux_);
      ++malformedAdvertisements_;
      portEXIT_CRITICAL(&mux_);
      return;
    }
    portENTER_CRITICAL(&advertMux_);
    Candidate *candidate = candidateFor(discovery.addr);
    candidate->sequence = ++candidateSequence_;
    if (discovery.event_type == BLE_HCI_ADV_RPT_EVTYPE_ADV_IND ||
        discovery.event_type == BLE_HCI_ADV_RPT_EVTYPE_DIR_IND) {
      candidate->connectable = true;
    }
    nimbleAccumulateAdvertisementName(
        fields.name, fields.name_len, fields.name_is_complete != 0,
        candidate->advertisement);
    for (uint8_t index = 0; index < fields.num_uuids16; ++index) {
      nimbleAccumulateAdvertisementUuid16(
          ble_uuid_u16(&fields.uuids16[index].u), candidate->advertisement);
    }

    const bool compatible =
        nimbleAdvertisementIsCompatible(candidate->advertisement);
    bool filterPresent = false;
    uint8_t filterAddress[6] = {};
    portENTER_CRITICAL(&mux_);
    filterPresent = filterPresent_;
    memcpy(filterAddress, filterAddress_, sizeof(filterAddress));
    portEXIT_CRITICAL(&mux_);
    const bool addressMatches =
        filterPresent && addressEqual(filterAddress, discovery.addr.val);
    const uint32_t receivedAtMs = nowMs();
    const NimblePeerKey peer = peerKey(discovery.addr);
    const bool negativeCached =
        (compatible || addressMatches) && negativeCache_.contains(peer, receivedAtMs);
    if (compatible || addressMatches) {
      portENTER_CRITICAL(&mux_);
      ++compatibleAdvertisements_;
      if (!timing_.has(ScaleBleTimingFirstCompatibleAdvertisement)) {
        timing_.firstCompatibleAdvertisementMs = receivedAtMs;
        timing_.recordedFlags |=
            ScaleBleTimingFirstCompatibleAdvertisement;
      }
      if (compatible) {
        formatAddress(candidate->address.val, seenAddress_,
                      sizeof(seenAddress_));
        strncpy(seenName_, candidate->advertisement.name,
                sizeof(seenName_) - 1);
        seenName_[sizeof(seenName_) - 1] = '\0';
        seenPending_ = true;
      }
      portEXIT_CRITICAL(&mux_);
    }
    if (negativeCached) {
      portENTER_CRITICAL(&mux_);
      ++negativeCacheHits_;
      ++discardedAdvertisements_;
      portEXIT_CRITICAL(&mux_);
      portEXIT_CRITICAL(&advertMux_);
      return;
    }
    if (candidate->connectable &&
        ((!filterPresent && compatible) || addressMatches)) {
      Event selected = {};
      selected.type = EventType::Candidate;
      selected.address = candidate->address;
      memcpy(selected.name, candidate->advertisement.name,
             sizeof(selected.name));
      bool shouldQueue = false;
      portENTER_CRITICAL(&mux_);
      if (state_ == State::Scanning &&
          operationId == scanOperationId_ && !candidateQueued_) {
        candidateQueued_ = true;
        selected.generation = generation_;
        selected.operationId = operationId;
        candidateMailbox_ = selected;
        candidatePending_ = true;
        shouldQueue = true;
      }
      portEXIT_CRITICAL(&mux_);
      (void)shouldQueue;
    } else if (!compatible && !addressMatches) {
      portENTER_CRITICAL(&mux_);
      ++discardedAdvertisements_;
      portEXIT_CRITICAL(&mux_);
    }
    portEXIT_CRITICAL(&advertMux_);
  }

  void onNotification(uint16_t connectionHandle, uint16_t attributeHandle,
                      os_mbuf *buffer, uint32_t operationId) {
    if (buffer == nullptr) {
      return;
    }
    portENTER_CRITICAL(&mux_);
    const bool current = operationId == linkOperationId_ &&
                         connectionHandle == connectionHandle_ &&
                         attributeHandle == readHandle_;
    if (!current) {
      ++staleCallbacks_;
    }
    portEXIT_CRITICAL(&mux_);
    if (!current) {
      return;
    }
    const uint16_t length = OS_MBUF_PKTLEN(buffer);
    if (length == 0 || length > MAX_BLE_PACKET_LENGTH) {
      portENTER_CRITICAL(&mux_);
      ++rejectedPackets_;
      if (invalidNotificationStreak_ != 0xff) {
        ++invalidNotificationStreak_;
      }
      if (invalidNotificationStreak_ >= MAX_CONSECUTIVE_REJECTED_PACKETS) {
        invalidNotificationStream_ = true;
      }
      portEXIT_CRITICAL(&mux_);
      return;
    }
    RxFrame frame = {};
    frame.operationId = operationId;
    frame.receivedAtMs = nowMs();
    frame.length = static_cast<uint8_t>(length);
    if (os_mbuf_copydata(buffer, 0, length, frame.data) != 0) {
      portENTER_CRITICAL(&mux_);
      ++mbufFailures_;
      mbufFailed_ = true;
      portEXIT_CRITICAL(&mux_);
      return;
    }
    portENTER_CRITICAL(&mux_);
    if (operationId != linkOperationId_ ||
        connectionHandle != connectionHandle_ ||
        attributeHandle != readHandle_) {
      ++staleCallbacks_;
    } else if (rxCount_ == kRxFrameCount) {
      ++rxDrops_;
      rxOverflowed_ = true;
    } else {
      invalidNotificationStreak_ = 0;
      frame.generation = generation_;
      rxFrames_[rxTail_] = frame;
      rxTail_ = (rxTail_ + 1) % kRxFrameCount;
      ++rxCount_;
      if (rxCount_ > rxHighWater_) {
        rxHighWater_ = rxCount_;
      }
    }
    portEXIT_CRITICAL(&mux_);
  }

  int onService(uint16_t connectionHandle, const ble_gatt_error *error,
                const ble_gatt_svc *service, uint32_t operationId) {
    if (!callbackMatches(operationId, CallbackDomain::Gatt)) {
      return 0;
    }
    if (error == nullptr) {
      return 0;
    }
    if (error->status == 0 && service != nullptr) {
      portENTER_CRITICAL(&mux_);
      const bool current = operationId == gattOperationId_ &&
                           connectionHandle == connectionHandle_;
      if (!current) {
        ++staleCallbacks_;
      } else if (serviceCount_ < kServiceCount) {
        services_[serviceCount_++] = {service->start_handle,
                                      service->end_handle};
      } else {
        serviceOverflowed_ = true;
      }
      portEXIT_CRITICAL(&mux_);
      return 0;
    }
    pushControlEvent(EventType::ServicesComplete, error->status,
                     connectionHandle, operationId);
    return 0;
  }

  int onCharacteristic(uint16_t connectionHandle,
                       const ble_gatt_error *error,
                       const ble_gatt_chr *characteristic,
                       uint32_t operationId) {
    if (!callbackMatches(operationId, CallbackDomain::Gatt)) {
      return 0;
    }
    if (error == nullptr) {
      return 0;
    }
    if (error->status == 0 && characteristic != nullptr) {
      for (size_t index = 0; index < scaleProtocolCount(); ++index) {
        const ScaleProtocol *protocol = scaleProtocolAt(index);
        ble_uuid_any_t uuid = {};
        const bool readMatch =
            protocol != nullptr &&
            ble_uuid_from_str(&uuid, protocol->readUuid) == 0 &&
            ble_uuid_cmp(&uuid.u, &characteristic->uuid.u) == 0;
        const bool writeMatch =
            protocol != nullptr &&
            ble_uuid_from_str(&uuid, protocol->writeUuid) == 0 &&
            ble_uuid_cmp(&uuid.u, &characteristic->uuid.u) == 0;
        portENTER_CRITICAL(&mux_);
        const bool current = operationId == gattOperationId_ &&
                             connectionHandle == connectionHandle_ &&
                             serviceIndex_ < serviceCount_;
        if (current) {
          ProtocolHandles &handles = protocolHandles_[index];
          if (handles.read != 0 && handles.readService == serviceIndex_ &&
              characteristic->def_handle > handles.read &&
              characteristic->def_handle - 1 < handles.readEnd) {
            handles.readEnd = characteristic->def_handle - 1;
          }
          if (readMatch && handles.read == 0) {
            handles.read = characteristic->val_handle;
            handles.readEnd = services_[serviceIndex_].end;
            handles.readProperties = characteristic->properties;
            handles.readService = static_cast<uint8_t>(serviceIndex_);
          }
          if (writeMatch && handles.write == 0) {
            handles.write = characteristic->val_handle;
            handles.writeProperties = characteristic->properties;
          }
        } else {
          ++staleCallbacks_;
        }
        portEXIT_CRITICAL(&mux_);
        if (!current) {
          return 0;
        }
      }
      return 0;
    }
    pushControlEvent(EventType::CharacteristicsComplete, error->status,
                     connectionHandle, operationId);
    return 0;
  }

  int onDescriptor(uint16_t connectionHandle, const ble_gatt_error *error,
                   uint16_t, const ble_gatt_dsc *descriptor,
                   uint32_t operationId) {
    if (!callbackMatches(operationId, CallbackDomain::Gatt)) {
      return 0;
    }
    if (error == nullptr) {
      return 0;
    }
    if (error->status == 0 && descriptor != nullptr) {
      portENTER_CRITICAL(&mux_);
      const bool current = operationId == gattOperationId_ &&
                           connectionHandle == connectionHandle_;
      if (!current) {
        ++staleCallbacks_;
      } else if (ble_uuid_u16(&descriptor->uuid.u) ==
                 BLE_GATT_DSC_CLT_CFG_UUID16) {
        cccdHandle_ = descriptor->handle;
      }
      portEXIT_CRITICAL(&mux_);
      return 0;
    }
    pushControlEvent(EventType::DescriptorsComplete, error->status,
                     connectionHandle, operationId);
    return 0;
  }

  int onWrite(uint16_t connectionHandle, const ble_gatt_error *error,
              uint32_t operationId) {
    if (!callbackMatches(operationId, CallbackDomain::Gatt)) {
      return 0;
    }
    if (error == nullptr) {
      return 0;
    }
    TaskHandle_t waiter = nullptr;
    WritePurpose purpose = WritePurpose::None;
    portENTER_CRITICAL(&mux_);
    const bool current = operationId == gattOperationId_ &&
                         connectionHandle == connectionHandle_ &&
                         writePurpose_ != WritePurpose::None && !writeCompleted_;
    if (current) {
      purpose = writePurpose_;
      writeResult_ = error->status;
      writeCompleted_ = true;
      if (error->status == BLE_HS_ENOMEM) {
        ++mbufFailures_;
      }
      waiter = writeWaiter_;
      if (purpose != WritePurpose::Command) {
        writePurpose_ = WritePurpose::None;
      }
    } else {
      ++staleCallbacks_;
    }
    portEXIT_CRITICAL(&mux_);
    if (!current) {
      return 0;
    }
    if (purpose == WritePurpose::Command && waiter != nullptr) {
      xSemaphoreGive(writeSignal_);
    } else if (purpose != WritePurpose::None) {
      pushControlEvent(EventType::WriteComplete, error->status,
                       connectionHandle, operationId);
    }
    return 0;
  }

  bool pushControlEvent(EventType type, int32_t status,
                        uint16_t connectionHandle, uint32_t operationId) {
    Event event = {};
    event.type = type;
    event.operationId = operationId;
    event.status = status;
    event.connectionHandle = connectionHandle;
    bool pushed = false;
    portENTER_CRITICAL(&mux_);
    event.generation = generation_;
    pushed = controlEvents_.push(event);
    if (!pushed) {
      eventOverflowed_ = true;
    }
    portEXIT_CRITICAL(&mux_);
    return pushed;
  }

  bool pushCriticalEvent(EventType type, int32_t status,
                         uint16_t connectionHandle, uint32_t operationId) {
    Event event = {};
    event.type = type;
    event.operationId = operationId;
    event.status = status;
    event.connectionHandle = connectionHandle;
    portENTER_CRITICAL(&mux_);
    event.generation = generation_;
    const bool pushed = criticalEvents_.push(event);
    if (!pushed) {
      criticalOverflowed_ = true;
    }
    TaskHandle_t waiter = nullptr;
    if (type == EventType::Disconnected &&
        operationId == linkOperationId_ && connectionHandle == connectionHandle_) {
      waiter = writeWaiter_;
      writeResult_ = status;
      writeInterrupted_ = true;
      pendingDisconnect_ = true;
      pendingDisconnectStatus_ = status;
    }
    portEXIT_CRITICAL(&mux_);
    if (waiter != nullptr) {
      xSemaphoreGive(writeSignal_);
    }
    return pushed;
  }

  bool popCriticalEvent(Event &event) {
    portENTER_CRITICAL(&mux_);
    const bool present = criticalEvents_.pop(event);
    portEXIT_CRITICAL(&mux_);
    return present;
  }

  bool popControlEvent(Event &event) {
    portENTER_CRITICAL(&mux_);
    const bool present = controlEvents_.pop(event);
    portEXIT_CRITICAL(&mux_);
    return present;
  }

  bool popCandidate(Event &event) {
    portENTER_CRITICAL(&mux_);
    const bool present = candidatePending_;
    if (present) {
      event = candidateMailbox_;
      candidatePending_ = false;
    }
    portEXIT_CRITICAL(&mux_);
    return present;
  }

  bool popRx(RxFrame &frame) {
    portENTER_CRITICAL(&mux_);
    const bool present = rxCount_ != 0;
    if (present) {
      frame = rxFrames_[rxHead_];
      rxHead_ = (rxHead_ + 1) % kRxFrameCount;
      --rxCount_;
    }
    portEXIT_CRITICAL(&mux_);
    return present && frame.generation == generation_ &&
           frame.operationId == linkOperationId_;
  }

  static NimblePeerKey peerKey(const ble_addr_t &address) {
    NimblePeerKey key = {};
    memcpy(key.address, address.val, sizeof(key.address));
    key.type = address.type;
    return key;
  }

  uint32_t nextOperationIdLocked() {
    // The ID is process-wide, not object-local. If a canceled NimBLE
    // operation reports late after a new facade has registered, it cannot
    // accidentally match the new object's first operation.
    operationId_ = nextCallbackOperationId();
    return operationId_;
  }

  uint32_t beginOperation(CallbackDomain domain) {
    portENTER_CRITICAL(&mux_);
    const uint32_t id = nextOperationIdLocked();
    if (domain == CallbackDomain::Scan) {
      scanOperationId_ = id;
    } else if (domain == CallbackDomain::Link) {
      linkOperationId_ = id;
    } else {
      gattOperationId_ = id;
    }
    portEXIT_CRITICAL(&mux_);
    return id;
  }

  uint32_t beginGeneration() {
    portENTER_CRITICAL(&mux_);
    ++generation_;
    if (generation_ == 0) {
      generation_ = 1;
    }
    const uint32_t generation = generation_;
    portEXIT_CRITICAL(&mux_);
    return generation;
  }

  void invalidateGeneration(ScaleDisconnectReason &reason, int32_t &rawStatus,
                            bool &terminatePeer) {
    portENTER_CRITICAL(&mux_);
    // A GAP callback can arrive after the owner's last service() call. Claim
    // its evidence atomically with invalidation before clearing the queues.
    if (pendingDisconnect_ && activeCommand_ != 0xff &&
        (reason == ScaleDisconnectReason::COMMAND_WRITE_FAILED ||
         reason == ScaleDisconnectReason::MBUF_ALLOCATION_FAILED)) {
      rawStatus = pendingDisconnectStatus_;
      reason = mapRawDisconnectReason(rawStatus);
      terminatePeer = false;
    }
    pendingDisconnect_ = false;
    ++generation_;
    if (generation_ == 0) {
      generation_ = 1;
    }
    scanOperationId_ = 0;
    linkOperationId_ = 0;
    gattOperationId_ = 0;
    portEXIT_CRITICAL(&mux_);
  }

  void enterState(State state, uint32_t timeoutMs = 0) {
    portENTER_CRITICAL(&mux_);
    state_ = state;
    stateEnteredAtMs_ = nowMs();
    stateTimeoutMs_ = timeoutMs;
    stateDeadlineArmed_ = timeoutMs != 0;
    portEXIT_CRITICAL(&mux_);
  }

  bool beginConfiguredScan(bool restart, State initialState = State::Scanning,
                           uint32_t stateTimeoutMs = 0) {
    beginGeneration();
    lifecycleActive_ = true;
    syncGeneration_ = shotStopperBleRuntimeSyncGeneration();
    clearScanData();
    portENTER_CRITICAL(&mux_);
    timing_ = {};
    portEXIT_CRITICAL(&mux_);
    const uint32_t scanOperationId = beginOperation(CallbackDomain::Scan);

    ble_gap_disc_params params = {};
    params.passive = 0;
    params.filter_duplicates = 0;
    params.itvl = scanInterval_;
    params.window = scanWindow_;
    params.filter_policy = 0;
    params.limited = 0;
    const int rc = ble_gap_disc(shotStopperBleRuntimeOwnAddressType(),
                                BLE_HS_FOREVER, &params, gapCallback,
                                callbackArg(scanOperationId));
    if (rc != 0) {
      finishLink(false, ScaleDisconnectReason::SCAN_START_FAILED, rc);
      return state_ == State::Backoff;
    }
    backoffScanActive_ = true;
    enterState(initialState, stateTimeoutMs);
    scanStartedAt_ = stateEnteredAtMs_;
    portENTER_CRITICAL(&mux_);
    timing_.scanStartedMs = scanStartedAt_;
    timing_.recordedFlags = ScaleBleTimingScanStarted;
    portEXIT_CRITICAL(&mux_);
    ++scanStarts_;
    if (restart) {
      ++scanRestarts_;
    }
    if (debug_) {
      scaleLogDebug("active scan started (%u/%u)", scanInterval_, scanWindow_);
    }
    return true;
  }

  void service() {
    const uint32_t runtimeGeneration =
        shotStopperBleRuntimeSyncGeneration();
    if (syncGeneration_ != 0 && syncGeneration_ != runtimeGeneration) {
      const ShotStopperBleHealth health = shotStopperBleRuntimeHealth();
      finishLink(false, ScaleDisconnectReason::HOST_RESET,
                 health.lastResetReason);
      syncGeneration_ = runtimeGeneration;
    }
    if (!shotStopperBleRuntimeReady() && state_ != State::Idle) {
      finishLink(false, ScaleDisconnectReason::HOST_RESET, BLE_HS_ENOTSYNCED);
      return;
    }
    bool criticalOverflowed = false;
    bool controlOverflowed = false;
    bool rxOverflowed = false;
    bool mbufFailed = false;
    bool invalidNotificationStream = false;
    portENTER_CRITICAL(&mux_);
    criticalOverflowed = criticalOverflowed_;
    controlOverflowed = eventOverflowed_;
    rxOverflowed = rxOverflowed_;
    mbufFailed = mbufFailed_;
    invalidNotificationStream = invalidNotificationStream_;
    criticalOverflowed_ = false;
    eventOverflowed_ = false;
    rxOverflowed_ = false;
    mbufFailed_ = false;
    invalidNotificationStream_ = false;
    portEXIT_CRITICAL(&mux_);
    if (criticalOverflowed || controlOverflowed) {
      finishLink(true, ScaleDisconnectReason::EVENT_QUEUE_OVERFLOW,
                 BLE_HS_ENOMEM);
      return;
    }
    if (rxOverflowed) {
      finishLink(true, ScaleDisconnectReason::RX_QUEUE_OVERFLOW,
                 BLE_HS_ENOMEM);
      return;
    }
    if (mbufFailed) {
      finishLink(true, ScaleDisconnectReason::MBUF_ALLOCATION_FAILED,
                 BLE_HS_ENOMEM);
      return;
    }
    if (invalidNotificationStream) {
      finishLink(true, ScaleDisconnectReason::INVALID_PACKET_STREAM,
                 BLE_HS_EBADDATA);
      return;
    }

    Event event = {};
    while (popCriticalEvent(event)) {
      if (!eventMatches(event)) {
        noteStaleCallback();
        continue;
      }
      handleEvent(event);
    }
    if (popCandidate(event)) {
      if (eventMatches(event)) {
        handleEvent(event);
      } else {
        noteStaleCallback();
      }
    }
    while (popControlEvent(event)) {
      if (!eventMatches(event)) {
        noteStaleCallback();
        continue;
      }
      handleEvent(event);
    }

    if (isConnecting() && connectStartedAt_ != 0 &&
        elapsedMs(connectStartedAt_) >= SCALE_CONNECT_BUDGET_MS) {
      finishLink(true, ScaleDisconnectReason::OPERATION_TIMEOUT,
                 BLE_HS_ETIMEOUT);
      return;
    }
    if (state_ == State::Backoff && !backoff_.active(nowMs())) {
      backoff_.clearDeadline();
      if (backoffScanActive_) {
        enterState(State::Scanning);
      } else {
        enterState(State::Idle);
        (void)beginConfiguredScan(true);
      }
      return;
    }
    if (stateDeadlineArmed_ &&
        elapsedMs(stateEnteredAtMs_) >= stateTimeoutMs_) {
      if (state_ == State::Settling) {
        beginConnect();
        return;
      }
      finishLink(true, ScaleDisconnectReason::OPERATION_TIMEOUT,
                 BLE_HS_ETIMEOUT);
      return;
    }
  }

  bool eventMatches(const Event &event) const {
    if (event.generation != generation_ || event.operationId == 0) {
      return false;
    }
    switch (event.type) {
      case EventType::Candidate:
      case EventType::ScanComplete:
        return event.operationId == scanOperationId_;
      case EventType::ConnectComplete:
      case EventType::Disconnected:
        return event.operationId == linkOperationId_;
      case EventType::ServicesComplete:
      case EventType::CharacteristicsComplete:
      case EventType::DescriptorsComplete:
      case EventType::WriteComplete:
        return event.operationId == gattOperationId_;
    }
    return false;
  }

  void handleEvent(const Event &event) {
    switch (event.type) {
      case EventType::Candidate: {
        if (state_ != State::Scanning) {
          noteStaleCallback();
          return;
        }
        selectedAddress_ = event.address;
        strncpy(name_, event.name, sizeof(name_) - 1);
        name_[sizeof(name_) - 1] = '\0';
        formatAddress(event.address.val, address_, sizeof(address_));
        identityPresent_ = true;
        // This facade counter describes failed attempts in the current
        // connection sequence (the worker uses increases to emit warnings),
        // not attempts merely started.  Keep the lifetime attempt total in
        // connectAttemptsTotal_ for backend health telemetry.
        connectAttempts_ = 0;
        enterState(State::CancelPending, kScanCancelTimeoutMs);
        connectStartedAt_ = nowMs();
        ++scanCancels_;
        const int cancelResult = ble_gap_disc_cancel();
        // ble_gap_disc_cancel() is synchronous: a zero return means scanning
        // has been fully aborted and a connect procedure can start
        // immediately.  A manual cancellation does not produce a later
        // BLE_GAP_EVENT_DISC_COMPLETE callback.
        if (cancelResult == 0 || cancelResult == BLE_HS_EALREADY) {
          beginSettle();
        } else {
          finishLink(false, ScaleDisconnectReason::SCAN_START_FAILED,
                     cancelResult);
        }
        return;
      }

      case EventType::ScanComplete:
        if (state_ == State::CancelPending) {
          beginSettle();
        } else if (state_ == State::Scanning || state_ == State::Backoff) {
          finishLink(false, ScaleDisconnectReason::SCAN_START_FAILED,
                     event.status);
        } else {
          noteStaleCallback();
        }
        return;

      case EventType::ConnectComplete:
        if (state_ != State::Connecting) {
          noteStaleCallback();
          return;
        }
        if (event.status != 0) {
          if (connectAttempts_ != 0xff) {
            ++connectAttempts_;
          }
          ++connectionFailures_;
          finishLink(false, mapRawDisconnectReason(event.status),
                     event.status);
          return;
        }
        portENTER_CRITICAL(&mux_);
        connectionHandle_ = event.connectionHandle;
        portEXIT_CRITICAL(&mux_);
        beginServiceDiscovery();
        return;

      case EventType::Disconnected:
        if (event.connectionHandle != connectionHandle_) {
          noteStaleCallback();
          return;
        }
        finishLink(false, mapRawDisconnectReason(event.status), event.status);
        return;

      case EventType::ServicesComplete:
        if (state_ != State::DiscoveringServices) {
          noteStaleCallback();
          return;
        }
        if (event.status != BLE_HS_EDONE || serviceOverflowed_ ||
            serviceCount_ == 0) {
          ++discoveryFailures_;
          finishLink(true, ScaleDisconnectReason::DISCOVERY_FAILED,
                     serviceOverflowed_ ? BLE_HS_ENOMEM : event.status);
          return;
        }
        serviceIndex_ = 0;
        beginCharacteristicDiscovery();
        return;

      case EventType::CharacteristicsComplete:
        if (state_ != State::DiscoveringCharacteristics) {
          noteStaleCallback();
          return;
        }
        if (event.status != BLE_HS_EDONE) {
          ++discoveryFailures_;
          finishLink(true, ScaleDisconnectReason::DISCOVERY_FAILED,
                     event.status);
          return;
        }
        ++serviceIndex_;
        if (serviceIndex_ < serviceCount_) {
          beginCharacteristicDiscovery();
        } else {
          selectProtocolAndDiscoverCccd();
        }
        return;

      case EventType::DescriptorsComplete:
        if (state_ != State::DiscoveringDescriptors) {
          noteStaleCallback();
          return;
        }
        if (event.status != BLE_HS_EDONE || cccdHandle_ == 0) {
          ++subscriptionFailures_;
          finishLink(true, ScaleDisconnectReason::SUBSCRIBE_FAILED,
                     cccdHandle_ == 0 ? BLE_HS_ENOENT : event.status);
          return;
        }
        beginSubscription();
        return;

      case EventType::WriteComplete:
        if (state_ == State::Subscribing) {
          if (event.status != 0) {
            ++subscriptionFailures_;
            finishLink(true,
                       event.status == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::SUBSCRIBE_FAILED,
                       event.status);
            return;
          }
          initWriteIndex_ = 0;
          enterState(State::Initializing, BLE_OPERATION_TIMEOUT_MS);
          beginNextInitWrite();
        } else if (state_ == State::Initializing) {
          if (event.status != 0) {
            ++writeFailures_;
            finishLink(true,
                       event.status == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED,
                       event.status);
            return;
          }
          ++initWriteIndex_;
          beginNextInitWrite();
        }
        return;
    }
  }

  void beginSettle() {
    if (state_ == State::CancelPending) {
      enterState(State::Settling, SCALE_CONNECT_SETTLE_MS);
    }
  }

  void beginConnect() {
    enterState(State::Connecting,
               BLE_CONNECT_TIMEOUT_MS + kConnectCallbackMarginMs);
    portENTER_CRITICAL(&mux_);
    if (!timing_.has(ScaleBleTimingConnectIssued)) {
      timing_.connectIssuedMs = nowMs();
      timing_.recordedFlags |= ScaleBleTimingConnectIssued;
    }
    portEXIT_CRITICAL(&mux_);
    const uint32_t linkOperationId = beginOperation(CallbackDomain::Link);
    ++connectAttemptsTotal_;
    const int rc = ble_gap_connect(shotStopperBleRuntimeOwnAddressType(),
                                   &selectedAddress_, BLE_CONNECT_TIMEOUT_MS,
                                   nullptr, gapCallback,
                                   callbackArg(linkOperationId));
    if (rc != 0) {
      if (connectAttempts_ != 0xff) {
        ++connectAttempts_;
      }
      ++connectionFailures_;
      finishLink(false, ScaleDisconnectReason::CONNECT_FAILED, rc);
    }
  }

  void beginServiceDiscovery() {
    serviceCount_ = 0;
    serviceOverflowed_ = false;
    memset(services_, 0, sizeof(services_));
    memset(protocolHandles_, 0, sizeof(protocolHandles_));
    enterState(State::DiscoveringServices, BLE_DISCOVER_TIMEOUT_MS);
    const uint32_t gattOperationId = beginOperation(CallbackDomain::Gatt);
    const int rc = ble_gattc_disc_all_svcs(connectionHandle_, serviceCallback,
                                           callbackArg(gattOperationId));
    if (rc != 0) {
      ++discoveryFailures_;
      finishLink(true, ScaleDisconnectReason::DISCOVERY_FAILED, rc);
    }
  }

  void beginCharacteristicDiscovery() {
    enterState(State::DiscoveringCharacteristics, BLE_DISCOVER_TIMEOUT_MS);
    const ServiceRange &service = services_[serviceIndex_];
    const uint32_t gattOperationId = beginOperation(CallbackDomain::Gatt);
    const int rc = ble_gattc_disc_all_chrs(
        connectionHandle_, service.start, service.end, characteristicCallback,
        callbackArg(gattOperationId));
    if (rc != 0) {
      ++discoveryFailures_;
      finishLink(true, ScaleDisconnectReason::DISCOVERY_FAILED, rc);
    }
  }

  void selectProtocolAndDiscoverCccd() {
    protocol_ = nullptr;
    for (size_t index = 0; index < scaleProtocolCount(); ++index) {
      const ScaleProtocol *candidate = scaleProtocolAt(index);
      const ProtocolHandles &handles = protocolHandles_[index];
      const bool canSubscribe =
          (handles.readProperties &
           (BLE_GATT_CHR_PROP_NOTIFY | BLE_GATT_CHR_PROP_INDICATE)) != 0;
      const bool canWrite =
          (handles.writeProperties &
           (BLE_GATT_CHR_PROP_WRITE | BLE_GATT_CHR_PROP_WRITE_NO_RSP)) != 0;
      const bool nameAllowed =
          candidate != nullptr &&
          (!candidate->requireAdvertisedName ||
           scaleNameMatchesProtocol(name_, candidate));
      if (candidate != nullptr && handles.read != 0 && handles.write != 0 &&
          canSubscribe && canWrite && nameAllowed) {
        protocol_ = candidate;
        portENTER_CRITICAL(&mux_);
        readHandle_ = handles.read;
        portEXIT_CRITICAL(&mux_);
        readEndHandle_ = handles.readEnd;
        readProperties_ = handles.readProperties;
        writeHandle_ = handles.write;
        writeProperties_ = handles.writeProperties;
        break;
      }
    }
    if (protocol_ == nullptr) {
      cooldownSelected(kUnsupportedCooldownMs);
      finishLink(true, ScaleDisconnectReason::UNSUPPORTED_SCALE,
                 BLE_HS_ENOENT);
      return;
    }
    cccdHandle_ = 0;
    if (readEndHandle_ <= readHandle_) {
      ++subscriptionFailures_;
      finishLink(true, ScaleDisconnectReason::SUBSCRIBE_FAILED,
                 BLE_HS_EINVAL);
      return;
    }
    enterState(State::DiscoveringDescriptors, BLE_DISCOVER_TIMEOUT_MS);
    const uint32_t gattOperationId = beginOperation(CallbackDomain::Gatt);
    const int rc = ble_gattc_disc_all_dscs(
        connectionHandle_, readHandle_, readEndHandle_, descriptorCallback,
        callbackArg(gattOperationId));
    if (rc != 0) {
      ++subscriptionFailures_;
      finishLink(true, ScaleDisconnectReason::SUBSCRIBE_FAILED, rc);
    }
  }

  void beginSubscription() {
    const uint16_t value =
        (readProperties_ & BLE_GATT_CHR_PROP_NOTIFY) != 0 ? 1 : 2;
    const uint8_t cccd[2] = {static_cast<uint8_t>(value & 0xff),
                             static_cast<uint8_t>(value >> 8)};
    enterState(State::Subscribing, BLE_OPERATION_TIMEOUT_MS);
    if (!submitWrite(cccdHandle_, cccd, sizeof(cccd),
                     WritePurpose::Subscribe, true)) {
      ++subscriptionFailures_;
      finishLink(true, lastRawStatus_ == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::SUBSCRIBE_FAILED,
                 lastRawStatus_);
    }
  }

  void beginNextInitWrite() {
    if (protocol_ == nullptr || initWriteIndex_ >= protocol_->initWriteCount) {
      finishReady();
      return;
    }
    const ScalePayload &payload = protocol_->initWrites[initWriteIndex_];
    if (payload.data == nullptr || payload.length <= 0 ||
        payload.length > SCALE_MAX_COMMAND_LENGTH) {
      ++writeFailures_;
      finishLink(true, ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED,
                 BLE_HS_EINVAL);
      return;
    }
    const bool withResponse =
        (writeProperties_ & BLE_GATT_CHR_PROP_WRITE) != 0;
    // Each initialization transaction owns a fresh bounded deadline. This is
    // important for protocols with several writes: one slow/lost callback must
    // not inherit an already-expired deadline from the previous transaction.
    enterState(State::Initializing, BLE_OPERATION_TIMEOUT_MS);
    if (!submitWrite(writeHandle_, payload.data,
                     static_cast<uint16_t>(payload.length),
                     WritePurpose::Initialize, withResponse)) {
      ++writeFailures_;
      finishLink(true, lastRawStatus_ == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::INITIALIZATION_WRITE_FAILED,
                 lastRawStatus_);
      return;
    }
    if (!withResponse) {
      ++initWriteIndex_;
      beginNextInitWrite();
    }
  }

  bool submitWrite(uint16_t handle, const uint8_t *data, uint16_t length,
                   WritePurpose purpose, bool withResponse) {
    if (connectionHandle_ == kInvalidHandle || handle == 0 || data == nullptr ||
        length == 0 || length > SCALE_MAX_COMMAND_LENGTH) {
      return false;
    }
    if (!withResponse) {
      const int rc = ble_gattc_write_no_rsp_flat(connectionHandle_, handle,
                                                 data, length);
      lastRawStatus_ = rc;
      if (rc == BLE_HS_ENOMEM) {
        portENTER_CRITICAL(&mux_);
        ++mbufFailures_;
        portEXIT_CRITICAL(&mux_);
      }
      return rc == 0;
    }
    const uint32_t gattOperationId = beginOperation(CallbackDomain::Gatt);
    portENTER_CRITICAL(&mux_);
    writePurpose_ = purpose;
    writeCompleted_ = false;
    writeInterrupted_ = false;
    writeResult_ = BLE_HS_EUNKNOWN;
    writeWaiter_ = purpose == WritePurpose::Command
                       ? xTaskGetCurrentTaskHandle()
                       : nullptr;
    portEXIT_CRITICAL(&mux_);
    const int rc = ble_gattc_write_flat(connectionHandle_, handle, data,
                                        length, writeCallback,
                                        callbackArg(gattOperationId));
    if (rc != 0) {
      portENTER_CRITICAL(&mux_);
      writePurpose_ = WritePurpose::None;
      writeWaiter_ = nullptr;
      portEXIT_CRITICAL(&mux_);
      lastRawStatus_ = rc;
      if (rc == BLE_HS_ENOMEM) {
        portENTER_CRITICAL(&mux_);
        ++mbufFailures_;
        portEXIT_CRITICAL(&mux_);
      }
      return false;
    }
    return true;
  }

  ScaleCommandResult writeCommand(const uint8_t *data, uint16_t length) {
    const uint32_t commandGeneration = generation_;
    const bool withResponse =
        (writeProperties_ & BLE_GATT_CHR_PROP_WRITE) != 0;
    (void)xSemaphoreTake(writeSignal_, 0);
    const bool submitted = submitWrite(writeHandle_, data, length,
                                      WritePurpose::Command, withResponse);
    bool completed = false;
    bool interrupted = false;
    int result = submitted ? 0 : lastRawStatus_;
    if (submitted && withResponse) {
      const uint32_t startedAt = nowMs();
      for (;;) {
        portENTER_CRITICAL(&mux_);
        completed = writeCompleted_;
        interrupted = writeInterrupted_;
        result = writeResult_;
        portEXIT_CRITICAL(&mux_);
        const uint32_t elapsed = elapsedMs(startedAt);
        if (completed || interrupted || elapsed >= BLE_OPERATION_TIMEOUT_MS ||
            !shotStopperBleRuntimeReady() ||
            syncGeneration_ != shotStopperBleRuntimeSyncGeneration()) break;
        // Only the operation predicate completes a write. A delayed signal
        // from an older callback cannot shorten or extend its deadline.
        const uint32_t remaining = BLE_OPERATION_TIMEOUT_MS - elapsed;
        (void)xSemaphoreTake(writeSignal_, pdMS_TO_TICKS(remaining < 10 ? remaining : 10));
      }
      if (!completed && !interrupted) result = BLE_HS_ETIMEOUT;
    }
    portENTER_CRITICAL(&mux_);
    writePurpose_ = WritePurpose::None;
    writeWaiter_ = nullptr;
    gattOperationId_ = 0;
    portEXIT_CRITICAL(&mux_);

    // Drain authoritative GAP/reset evidence before classifying a command
    // failure. service() retains its original reason and performs one cleanup.
    service();
    const bool linkSurvived = generation_ == commandGeneration && isLinkUp();
    if (submitted && result == 0 && !interrupted && linkSurvived) {
      return ScaleCommandResult::Ok;
    }
    if (!linkSurvived) result = diagnostics_.disconnectStatus;
    ++writeFailures_;
    ++diagnostics_.commandFailureSequence;
    diagnostics_.commandFailureAtMs = nowMs();
    diagnostics_.commandElapsedMs = elapsedMs(commandStartedAt_);
    diagnostics_.commandStatus = result;
    diagnostics_.command = activeCommand_;
    lastRawStatus_ = result;

    // These submission errors mean this command was not accepted. ATT Error
    // Responses complete the procedure; invalid handles still require fresh
    // discovery. Unknown errors and unresolved timeouts remain fail-closed.
    const bool rejectedLocally = !submitted &&
        (result == BLE_HS_EBUSY || result == BLE_HS_EAGAIN || result == BLE_HS_ENOMEM);
    const bool rejectedByPeer = completed &&
        result > BLE_HS_ERR_ATT_BASE && result < BLE_HS_ERR_HCI_BASE &&
        result != BLE_HS_ATT_ERR(0x01) && result != BLE_HS_ATT_ERR(0x0a) &&
        result != BLE_HS_ATT_ERR(0x12);
    if (linkSurvived && !rejectedLocally && !rejectedByPeer) {
      finishLink(true, result == BLE_HS_ENOMEM
                           ? ScaleDisconnectReason::MBUF_ALLOCATION_FAILED
                           : ScaleDisconnectReason::COMMAND_WRITE_FAILED, result);
      diagnostics_.commandStatus = diagnostics_.disconnectStatus;
    }
    return ScaleCommandResult::WriteFailed;
  }

  void finishReady() {
    enterState(State::Ready);
    connectedAt_ = nowMs();
    portENTER_CRITICAL(&mux_);
    timing_.readyMs = connectedAt_;
    timing_.recordedFlags |= ScaleBleTimingReady;
    portEXIT_CRITICAL(&mux_);
    const uint16_t heartbeatPeriod =
        protocol_ != nullptr && protocol_->features.heartbeatPeriodMs != 0
            ? protocol_->features.heartbeatPeriodMs
            : HEARTBEAT_PERIOD_MS;
    lastHeartbeat_ = connectedAt_ - heartbeatPeriod;
    hasValidPacket_ = false;
    portENTER_CRITICAL(&mux_);
    invalidNotificationStreak_ = 0;
    portEXIT_CRITICAL(&mux_);
    hasTimer_ = false;
    currentTimerMs_ = 0;
    lastTimerPacket_ = 0;
    if (successfulConnections_ != 0) {
      ++reconnects_;
    }
    ++successfulConnections_;
    portENTER_CRITICAL(&mux_);
    if (timing_.has(ScaleBleTimingFirstCompatibleAdvertisement)) {
      lastAdvertisementToConnectMs_ =
          timing_.connectIssuedMs - timing_.firstCompatibleAdvertisementMs;
      lastAdvertisementToReadyMs_ =
          timing_.readyMs - timing_.firstCompatibleAdvertisementMs;
    }
    portEXIT_CRITICAL(&mux_);
    if (debug_) {
      scaleLogDebug("ready: %s @ %s", protocol_->id, address_);
    }
  }

  void cooldownSelected(uint32_t cooldownMs) {
    if (!identityPresent_) {
      return;
    }
    portENTER_CRITICAL(&advertMux_);
    negativeCache_.insert(peerKey(selectedAddress_), nowMs(), cooldownMs);
    portEXIT_CRITICAL(&advertMux_);
    ++negativeCacheInsertions_;
  }

  bool finishLink(bool terminatePeer, ScaleDisconnectReason reason,
                  int32_t rawStatus) {
    if (!lifecycleActive_) {
      if (state_ == State::Backoff &&
          (reason == ScaleDisconnectReason::USER_REQUEST ||
           reason == ScaleDisconnectReason::HOST_RESET)) {
        backoff_.reset();
        enterState(State::Idle);
      } else if (state_ != State::Idle && state_ != State::Backoff) {
        enterState(State::Idle);
      }
      if (reason != ScaleDisconnectReason::NONE) {
        lastReason_ = reason;
        lastRawStatus_ = rawStatus;
      }
      ++duplicateCleanups_;
      return false;
    }
    const State previous = state_;
    const uint16_t oldHandle = connectionHandle_;
    const uint32_t finishedGeneration = generation_;
    lifecycleActive_ = false;
    backoffScanActive_ = false;
    invalidateGeneration(reason, rawStatus, terminatePeer);
    enterState(State::Idle);
    ++cleanupCount_;
    if (reason != ScaleDisconnectReason::NONE) {
      lastReason_ = reason;
      lastRawStatus_ = rawStatus;
      ++diagnostics_.disconnectSequence;
      diagnostics_.disconnectAtMs = nowMs();
      diagnostics_.disconnectGeneration = finishedGeneration;
      diagnostics_.disconnectStatus = rawStatus;
      diagnostics_.teardownStatus = 0;
      diagnostics_.disconnectReason = static_cast<uint8_t>(reason);
      diagnostics_.disconnectCommand = activeCommand_;
      diagnostics_.disconnectCommandElapsedMs =
          activeCommand_ == 0xff ? 0 : elapsedMs(commandStartedAt_);
      const bool gapLoss = reason == ScaleDisconnectReason::REMOTE_DISCONNECTED ||
          reason == ScaleDisconnectReason::SUPERVISION_TIMEOUT ||
          reason == ScaleDisconnectReason::CONNECTION_FAILED_TO_ESTABLISH;
      diagnostics_.disconnectOrigin = reason == ScaleDisconnectReason::HOST_RESET
          ? ScaleBleDisconnectOrigin::HostReset
          : (gapLoss ? ScaleBleDisconnectOrigin::Gap : ScaleBleDisconnectOrigin::Local);
    }
    // Generation is invalidated before touching NimBLE. A cancellation can
    // synchronously or asynchronously surface a callback, but neither may
    // reacquire ownership after this point.
    int teardownStatus = 0;
    if (previous == State::Scanning || previous == State::CancelPending ||
        previous == State::Settling || previous == State::Backoff) {
      teardownStatus = ble_gap_disc_cancel();
    } else if (previous == State::Connecting) {
      teardownStatus = ble_gap_conn_cancel();
    }
    noteTeardownResult(teardownStatus);
    portENTER_CRITICAL(&mux_);
    connectionHandle_ = kInvalidHandle;
    readHandle_ = 0;
    portEXIT_CRITICAL(&mux_);
    if (terminatePeer && oldHandle != kInvalidHandle) {
      noteTeardownResult(
          ble_gap_terminate(oldHandle, BLE_ERR_REM_USER_CONN_TERM));
    }
    protocol_ = nullptr;
    writeHandle_ = 0;
    cccdHandle_ = 0;
    readEndHandle_ = 0;
    readProperties_ = 0;
    writeProperties_ = 0;
    scanStartedAt_ = 0;
    connectStartedAt_ = 0;
    connectedAt_ = 0;
    lastPacket_ = 0;
    packetPeriod_ = 0;
    lastHeartbeat_ = 0;
    hasValidPacket_ = false;
    consecutiveRejectedPackets_ = 0;
    hasTimer_ = false;
    currentTimerMs_ = 0;
    lastTimerPacket_ = 0;
    TaskHandle_t waiterToWake = nullptr;
    portENTER_CRITICAL(&mux_);
    invalidNotificationStreak_ = 0;
    criticalEvents_.clear();
    controlEvents_.clear();
    candidatePending_ = false;
    rxHead_ = rxTail_ = rxCount_ = 0;
    writePurpose_ = WritePurpose::None;
    waiterToWake = writeWaiter_;
    if (waiterToWake != nullptr) {
      writeResult_ = rawStatus;
      writeInterrupted_ = true;
    }
    writeWaiter_ = nullptr;
    eventOverflowed_ = false;
    criticalOverflowed_ = false;
    rxOverflowed_ = false;
    mbufFailed_ = false;
    invalidNotificationStream_ = false;
    portEXIT_CRITICAL(&mux_);
    if (waiterToWake != nullptr && waiterToWake != xTaskGetCurrentTaskHandle()) {
      xSemaphoreGive(writeSignal_);
    }
    const bool retryable = reason != ScaleDisconnectReason::NONE &&
                           reason != ScaleDisconnectReason::USER_REQUEST &&
                           reason != ScaleDisconnectReason::UNSUPPORTED_SCALE &&
                           reason != ScaleDisconnectReason::HOST_RESET;
    if (retryable) {
      const uint32_t entropy = static_cast<uint32_t>(selectedAddress_.val[0]) |
                               generation_ << 8;
      const uint32_t delayMs = backoff_.schedule(nowMs(), entropy);
      ++backoffCount_;
      if (reason == ScaleDisconnectReason::SCAN_START_FAILED) {
        backoffScanActive_ = false;
        enterState(State::Backoff, delayMs);
      } else {
        (void)beginConfiguredScan(false, State::Backoff, delayMs);
      }
    } else {
      backoff_.reset();
    }
    if (debug_ && reason != ScaleDisconnectReason::NONE) {
      scaleLogDebug("link finished: %s raw=%ld gen=%lu",
                    disconnectReasonName(reason), static_cast<long>(rawStatus),
                    static_cast<unsigned long>(finishedGeneration));
    }
    return true;
  }

  void noteTeardownResult(int status) {
    // A completed/no-longer-active GAP procedure is the expected race with a
    // remote disconnect. Any other error is retained for diagnostics while
    // the already-invalidated generation keeps the client fail-closed.
    if (status == 0 || status == BLE_HS_EALREADY) return;
    ++teardownFailures_;
    diagnostics_.teardownStatus = status;
  }

  void clearScanData() {
    portENTER_CRITICAL(&advertMux_);
    memset(candidates_, 0, sizeof(candidates_));
    candidateSequence_ = 0;
    portEXIT_CRITICAL(&advertMux_);
    identityPresent_ = false;
    portENTER_CRITICAL(&mux_);
    seenPending_ = false;
    seenAddress_[0] = '\0';
    seenName_[0] = '\0';
    candidateQueued_ = false;
    candidatePending_ = false;
    portEXIT_CRITICAL(&mux_);
  }

  uint32_t maxPacketPeriodMs() const {
    return protocol_ != nullptr && protocol_->features.maxPacketSilenceMs != 0
               ? protocol_->features.maxPacketSilenceMs
               : MAX_PACKET_PERIOD_MS;
  }

  bool supportedPacketLength(int length) const {
    return protocol_ != nullptr && protocol_->supportedPacketLength != nullptr &&
           protocol_->supportedPacketLength(length);
  }

  void rejectPacket() {
    bool rejectStream = false;
    portENTER_CRITICAL(&mux_);
    ++rejectedPackets_;
    ++consecutiveRejectedPackets_;
    rejectStream =
        consecutiveRejectedPackets_ >= MAX_CONSECUTIVE_REJECTED_PACKETS;
    portEXIT_CRITICAL(&mux_);
    if (rejectStream) {
      finishLink(true, ScaleDisconnectReason::INVALID_PACKET_STREAM,
                 BLE_HS_EBADDATA);
    }
  }

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  mutable portMUX_TYPE advertMux_ = portMUX_INITIALIZER_UNLOCKED;
  State state_ = State::Idle;
  bool debug_ = false;
  bool callbackOwner_ = false;
  bool lifecycleActive_ = false;
  uint32_t generation_ = 0;
  uint32_t operationId_ = 0;
  uint32_t scanOperationId_ = 0;
  uint32_t linkOperationId_ = 0;
  uint32_t gattOperationId_ = 0;
  uint32_t syncGeneration_ = 0;
  uint32_t stateEnteredAtMs_ = 0;
  uint32_t stateTimeoutMs_ = 0;
  bool stateDeadlineArmed_ = false;
  int32_t lastRawStatus_ = 0;
  ScaleDisconnectReason lastReason_ = ScaleDisconnectReason::NONE;
  ScaleBleDiagnostics diagnostics_ = {};
  uint32_t commandStartedAt_ = 0;
  uint8_t activeCommand_ = 0xff;

  NimbleFixedRing<Event, kCriticalEventCount> criticalEvents_;
  NimbleFixedRing<Event, kEventCount> controlEvents_;
  Event candidateMailbox_ = {};
  bool candidatePending_ = false;
  bool criticalOverflowed_ = false;
  bool eventOverflowed_ = false;
  uint32_t staleCallbacks_ = 0;
  uint32_t teardownFailures_ = 0;

  Candidate candidates_[kCandidateCount] = {};
  NimbleNegativeCache negativeCache_;
  NimbleBackoffPolicy backoff_;
  bool backoffScanActive_ = false;
  uint32_t candidateSequence_ = 0;
  uint32_t advertisementsSeen_ = 0;
  uint32_t compatibleAdvertisements_ = 0;
  uint32_t discardedAdvertisements_ = 0;
  uint32_t malformedAdvertisements_ = 0;
  uint32_t negativeCacheHits_ = 0;
  uint32_t negativeCacheInsertions_ = 0;
  uint32_t scanStarts_ = 0;
  uint32_t scanCancels_ = 0;
  uint32_t scanRestarts_ = 0;
  bool candidateQueued_ = false;
  ble_addr_t selectedAddress_ = {};
  uint8_t filterAddress_[6] = {};
  bool filterPresent_ = false;
  bool scanAddressFilter_ = false;
  uint16_t scanInterval_ = 0;
  uint16_t scanWindow_ = 0;
  uint32_t scanStartedAt_ = 0;
  bool seenPending_ = false;
  char seenAddress_[SCALE_MAC_CAPACITY] = {};
  char seenName_[SCALE_NAME_CAPACITY] = {};

  ServiceRange services_[kServiceCount] = {};
  size_t serviceCount_ = 0;
  size_t serviceIndex_ = 0;
  bool serviceOverflowed_ = false;
  ProtocolHandles protocolHandles_[kProtocolCapacity] = {};
  const ScaleProtocol *protocol_ = nullptr;
  uint16_t connectionHandle_ = kInvalidHandle;
  uint16_t readHandle_ = 0;
  uint16_t readEndHandle_ = 0;
  uint16_t writeHandle_ = 0;
  uint16_t cccdHandle_ = 0;
  uint8_t readProperties_ = 0;
  uint8_t writeProperties_ = 0;
  size_t initWriteIndex_ = 0;
  uint8_t connectAttempts_ = 0;
  uint32_t connectAttemptsTotal_ = 0;
  uint32_t connectionFailures_ = 0;
  uint32_t discoveryFailures_ = 0;
  uint32_t subscriptionFailures_ = 0;
  uint32_t writeFailures_ = 0;
  uint32_t connectStartedAt_ = 0;

  WritePurpose writePurpose_ = WritePurpose::None;
  StaticSemaphore_t writeSignalStorage_ = {};
  SemaphoreHandle_t writeSignal_ = nullptr;
  TaskHandle_t writeWaiter_ = nullptr;
  bool writeCompleted_ = false;
  bool writeInterrupted_ = false;
  bool pendingDisconnect_ = false;
  int32_t pendingDisconnectStatus_ = 0;
  int writeResult_ = 0;

  RxFrame rxFrames_[kRxFrameCount] = {};
  size_t rxHead_ = 0;
  size_t rxTail_ = 0;
  uint16_t rxCount_ = 0;
  uint16_t rxHighWater_ = 0;
  uint32_t rxDrops_ = 0;
  bool rxOverflowed_ = false;
  bool mbufFailed_ = false;
  bool invalidNotificationStream_ = false;
  uint8_t invalidNotificationStreak_ = 0;
  uint32_t mbufFailures_ = 0;

  bool identityPresent_ = false;
  char address_[SCALE_MAC_CAPACITY] = {};
  char name_[SCALE_NAME_CAPACITY] = {};
  ScaleBleTimingSnapshot timing_ = {};
  uint32_t connectedAt_ = 0;
  uint32_t lastHeartbeat_ = 0;
  uint32_t lastPacket_ = 0;
  uint32_t packetPeriod_ = 0;
  uint32_t lastTimerPacket_ = 0;
  uint32_t currentTimerMs_ = 0;
  float currentWeight_ = 0.0f;
  uint32_t rejectedPackets_ = 0;
  uint8_t consecutiveRejectedPackets_ = 0;
  uint32_t reconnects_ = 0;
  uint32_t successfulConnections_ = 0;
  uint32_t cleanupCount_ = 0;
  uint32_t duplicateCleanups_ = 0;
  uint32_t backoffCount_ = 0;
  uint32_t lastAdvertisementToConnectMs_ = 0;
  uint32_t lastAdvertisementToReadyMs_ = 0;
  uint32_t lastAdvertisementToFirstWeightMs_ = 0;
  uint32_t lastReadyToFirstWeightMs_ = 0;
  bool hasValidPacket_ = false;
  bool hasTimer_ = false;
};

NimbleScaleClient &clientFromStorage(void *storage) {
  return *reinterpret_cast<NimbleScaleClient *>(storage);
}

const NimbleScaleClient &clientFromStorage(const void *storage) {
  return *reinterpret_cast<const NimbleScaleClient *>(storage);
}

}  // namespace

EspressoScaleBLE::EspressoScaleBLE(bool debug) {
  static_assert(sizeof(NimbleScaleClient) <= NIMBLE_CLIENT_STORAGE_SIZE,
                "increase fixed NimBLE client storage");
  static_assert(alignof(NimbleScaleClient) <= 8,
                "NimBLE client storage alignment is insufficient");
  new (_nimbleClientStorage) NimbleScaleClient(debug);
}

EspressoScaleBLE::~EspressoScaleBLE() {
  clientFromStorage(_nimbleClientStorage).~NimbleScaleClient();
}

bool EspressoScaleBLE::init(const char *mac) {
  NimbleScaleClient &client = clientFromStorage(_nimbleClientStorage);
  client.disconnect();
  if (!client.startScan(mac, false, BLE_SCAN_NORMAL_INTERVAL,
                        BLE_SCAN_NORMAL_WINDOW, false)) {
    return false;
  }
  const uint32_t startedAt = nowMs();
  while (elapsedMs(startedAt) < SCALE_SCAN_TIMEOUT_MS ||
         client.isConnecting()) {
    if (client.poll()) {
      return true;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  client.disconnect();
  return false;
}

bool EspressoScaleBLE::startScan(const char *mac, bool forceRestart,
                                 uint16_t interval, uint16_t window,
                                 bool addressScan) {
  return clientFromStorage(_nimbleClientStorage)
      .startScan(mac, forceRestart, interval, window, addressScan);
}

bool EspressoScaleBLE::pollScan() {
  return clientFromStorage(_nimbleClientStorage).poll();
}

bool EspressoScaleBLE::isScanning() const {
  return clientFromStorage(_nimbleClientStorage).isScanning();
}

bool EspressoScaleBLE::isConnecting() const {
  return clientFromStorage(_nimbleClientStorage).isConnecting();
}

void EspressoScaleBLE::disconnect() {
  clientFromStorage(_nimbleClientStorage).disconnect();
}

ScaleCommandResult EspressoScaleBLE::tare() {
  return clientFromStorage(_nimbleClientStorage).writeOp(ScaleOp::Tare);
}

ScaleCommandResult EspressoScaleBLE::startTimer() {
  return clientFromStorage(_nimbleClientStorage).writeOp(ScaleOp::StartTimer);
}

ScaleCommandResult EspressoScaleBLE::stopTimer() {
  return clientFromStorage(_nimbleClientStorage).writeOp(ScaleOp::StopTimer);
}

ScaleCommandResult EspressoScaleBLE::resetTimer() {
  return clientFromStorage(_nimbleClientStorage).writeOp(ScaleOp::ResetTimer);
}

ScaleCommandResult EspressoScaleBLE::tareStartTimer() {
  if (!supportsTareStartTimer()) {
    return ScaleCommandResult::Unsupported;
  }
  return clientFromStorage(_nimbleClientStorage)
      .writeOp(ScaleOp::CombinedTareStart);
}

bool EspressoScaleBLE::supportsTareStartTimer() const {
  return features().has(ScaleFeatureCombinedTareStart);
}

ScaleCommandResult EspressoScaleBLE::beep() { return beepWithoutStateChange(); }

bool EspressoScaleBLE::supportsIndependentBeep() const {
  return features().has(ScaleFeatureIndependentBeep);
}

bool EspressoScaleBLE::supportsCommandFeedback() const {
  return features().has(ScaleFeatureCommandAudibleFeedback);
}

ScaleCommandResult EspressoScaleBLE::beepWithoutStateChange() {
  return setBeepLevel(1);
}

ScaleCommandResult EspressoScaleBLE::setBeepLevel(uint8_t level) {
  const ScaleFeatureSet available = features();
  if (!available.has(ScaleFeatureVolume) &&
      !available.has(ScaleFeatureIndependentBeep)) {
    return ScaleCommandResult::Unsupported;
  }
  if (level > available.volumeMax) {
    return ScaleCommandResult::InvalidArgument;
  }
  return clientFromStorage(_nimbleClientStorage)
      .writeOp(ScaleOp::SetVolume, level);
}

ScaleCommandResult EspressoScaleBLE::heartbeat() {
  NimbleScaleClient &client = clientFromStorage(_nimbleClientStorage);
  if (!client.features().has(ScaleFeatureHeartbeat)) {
    return ScaleCommandResult::Unsupported;
  }
  const ScaleCommandResult result = client.writeOp(ScaleOp::Heartbeat);
  if (scaleCommandOk(result)) {
    client.noteHeartbeat();
  }
  return result;
}

float EspressoScaleBLE::getWeight() const {
  return clientFromStorage(_nimbleClientStorage).weight();
}

bool EspressoScaleBLE::hasTimer() const {
  return clientFromStorage(_nimbleClientStorage).hasTimer();
}

uint32_t EspressoScaleBLE::getTimerMs() const {
  return clientFromStorage(_nimbleClientStorage).timerMs();
}

uint32_t EspressoScaleBLE::lastTimerAgeMs() const {
  return clientFromStorage(_nimbleClientStorage).timerAgeMs();
}

bool EspressoScaleBLE::heartbeatRequired() const {
  return clientFromStorage(_nimbleClientStorage).heartbeatRequired();
}

bool EspressoScaleBLE::isConnected() {
  return clientFromStorage(_nimbleClientStorage).isConnected();
}

bool EspressoScaleBLE::isLinkUp() const {
  return clientFromStorage(_nimbleClientStorage).isLinkUp();
}

bool EspressoScaleBLE::newWeightAvailable() {
  return clientFromStorage(_nimbleClientStorage).newWeightAvailable();
}

ScaleFeatureSet EspressoScaleBLE::features() const {
  return clientFromStorage(_nimbleClientStorage).features();
}

const char *EspressoScaleBLE::connectedProtocolName() const {
  return clientFromStorage(_nimbleClientStorage).protocolName();
}

const char *EspressoScaleBLE::address() const {
  return clientFromStorage(_nimbleClientStorage).address();
}

const char *EspressoScaleBLE::localName() const {
  return clientFromStorage(_nimbleClientStorage).name();
}

bool EspressoScaleBLE::isDirectedScan() const {
  return clientFromStorage(_nimbleClientStorage).directedScan();
}

bool EspressoScaleBLE::takeSeenAdvertisement(char *macOut, size_t macCapacity,
                                             char *nameOut,
                                             size_t nameCapacity) {
  return clientFromStorage(_nimbleClientStorage)
      .takeSeenAdvertisement(macOut, macCapacity, nameOut, nameCapacity);
}

ScaleDisconnectReason EspressoScaleBLE::lastDisconnectReason() const {
  return clientFromStorage(_nimbleClientStorage).lastReason();
}

const char *EspressoScaleBLE::lastDisconnectReasonName() const {
  return disconnectReasonName(lastDisconnectReason());
}

uint8_t EspressoScaleBLE::connectAttemptCount() const {
  return clientFromStorage(_nimbleClientStorage).connectAttempts();
}

uint8_t EspressoScaleBLE::connectStepId() const {
  return clientFromStorage(_nimbleClientStorage).stateId();
}

uint32_t EspressoScaleBLE::lastValidPacketAgeMs() const {
  return clientFromStorage(_nimbleClientStorage).lastPacketAgeMs();
}

uint32_t EspressoScaleBLE::rejectedPacketCount() const {
  return clientFromStorage(_nimbleClientStorage).rejectedPackets();
}

uint32_t EspressoScaleBLE::reconnectCount() const {
  return clientFromStorage(_nimbleClientStorage).reconnects();
}

ScaleBleTimingSnapshot EspressoScaleBLE::timingSnapshot() const {
  return clientFromStorage(_nimbleClientStorage).timing();
}

int32_t EspressoScaleBLE::lastBackendStatus() const {
  return clientFromStorage(_nimbleClientStorage).lastRawStatus();
}

ScaleBleDiagnostics EspressoScaleBLE::diagnostics() const {
  return clientFromStorage(_nimbleClientStorage).diagnostics();
}

ScaleBleBackendHealth EspressoScaleBLE::backendHealth() const {
  return clientFromStorage(_nimbleClientStorage).health();
}

int EspressoScaleBLE::linkRssi() {
  return clientFromStorage(_nimbleClientStorage).rssi();
}
