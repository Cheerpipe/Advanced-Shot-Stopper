#include <algorithm>
#include <cassert>
#include <cstring>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <thread>
#include "machine/ShotStopperMicraPublicIdentityCache.h"
#include "machine/ShotStopperMicraService.h"
#include "machine/ShotStopperMicraStomp.h"
#include "ShotStopperOutboundAdmission.h"
#include "ShotStopperJsonArena.h"
#include "ShotStopperDomain.h"

static std::atomic<uint64_t> now{1000};
uint32_t millis() { return static_cast<uint32_t>(now); }
int64_t esp_timer_get_time() { return static_cast<int64_t>(now * 1000); }
void xTaskNotifyGive(TaskHandle_t) {}
static bool callback = false, locked = false;
static unsigned stops = 0, destroys = 0;
static std::string transmitted;
static std::string disconnectLog;
void serialTraceCategoryf(shotstopper::LogLevel level, shotstopper::DebugCategory category,
                          const char *format, ...) {
  assert(!locked && level == shotstopper::LogLevel::WARNING &&
         category == shotstopper::DebugCategory::NETWORK);
  char line[160];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  disconnectLog = line;
}
static int sendLimit = -1;
static bool initFails = false, startFails = false;
static bool sessionFails = false, inhibitAfterSession = false, inhibitAfterInit = false;
static uint32_t stopDelayMs = 2, sessionDelayMs = 0, startDelayMs = 0;
static unsigned initCalls = 0, startCalls = 0;
constexpr int ESP_OK = 0;
constexpr int WEBSOCKET_EVENT_ANY = 0, WEBSOCKET_EVENT_CONNECTED = 1;
constexpr int WEBSOCKET_EVENT_DATA = 2, WEBSOCKET_EVENT_ERROR = 3, WEBSOCKET_EVENT_DISCONNECTED = 4;
constexpr int WEBSOCKET_EVENT_CLOSED = 5;
using esp_websocket_client_handle_t = void *;
struct esp_websocket_event_data_t {
  void *client = nullptr;
  struct {
    int esp_ws_handshake_status_code = 0, esp_tls_last_esp_err = 0;
    int esp_tls_stack_err = 0, esp_tls_cert_verify_flags = 0, esp_transport_sock_errno = 0;
  } error_handle;
  int data_len = 0, payload_len = 0, payload_offset = 0, op_code = 1;
  bool fin = true;
  const char *data_ptr = nullptr;
};
using Handler = void (*)(void *, const char *, int32_t, void *);
struct esp_websocket_client_config_t {
  const char *uri = nullptr, *headers = nullptr;
  int (*crt_bundle_attach)(void *) = nullptr;
  bool disable_auto_reconnect = false, task_core_id_set = false;
  int buffer_size = 0, task_prio = 0, task_stack = 0, task_core_id = -1;
  int network_timeout_ms = 0, ping_interval_sec = 0, pingpong_timeout_sec = 0;
};
int esp_crt_bundle_attach(void *) { return 0; }
int pdMS_TO_TICKS(int value) { return value; }
void taskYIELD() {}
void *esp_websocket_client_init(const esp_websocket_client_config_t *config) {
  assert(!locked && !callback && config->disable_auto_reconnect);
  assert(config->task_core_id == 0 && config->task_prio == 1 && config->buffer_size == 1024);
  assert(config->network_timeout_ms == 10000);
  ++initCalls;
  if (inhibitAfterInit) shotstopper::outboundBleQuiet.store(true);
  return initFails ? nullptr : reinterpret_cast<void *>(1);
}
int esp_websocket_register_events(void *, int, Handler, void *) { assert(!locked); return ESP_OK; }
int esp_websocket_unregister_events(void *, int, Handler) { assert(!locked && !callback); return ESP_OK; }
int esp_websocket_client_start(void *) {
  assert(!locked && !callback);
  ++startCalls;
  now += startDelayMs;
  return startFails ? -1 : ESP_OK;
}
int esp_websocket_client_stop(void *) { assert(!locked && !callback); ++stops; now += stopDelayMs; return ESP_OK; }
int esp_websocket_client_destroy(void *) { assert(!locked && !callback); ++destroys; return ESP_OK; }
int esp_websocket_client_send_text(void *, const char *bytes, int count, int) {
  assert(!locked);
  transmitted.assign(bytes, count);
  return sendLimit < 0 ? count : std::min(sendLimit, count);
}
#include "micra_websocket_work.inc"
namespace shotstopper {
void secureWipe(void *bytes, size_t count) { std::memset(bytes, 0, count); }
bool ShotStopperMicraService::networkEligible(LineaMicraError &error, bool) const {
  error = LineaMicraError::NONE;
  return true;
}
LineaMicraError ShotStopperMicraService::classifyFailure() const { return LineaMicraError::TRANSPORT; }
bool ShotStopperMicraService::ensureWorkBuffer() {
  if (!work_) work_ = new WorkBuffer;
  return true;
}
bool ShotStopperMicraService::ensureSession(LineaMicraPersistedSettings &, bool, bool *) {
  now += sessionDelayMs;
  if (inhibitAfterSession) outboundBleQuiet.store(true);
  std::strcpy(work_->accessToken, "synthetic-token");
  work_->accessTokenIssuedAtMs = now;
  return !sessionFails;
}
void ShotStopperMicraService::clearSession() { work_->accessToken[0] = '\0'; }
bool ShotStopperMicraService::applySignedHeaders(const LineaMicraPersistedSettings &, char *headers, size_t) {
  std::strcpy(headers, "synthetic-signed-upgrade\r\n");
  return true;
}
#include "machine/ShotStopperMicraWebSocket.inc"
#include "micra_websocket_status.inc"
struct MicraWebSocketTest {
  static void setupAdmissionAndBackoff() {
    for (unsigned gate = 0; gate < 4; ++gate) {
      now = 1000;
      ShotStopperMicraService service;
      service.config_.accountConfigured = true;
      std::strcpy(service.config_.selectedSerial, "synthetic");
      service.staConnected_.store(true);
      auto &inhibited = gate == 0 ? outboundAcquisitionHeld : gate == 1 ? outboundScaleSetup
          : gate == 2 ? outboundBleQuiet : service.scaleConnecting_;
      inhibited.store(true);
      service.serviceWebSocket();
      assert(!service.websocket_ && !service.work_);
      assert(service.websocketStatus().state == MicraSocketState::PAUSED);
      inhibited.store(false);
      service.serviceWebSocket();
      subscribe(service);
      inhibited.store(true);
      service.serviceWebSocket();
      assert(service.websocket_->client && service.websocketStatus().subscribed);
      service.websocket_->failed.store(true);
      service.serviceWebSocket();
      now = service.websocketStatus().retryAtMs;
      service.serviceWebSocket();
      assert(!service.websocket_->client && service.websocket_->attempts == 1);
      inhibited.store(false);
      service.serviceWebSocket();
      assert(service.websocket_->client);
      service.stopWebSocket(true);
      delete service.work_;
    }
    for (unsigned phase = 0; phase < 2; ++phase) {
      ShotStopperMicraService service;
      service.config_.accountConfigured = true;
      service.staConnected_.store(true);
      inhibitAfterSession = phase == 0;
      inhibitAfterInit = phase == 1;
      const auto inits = initCalls, starts = startCalls;
      service.serviceWebSocket();
      assert(initCalls == inits + phase && startCalls == starts);
      assert(service.websocket_ && !service.websocket_->client);
      inhibitAfterSession = inhibitAfterInit = false;
      outboundBleQuiet.store(false);
      service.stopWebSocket(true);
      delete service.work_;
    }
    for (unsigned phase = 0; phase < 3; ++phase) {
      now = UINT32_MAX - 2000ULL;  // Cleanup and backoff cross millis rollover.
      ShotStopperMicraService service;
      service.config_.accountConfigured = true;
      service.staConnected_.store(true);
      if (phase == 0) {
        service.serviceWebSocket();
        service.websocket_->failed.store(true);
        stopDelayMs = 10000;
      } else if (phase == 1) {
        sessionFails = true;
        sessionDelayMs = 10000;
      } else {
        startFails = true;
        startDelayMs = 10000;
      }
      service.serviceWebSocket();
      assert(service.websocket_ && !service.websocket_->client);
      assert(service.websocketStatus().retryAtMs == millis() + 3000);
      stopDelayMs = 2;
      sessionDelayMs = startDelayMs = 0;
      sessionFails = startFails = false;
      now += 2999;
      service.serviceWebSocket();
      assert(!service.websocket_->client);
      ++now;
      service.serviceWebSocket();
      assert(service.websocket_->client);
      service.stopWebSocket(true);
      delete service.work_;
    }
  }
  static void pongPublicationRace() {
    now = 1000;
    ShotStopperMicraService service;
    service.config_.accountConfigured = true;
    std::strcpy(service.config_.selectedSerial, "synthetic");
    service.staConnected_.store(true);
    service.serviceWebSocket();
    subscribe(service);
    static ShotStopperMicraService *subject;
    static unsigned acquisitions;
    subject = &service;
    acquisitions = 0;
    const auto before = stops;
    TaskMutex::hostObserver = [](const TaskMutex *, bool acquired) {
      if (acquired && ++acquisitions == 3) {
        // Callback publishes after the worker sampled time, before its status copy.
        subject->websocketStatus_.pongAtMs = ++now;
        TaskMutex::hostObserver = nullptr;
      }
    };
    service.serviceWebSocket();
    TaskMutex::hostObserver = nullptr;
    assert(acquisitions == 3 && stops == before && service.websocketStatus().subscribed);
    service.stopWebSocket(true);
    delete service.work_;
  }
  static void connectionClassification() {
    now = 1000;
    ShotStopperMicraService service;
    service.config_.accountConfigured = true;
    std::strcpy(service.config_.selectedSerial, "synthetic");
    service.staConnected_.store(true);
    service.serviceWebSocket();
    assert(service.websocketStatus().plannedConnections == 0);
    subscribe(service);
    uint32_t planned = 1, unexpected = 0;
    // Every intentional pause, renewal and obsolete observation stays planned.
    for (int cause = 0; cause < 11; ++cause) {
      switch (cause) {
        case 0: service.shotActive_.store(true); outboundScaleConnected.store(true); break;
        case 1: publishOutboundAcquisition(true, ++now); break;
        case 2: outboundBleQuiet.store(true); break;
        case 3: outboundMaintenance.store(true); break;
        case 4: outboundOtaBusy.store(true); break;
        case 5: now += micra_timing::kAccessTokenRefreshAgeMs; break;
        case 6:
          deliver(service, "MESS", false);
          now += 10000;
          ++service.observationFence_.epoch;
          break;
        case 7: service.apActive_.store(true); break;
        case 8: service.config_.options &= ~LINEA_MICRA_OBSERVE_STATE; break;
        case 9: service.config_.connectionType = static_cast<uint8_t>(MicraConnectionType::API); break;
        case 10: service.clearSessionRequested_.store(true); break;
      }
      service.serviceWebSocket();
      service.shotActive_.store(false);
      outboundScaleConnected.store(false);
      publishOutboundAcquisition(false, now);
      outboundBleQuiet.store(false);
      outboundMaintenance.store(false);
      outboundOtaBusy.store(false);
      service.apActive_.store(false);
      service.config_.options |= LINEA_MICRA_OBSERVE_STATE;
      service.config_.connectionType = static_cast<uint8_t>(MicraConnectionType::WEBSOCKET);
      service.clearSessionRequested_.store(false);
      service.serviceWebSocket();
      if (cause != 1 && cause != 2) { subscribe(service); ++planned; }
      assert(service.websocketStatus().plannedConnections == planned);
      assert(service.websocketStatus().unexpectedConnections == unexpected);
    }
    // Failures remain unexpected through a maintenance release or radio pause.
    for (int cause = 0; cause < 8; ++cause) {
      switch (cause) {
        case 0: {
          esp_websocket_event_data_t event;
          service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_CLOSED, &event);
          break;
        }
        case 1: deliver(service, std::string("ERROR\n\n") + '\0'); break;
        case 2: service.websocketStatus_.subscribed = false; now += 10000; break;
        case 3: deliver(service, "MESS", false); now += 10000; break;
        case 4: service.staConnected_.store(false); break;
        case 5: case 6:
          service.stopWebSocket();
          initFails = cause == 5;
          startFails = cause == 6;
          break;
        case 7: {
          esp_websocket_event_data_t event;
          event.error_handle.esp_ws_handshake_status_code = 401;
          service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_ERROR, &event);
          break;
        }
      }
      service.serviceWebSocket();
      assert(service.websocketStatus().plannedConnections == planned);
      assert(service.websocketStatus().unexpectedConnections == unexpected);
      outboundMaintenance.store(true);
      service.serviceWebSocket();
      assert(!service.websocket_);
      outboundMaintenance.store(false);
      initFails = startFails = false;
      service.staConnected_.store(true);
      now = std::max(now.load(), uint64_t(service.websocketStatus().retryAtMs));
      service.serviceWebSocket();
      subscribe(service);
      assert(service.websocketStatus().plannedConnections == planned);
      assert(service.websocketStatus().unexpectedConnections == ++unexpected);
    }
    service.stopWebSocket(true);
    delete service.work_;
    service.work_ = nullptr;
  }
  static void lifecycleRegressions() {
    TaskMutex::hostObserver = [](const TaskMutex *, bool acquired) { locked = acquired; };
    now = 1000;
    ShotStopperMicraService service;
    service.config_.accountConfigured = true;
    std::strcpy(service.config_.selectedSerial, "synthetic");
    service.staConnected_.store(true);
    service.serviceWebSocket();
    subscribe(service);
    const char *off = R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"StandBy"}}]})";
    deliver(service, observation(service, off));
    esp_websocket_event_data_t event;
    event.client = service.websocket_->client;
    callback = true;
    service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_CLOSED, &event);
    callback = false;
    service.serviceWebSocket();
    assert(!service.websocket_->client && service.websocketStatus().state == MicraSocketState::BACKOFF);
    now += 3000;
    service.serviceWebSocket();
    subscribe(service);
    deliver(service, observation(service, off));
    assert(service.powerState_.notePhysicalStart(service.published_, true, true, now));
    service.stopWebSocket();  // Also used by the HTTP progress cancellation path.
    assert(service.powerState_.retained());
    now += 100000;
    assert(service.powerState_.effectiveStatus(service.published_, true, millis()).effectiveOn);
    service.serviceWebSocket();
    subscribe(service);
    for (unsigned attempt = 0; attempt < 2; ++attempt) {
      event.client = service.websocket_->client;
      event.error_handle.esp_ws_handshake_status_code = 401;
      callback = true;
      service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_ERROR, &event);
      if (attempt == 0) {
        event.error_handle.esp_tls_last_esp_err = 32774;
        event.error_handle.esp_tls_stack_err = -29312;
        event.error_handle.esp_tls_cert_verify_flags = 8;
        event.error_handle.esp_transport_sock_errno = 110;
        event.data_ptr = "synthetic-token-and-signed-header";
        service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_DISCONNECTED, &event);
        assert(disconnectLog == "Micra WS disconnected http=401 tls=32774 stack=-29312 verify=8 errno=110");
      }
      callback = false;
      service.serviceWebSocket();
      if (attempt == 0) {
        now = service.websocketStatus().retryAtMs;
        outboundMaintenance.store(true);
        service.serviceWebSocket();
        assert(!service.websocket_);
        outboundMaintenance.store(false);
        service.serviceWebSocket();
      }
    }
    assert(service.websocketStatus().state == MicraSocketState::AUTH_ERROR);
    publishOutboundAcquisition(true, now);
    service.serviceWebSocket();
    publishOutboundAcquisition(false, now);
    now += 60000;
    service.serviceWebSocket();
    assert(service.websocketStatus().state == MicraSocketState::AUTH_ERROR && !service.websocket_->client);
    outboundMaintenance.store(true);
    service.serviceWebSocket();
    assert(!service.websocket_);
    outboundMaintenance.store(false);
    service.serviceWebSocket();
    assert(service.websocketStatus().state == MicraSocketState::AUTH_ERROR && !service.websocket_);
    service.websocketRetryRequested_.store(true);
    service.serviceWebSocket();
    assert(service.websocket_ && service.websocket_->client);
    service.stopWebSocket(true);
    delete service.work_;
    service.work_ = nullptr;
    TaskMutex::hostObserver = nullptr;
  }
  static void deliver(ShotStopperMicraService &service, const std::string &bytes,
                      bool fin = true, int opcode = 1) {
    esp_websocket_event_data_t message;
    message.client = service.websocket_->client;
    message.data_ptr = bytes.data();
    message.data_len = message.payload_len = static_cast<int>(bytes.size());
    message.fin = fin;
    message.op_code = opcode;
    callback = true;
    service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_DATA, &message);
    callback = false;
  }
  static std::string observation(ShotStopperMicraService &service, const char *body) {
    return std::string("MESSAGE\ndestination:") + service.websocket_->destination +
        "\nsubscription:" + service.websocket_->subscription + "\n\n" + body + std::string(1, '\0');
  }
  static void subscribe(ShotStopperMicraService &service) {
    esp_websocket_event_data_t message;
    message.client = service.websocket_->client;
    callback = true;
    service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_CONNECTED, &message);
    callback = false;
    assert(transmitted.find("Authorization:Bearer synthetic-token") != std::string::npos);
    assert(service.websocket_->connect[0] == '\0');
    deliver(service, std::string("CONNECTED\nversion:1.2\n\n") + std::string(1, '\0'));
    assert(service.websocketStatus().subscribed);
    assert(service.snapshotPending_);
    assert(service.snapshotStamp_.epoch == service.observationFence_.epoch);
    assert(service.snapshotStamp_.connectionRevision == service.observationFence_.connectionRevision);
    assert(transmitted.find(service.websocket_->destination) != std::string::npos);
  }
  static void run() {
    TaskMutex::hostObserver = [](const TaskMutex *, bool acquired) { locked = acquired; };
    ShotStopperMicraService service;
    service.config_.accountConfigured = true;
    std::strcpy(service.config_.selectedSerial, "synthetic");
    service.staConnected_.store(true);
    service.serviceWebSocket();
    assert(service.websocket_ && service.websocket_->client);
    service.websocketStatus_.transmitBytes = UINT32_MAX;
    subscribe(service);
    assert(service.websocketStatus().transmitBytes > UINT32_MAX);
    const char *off = R"({"connected":true,"widgets":[{"code":"CMMachineStatus","output":{"mode":"StandBy"}},{"code":"CMBackFlush","output":{"status":"Requested"}}]})";
    deliver(service, observation(service, off));
    assert(service.published_.powerState == LineaMicraPowerState::OFF);
    assert(service.websocketStatus().cleaning == MicraCleaningState::REQUESTED);
    // A relay-only wake keeps the socket and accepts confirming push evidence.
    const auto connectionsBeforeWake = service.websocketStatus().reconnects;
    assert(service.powerState_.notePhysicalStart(service.published_, true, true, ++now));
    service.serviceWebSocket();
    assert(stops == 0 && destroys == 0);
    assert(service.websocketStatus().reconnects == connectionsBeforeWake);
    deliver(service, observation(service, R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"BrewingMode"}}]})"));
    assert(!service.powerState_.effectiveStatus(service.published_, true, now).optimisticOn);
    // Callback publication does not depend on the HTTP owner's progress.
    service.active_ = true;
    const char *on = R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"BrewingMode"}}]})";
    // The SDK's 64-bit monotonic clock keeps bins intact across millis rollover.
    const auto onFrame = observation(service, on), offFrame = observation(service, off);
    service.websocketStatus_.receiveBytes = UINT32_MAX;
    now = UINT32_MAX - 400ULL;
    deliver(service, onFrame);
    now += 800;
    deliver(service, offFrame);
    auto rates = service.websocketStatus();
    assert(rates.receiveBytes > UINT32_MAX);
    assert(rates.receivePerSecond == offFrame.size());
    assert(rates.receivePerMinute == onFrame.size() + offFrame.size());
    now += 61000;
    rates = service.websocketStatus();
    assert(rates.receivePerSecond == 0 && rates.receivePerMinute == 0);
    std::thread receiver([&] { deliver(service, observation(service, on)); });
    receiver.join();
    assert(service.published_.powerState == LineaMicraPowerState::ON);
    service.active_ = false;
    const auto fresh = service.published_.sampleAtMs;
    deliver(service, observation(service, R"({"connected":false,"widgets":[{"code":"CMMachineStatus","output":{"mode":"StandBy"}}]})"));
    assert(service.published_.sampleAtMs == fresh && service.powerState_.retained());
    // Discovery retains WebSocket observation and accepts fresh frames.
    publishOutboundAcquisition(true, ++now);
    deliver(service, observation(service, off));
    assert(service.published_.sampleAtMs == millis());
    service.websocketStatus_.pongAtMs = millis();
    service.serviceWebSocket();
    assert(stops == 0 && destroys == 0 && service.websocket_->client);
    assert(service.websocketStatus().state == MicraSocketState::STREAMING);
    publishOutboundAcquisition(false, now);
    service.serviceWebSocket();
    deliver(service, observation(service, off));
    assert(!service.powerState_.retained());
    // Unscaled paddle shots retain both the subscription and fresh push evidence.
    const auto stopsBeforeShot = stops;
    const auto epoch = service.observationFence_.epoch;
    outboundShotActive.store(true);
    service.shotActive_.store(true);
    service.inhibitCloud();
    service.serviceWebSocket();
    deliver(service, observation(service, on));
    assert(stops == stopsBeforeShot && service.websocketStatus().subscribed);
    assert(service.observationFence_.epoch == epoch);
    assert(service.status().quality == LineaMicraObservationQuality::CURRENT);
    assert(!service.status().shotPaused && !service.abortRequested_.load());
    // A live scale pauses the shot; losing it releases the pause mid-shot.
    outboundScaleConnected.store(true);
    service.serviceWebSocket();
    assert(stops == stopsBeforeShot + 1 && !service.websocket_->client);
    outboundScaleConnected.store(false);
    service.serviceWebSocket();
    assert(service.websocket_->client && service.websocket_->connect[0]);
    subscribe(service);
    deliver(service, observation(service, off));
    assert(service.status().quality == LineaMicraObservationQuality::CURRENT);
    service.shotActive_.store(false);
    outboundShotActive.store(false);
    // A whole shot can elapse while the owner is busy: old ingress stays fenced.
    deliver(service, observation(service, on));
    const auto beforeShortShot = service.published_.sampleAtMs;
    outboundScaleConnected.store(true);
    outboundShotActive.store(true);
    service.inhibitCloud();
    outboundShotActive.store(false);
    deliver(service, observation(service, off));
    assert(service.published_.sampleAtMs == beforeShortShot);
    assert(service.published_.powerState == LineaMicraPowerState::ON);
    service.serviceWebSocket();
    assert(service.websocket_->client && service.websocket_->connect[0]);
    subscribe(service);
    outboundScaleConnected.store(false);
    assert(service.powerState_.retained());
    deliver(service, observation(service, off));
    assert(!service.powerState_.retained());
    // A fragmented message started before an intent cannot confirm that intent.
    const auto bytes = observation(service, off);
    deliver(service, bytes.substr(0, 15), false);
    assert(service.powerState_.notePhysicalStart(service.published_, true, true, ++now));
    deliver(service, bytes.substr(15), true, 0);
    assert(service.powerState_.effectiveStatus(service.published_, true, now).optimisticOn);
    // Routing and malformed complete documents fail without partial publication.
    const auto before = service.published_.sampleAtMs;
    auto wrong = observation(service, on);
    wrong.replace(wrong.find("/ws/sn/"), 7, "/other/");
    deliver(service, wrong);
    assert(service.websocket_->failed.load() && service.published_.sampleAtMs == before);
    service.stopWebSocket(true);
    assert(!service.websocket_ && service.websocketStatus().retainedBytes == 0);
    service.websocketStatus_.retryAtMs = 0;
    service.serviceWebSocket();
    const auto transmittedBefore = service.websocketStatus().transmitBytes;
    sendLimit = 5;
    esp_websocket_event_data_t partial;
    partial.client = service.websocket_->client;
    callback = true;
    service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_CONNECTED, &partial);
    callback = false;
    assert(service.websocketStatus().transmitBytes == transmittedBefore + 5);
    assert(service.websocket_->failed.load() && service.websocket_->connect[0] == '\0');
    sendLimit = -1;
    outboundMaintenance.store(true);
    service.serviceWebSocket();
    assert(!service.websocket_ && service.websocketStatus().state == MicraSocketState::PAUSED);
    assert(std::strcmp(service.websocketStatus().reason, "maintenance") == 0);
    outboundMaintenance.store(false);
    detail::g_hostAllocationsUntilFailure.store(0);
    service.serviceWebSocket();
    assert(!service.websocket_ && service.websocketStatus().allocationFailures == 1);
    detail::g_hostAllocationsUntilFailure.store(-1);
    delete service.work_;
    service.work_ = nullptr;
    TaskMutex::hostObserver = nullptr;
  }
  static void initialSynchronization() {
    now = 1000;
    ShotStopperMicraService service;
    service.config_.accountConfigured = true;
    std::strcpy(service.config_.selectedSerial, "synthetic");
    service.published_.connectionFreshness = true;
    service.staConnected_.store(true);
    service.serviceWebSocket();
    subscribe(service);
    const auto firstStamp = service.snapshotStamp_;
    MicraObservation initial;
    initial.stamp = firstStamp;
    initial.source = MicraObservationSource::HTTP_INITIAL;
    initial.powerPresent = true;
    initial.mode = LineaMicraObservedMode::BREWING;
    initial.receivedAtMs = now;
    assert(service.observationFence_.merge(service.published_, service.powerState_, initial, true));
    now += 600000;
    assert(service.status().quality == LineaMicraObservationQuality::CURRENT);
    assert(service.status().powerSource == MicraObservationSource::HTTP_INITIAL);
    outboundAcquisitionHeld.store(true);
    assert(service.status().quality == LineaMicraObservationQuality::CURRENT);
    outboundAcquisitionHeld.store(false);
    deliver(service, observation(service, R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"StandBy"}}]})"));
    assert(service.published_.powerSource == MicraObservationSource::WEBSOCKET);
    assert(service.snapshotStamp_.powerRevision == firstStamp.powerRevision);
    service.snapshotPending_ = false;  // Owner has dispatched initialization.
    deliver(service, observation(service, R"({"connected":false})"));
    assert(service.observationFence_.offline && service.powerState_.retained());
    deliver(service, observation(service, R"({"connected":true})"));
    assert(service.snapshotPending_ && !service.observationFence_.synchronized);
    assert(service.snapshotStamp_.connectionRevision != firstStamp.connectionRevision);
    deliver(service, observation(service, R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"BrewingMode"}}]})"));
    assert(service.observationFence_.synchronized);
    ++outboundAcquisitionGeneration;
    assert(service.status().quality == LineaMicraObservationQuality::CURRENT);
    esp_websocket_event_data_t event;
    service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_CLOSED, &event);
    assert(!service.observationFence_.synchronized && !service.snapshotPending_);
    assert(service.powerState_.retained());  // Invalidated before owner teardown.
    assert(service.status().quality == LineaMicraObservationQuality::STALE);
    service.stopWebSocket();
    assert(!service.snapshotPending_);
    service.serviceWebSocket();
    subscribe(service);
    assert(service.snapshotStamp_.connectionRevision != firstStamp.connectionRevision);
    assert(service.status().quality == LineaMicraObservationQuality::STALE);
    service.stopWebSocket(true);
    delete service.work_;
  }
  static void backflushContract() {
    const char *inactive = R"({"connected":true,"widgets":[{"code":"CMMachineStatus","output":{"mode":"BrewingMode"}},{"code":"CMBackFlush","output":{"status":"Off"}}]})";
    const char *awaiting = R"({"connected":true,"widgets":[{"code":"CMMachineStatus","output":{"mode":"BrewingMode"}},{"code":"CMBackFlush","output":{"status":"Requested"}}]})";
    const char *active = R"({"widgets":[{"code":"CMBackFlush","output":{"status":"Cleaning"}}]})";
    for (bool scalePresent : {false, true}) for (int scenario = 0; scenario < 5; ++scenario) {
      outboundScaleConnected.store(scalePresent);
      now = 1000;
      ShotStopperMicraService service;
      service.config_.accountConfigured = true;
      std::strcpy(service.config_.selectedSerial, "synthetic");
      service.staConnected_.store(true);
      service.tokenAvailable_.store(true);
      service.tokenIssuedAtMs_.store(millis());
      service.serviceWebSocket();
      subscribe(service);
      if (scenario != 1) deliver(service, observation(service, inactive));
      deliver(service, observation(service, awaiting));
      const auto fragmented = observation(service, active);
      if (scenario == 2) deliver(service, fragmented.substr(0, 15), false);
      const auto before = service.backflush(true);
      assert(before.valid && before.ready && before.phase == MachineBackflushPhase::AWAITING);
      service.powerActive_.store(true);
      assert(!service.backflush().ready);
      service.powerActive_.store(false);
      service.tokenIssuedAtMs_.store(now - micra_timing::kAccessTokenRefreshAgeMs + 210000);
      assert(!service.backflush().ready);
      service.tokenIssuedAtMs_.store(now);
      MachineBackflushPermit permit;
      service.active_ = true;  // A parallel API read must survive the gesture.
      const auto stopsBeforeGesture = stops;
      assert(service.physicalStart(&permit) == MachinePhysicalStartDisposition::BACKFLUSH_CANDIDATE);
      service.serviceWebSocket();
      assert(stops == stopsBeforeGesture && service.websocketStatus().subscribed);
      assert(!service.abortRequested_.load() && service.websocketAdmitted());
      service.active_ = false;
      assert(permit.attempt && permit.extendable == (scenario != 1));
      if (scenario == 2) deliver(service, fragmented.substr(15), true, 0);
      else deliver(service, fragmented);
      auto batch = service.backflush(true);
      assert(batch.valid && batch.count == 1);
      assert((batch.changes[0].permit.attempt == permit.attempt) == (scenario != 2));
      if (scenario == 3) {
        deliver(service, observation(service, R"({"connected":false,"widgets":[{"code":"CMBackFlush","output":{"status":"Cleaning"}}]})"));
        deliver(service, observation(service, R"({"connected":true,"widgets":[]})"));
        batch = service.backflush(true);
        assert(!batch.valid && batch.permit.continuity != permit.continuity);
      } else if (scenario == 4) {
        now += 30000;
        deliver(service, observation(service, R"({"widgets":[]})"));
        assert(!service.backflush().valid);  // Traffic is not a validated pong.
        esp_websocket_event_data_t pong;
        pong.op_code = 0xA;
        service.websocketEvent(&service, nullptr, WEBSOCKET_EVENT_DATA, &pong);
        assert(!service.backflush().valid);  // No retained-data reauthorization.
      } else {
        deliver(service, observation(service, inactive));
        assert(service.backflush(true).phase == MachineBackflushPhase::INACTIVE);
      }
      service.finishBackflush(permit.attempt);
      service.stopWebSocket(true);
      delete service.work_;
    }
    outboundScaleConnected.store(false);
  }
  static void concurrentStatusLifecycle() {
    now = 1000;
    ShotStopperMicraService service;
    service.config_.accountConfigured = true;
    service.published_.connectionFreshness = true;
    std::strcpy(service.config_.selectedSerial, "synthetic");
    service.staConnected_.store(true);
    std::atomic<bool> done{false};
    std::atomic<unsigned> samples{0};
    std::thread reader([&] {
      while (!done.load()) {
        (void)service.backflush_.ingressAttempt();
        const auto state = service.status();
        assert(state.powerState != LineaMicraPowerState::OFF);
        (void)service.websocketStatus();
        ++samples;
      }
    });
    while (!samples.load()) std::this_thread::yield();
    for (unsigned cycle = 0; cycle < 100; ++cycle) {
      {
        TaskLockGuard lock(service.mux_);
        const auto permit = service.backflush_.start();
        service.backflush_.finish(permit.attempt);
      }
      service.serviceWebSocket();
      subscribe(service);
      deliver(service, observation(service, R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"BrewingMode"}}]})"));
      service.stopWebSocket(true);
    }
    done.store(true);
    reader.join();
    delete service.work_;
  }
};
}
int main() {
  shotstopper::initJsonParser();
  shotstopper::MicraWebSocketTest::run();
  shotstopper::MicraWebSocketTest::lifecycleRegressions();
  shotstopper::MicraWebSocketTest::connectionClassification();
  shotstopper::MicraWebSocketTest::initialSynchronization();
  shotstopper::MicraWebSocketTest::backflushContract();
  shotstopper::MicraWebSocketTest::pongPublicationRace();
  shotstopper::MicraWebSocketTest::concurrentStatusLifecycle();
  shotstopper::MicraWebSocketTest::setupAdmissionAndBackoff();
}
