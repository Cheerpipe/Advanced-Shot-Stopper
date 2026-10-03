#include <algorithm>
#include <cassert>
#include <cstring>
#include <string>
#include <thread>
#include "machine/ShotStopperMicraPublicIdentityCache.h"
#include "machine/ShotStopperMicraService.h"
#include "machine/ShotStopperMicraStomp.h"
#include "ShotStopperOutboundAdmission.h"
#include "ShotStopperJsonArena.h"

static uint64_t now = 1000;
uint32_t millis() { return static_cast<uint32_t>(now); }
int64_t esp_timer_get_time() { return static_cast<int64_t>(now * 1000); }
void xTaskNotifyGive(TaskHandle_t) {}
static bool callback = false, locked = false;
static unsigned stops = 0, destroys = 0;
static std::string transmitted;
static int sendLimit = -1;
constexpr int ESP_OK = 0;
constexpr int WEBSOCKET_EVENT_ANY = 0, WEBSOCKET_EVENT_CONNECTED = 1;
constexpr int WEBSOCKET_EVENT_DATA = 2, WEBSOCKET_EVENT_ERROR = 3, WEBSOCKET_EVENT_DISCONNECTED = 4;
constexpr int WEBSOCKET_EVENT_CLOSED = 5;
using esp_websocket_client_handle_t = void *;
struct esp_websocket_event_data_t {
  void *client = nullptr;
  struct { int esp_ws_handshake_status_code = 0; } error_handle;
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
  return reinterpret_cast<void *>(1);
}
int esp_websocket_register_events(void *, int, Handler, void *) { assert(!locked); return ESP_OK; }
int esp_websocket_unregister_events(void *, int, Handler) { assert(!locked && !callback); return ESP_OK; }
int esp_websocket_client_start(void *) { assert(!locked && !callback); return ESP_OK; }
int esp_websocket_client_stop(void *) { assert(!locked && !callback); ++stops; now += 2; return ESP_OK; }
int esp_websocket_client_destroy(void *) { assert(!locked && !callback); ++destroys; return ESP_OK; }
int esp_websocket_client_send_text(void *, const char *bytes, int count, int) {
  assert(!locked);
  transmitted.assign(bytes, count);
  return sendLimit < 0 ? count : std::min(sendLimit, count);
}
#include "micra_websocket_work.inc"
namespace shotstopper {
void secureWipe(void *bytes, size_t count) { std::memset(bytes, 0, count); }
bool ShotStopperMicraService::networkEligible(LineaMicraError &error) const {
  error = LineaMicraError::NONE;
  return true;
}
LineaMicraError ShotStopperMicraService::classifyFailure() const { return LineaMicraError::TRANSPORT; }
bool ShotStopperMicraService::ensureWorkBuffer() {
  if (!work_) work_ = new WorkBuffer;
  return true;
}
bool ShotStopperMicraService::ensureSession(LineaMicraPersistedSettings &, bool, bool *) {
  std::strcpy(work_->accessToken, "synthetic-token");
  work_->accessTokenIssuedAtMs = now;
  return true;
}
void ShotStopperMicraService::clearSession() { work_->accessToken[0] = '\0'; }
bool ShotStopperMicraService::applySignedHeaders(const LineaMicraPersistedSettings &, char *headers, size_t) {
  std::strcpy(headers, "synthetic-signed-upgrade\r\n");
  return true;
}
#include "machine/ShotStopperMicraWebSocket.inc"
struct MicraWebSocketTest {
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
    // Early discovery rejects even callbacks arriving before owner stop.
    publishOutboundAcquisition(true, ++now);
    deliver(service, observation(service, off));
    assert(service.published_.sampleAtMs == fresh);
    service.serviceWebSocket();
    assert(stops == 1 && destroys == 1 && !service.websocket_->client);
    assert(service.websocketStatus().state == MicraSocketState::PAUSED);
    now += 100000;
    assert(service.powerState_.effectiveStatus(service.published_, true, now).effectiveOn);
    publishOutboundAcquisition(false, now);
    service.serviceWebSocket();
    subscribe(service);
    assert(service.powerState_.retained());
    deliver(service, observation(service, off));
    assert(!service.powerState_.retained());
    // A whole shot can elapse while the owner is busy: old ingress stays fenced.
    deliver(service, observation(service, on));
    const auto beforeShortShot = service.published_.sampleAtMs;
    outboundShotActive.store(true);
    service.inhibitCloud();
    outboundShotActive.store(false);
    deliver(service, observation(service, off));
    assert(service.published_.sampleAtMs == beforeShortShot);
    assert(service.published_.powerState == LineaMicraPowerState::ON);
    service.serviceWebSocket();
    subscribe(service);
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
};
}
int main() {
  shotstopper::initJsonParser();
  shotstopper::MicraWebSocketTest::run();
  shotstopper::MicraWebSocketTest::lifecycleRegressions();
}
