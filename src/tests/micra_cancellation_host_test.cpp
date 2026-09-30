#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include "machine/ShotStopperMicraPublicIdentityCache.h"
#include "machine/ShotStopperMicraService.h"

// Only scheduling and transport are simulated; extracted service code is unchanged.
uint32_t millis() { return 1000; }
void xTaskNotifyGive(TaskHandle_t) {}
constexpr int ESP_OK = 0, ESP_FAIL = -1, ESP_ERR_HTTP_EAGAIN = 1;
constexpr int ESP_ERR_HTTP_READ_TIMEOUT = 2, pdTRUE = 1;
uint32_t pdMS_TO_TICKS(uint32_t ms) { return ms; }
struct WorkerStopped {};
static std::function<int()> performHook;
static std::function<void()> executeHook;
static unsigned waits = 0;
int esp_http_client_perform(void *) { return performHook(); }
void esp_http_client_cleanup(void *) {}
unsigned ulTaskNotifyTake(int, uint32_t ticks) {
  if (ticks == 250) throw WorkerStopped{};
  ++waits;
  return 0;
}
namespace shotstopper {
bool firmwareCompatibilityMode() { return false; }
bool ShotStopperMicraService::networkEligible(LineaMicraError &) const {
  return staConnected_.load() && !apActive_.load();
}
void ShotStopperMicraService::releaseIoBuffer(bool) {}
void ShotStopperMicraService::releaseWorkBuffer() { clearSession(); }
void ShotStopperMicraService::execute(PendingRequest &) { executeHook(); }
bool ShotStopperMicraService::executeTemperatureApplication(
    const LineaMicraRequest &, uint32_t) {
  executeHook();
  desiredTemperature_.present = false;
  return true;
}
bool ShotStopperMicraService::executePowerApplication(
    const LineaMicraRequest &, uint32_t) {
  executeHook();
  desiredPower_.present = false;
  return true;
}
}
#include "micra_cancellation_methods.inc"

using namespace shotstopper;
namespace shotstopper {
struct MicraCancellationTest {
  static void progress(ShotStopperMicraService &service, int outcome,
                       bool cancel, bool recoverDuringPerform);
  static void run();
};
}

void MicraCancellationTest::progress(ShotStopperMicraService &service, int outcome,
                                     bool cancel, bool recoverDuringPerform) {
  auto &shotActive_ = service.shotActive_;
  auto &scaleConnecting_ = service.scaleConnecting_;
  auto &abortRequested_ = service.abortRequested_;
  auto networkEligible = [&](LineaMicraError &error) {
    return service.networkEligible(error);
  };
  LineaMicraError gateError = LineaMicraError::NONE;
  ShotStopperMicraService::WorkBuffer work;
  auto *work_ = &work;
  const uint32_t requestStartedAtMs = millis();
  unsigned calls = 0;
  waits = 0;
  performHook = [&] {
    ++calls;
    if (cancel) {
      service.publishNetworkState(true, false, true, false);
      if (recoverDuringPerform)
        service.publishNetworkState(true, false, false, false);
    }
    return calls == 1 ? outcome : ESP_OK;
  };
#include "micra_cancellation_progress.inc"
  assert(canceled == cancel);
  assert(performed == (cancel ? ESP_FAIL : ESP_OK));
  assert(calls == (cancel || outcome != ESP_ERR_HTTP_EAGAIN ? 1U : 2U));
  assert(waits == (cancel || outcome != ESP_ERR_HTTP_EAGAIN ? 0U : 1U));
  performHook = {};
}

void MicraCancellationTest::run() {
  ShotStopperMicraService service;
  service.task_ = &service;
  LineaMicraPersistedSettings settings;
  settings.accountConfigured = true;
  std::strcpy(settings.username, "test@example.invalid");
  std::strcpy(settings.selectedSerial, "TEST");
  settings.options = LINEA_MICRA_APPLY_TEMPERATURE | LINEA_MICRA_SHUTDOWN_WITH_SCALE;
  service.publishConfig(settings, 1);
  service.publishNetworkState(true, false, false, false);
  LineaMicraRequest request;
  request.type = LineaMicraRequestType::APPLY_TEMPERATURE;
  request.presetId = 1;
  request.targetDeciC = 930;
  assert(service.queue(request));

  // The worker must consume old cancellation before admission, never after it.
  unsigned executions = 0;
  executeHook = [&] {
    ++executions;
    assert(!service.abortRequested_.load());
  };
  try { service.taskLoop(); } catch (const WorkerStopped &) {}
  assert(executions == 1);
  assert(service.queue(request));
  static ShotStopperMicraService *admitting = &service;
  TaskMutex::hostObserver = [](const TaskMutex *, bool locked) {
    if (!locked && admitting->active_) {
      admitting->abortRequested_.store(true);  // Producer at admission boundary.
      TaskMutex::hostObserver = nullptr;
    }
  };
  executeHook = [&] {
    ++executions;
    assert(service.abortRequested_.load());
  };
  try { service.taskLoop(); } catch (const WorkerStopped &) {}
  assert(executions == 2);
  TaskMutex::hostObserver = nullptr;

  for (bool power : {false, true}) {
    service.abortRequested_.store(false);
    service.temperatureActive_.store(!power);
    service.powerActive_.store(power);
    request.type = power ? LineaMicraRequestType::SET_STANDBY
                         : LineaMicraRequestType::APPLY_TEMPERATURE;
    assert(service.queue(request));
    service.publishNetworkState(true, false, false, false);
    assert(service.abortRequested_.load());
  }
  service.abortRequested_.store(false);
  settings.options = 0;
  service.publishConfig(settings, 2);
  service.publishNetworkState(true, false, false, false);
  assert(service.abortRequested_.load());

  // Cleanup during installation/session renewal must preserve cancellation.
  ShotStopperMicraService::WorkBuffer work;
  service.work_ = &work;
  service.clearSession();
  assert(service.abortRequested_.load());
  service.work_ = nullptr;

  for (int event = 0; event < 4; ++event) {
    service.abortRequested_.store(false);
    service.publishNetworkState(event != 0, event == 1, event == 2, event == 3);
    service.publishNetworkState(true, false, false, false);
    assert(service.abortRequested_.load());
  }
  for (bool recover : {false, true}) {
    for (int outcome : {ESP_ERR_HTTP_EAGAIN, ESP_OK, ESP_FAIL}) {
      service.publishNetworkState(true, false, false, false);
      service.abortRequested_.store(false);
      progress(service, outcome, true, recover);
    }
  }
  service.publishNetworkState(true, false, false, false);
  service.abortRequested_.store(false);
  progress(service, ESP_ERR_HTTP_EAGAIN, false, false);

  // Network publication cannot erase cancellation from a concurrent producer.
  service.abortRequested_.store(false);
  std::thread publisher([&] {
    for (unsigned i = 0; i < 10000; ++i)
      service.publishNetworkState(true, false, false, false);
  });
  service.abortRequested_.store(true);
  publisher.join();
  assert(service.abortRequested_.load());
}

int main() { MicraCancellationTest::run(); }
