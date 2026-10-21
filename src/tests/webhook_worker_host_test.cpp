#define SHOT_STOPPER_HOST_TEST
#define SHOT_STOPPER_WEBHOOK_TEST_PLATFORM "tests/webhook_worker_platform.h"
#include "../OpenBrewWebhook.h"
#include "../OpenBrewPsram.h"
#include <algorithm>

static unsigned samples = 0;
static uint32_t heapFree = 1000;
static shotstopper::HeapCapSnapshot injectedHeapSample() {
  ++samples;
  workerTrace.push_back("sample");
  shotstopper::HeapCapSnapshot result;
  result.internalFree = heapFree;
  result.internalLargest = heapFree / 2;
  result.psramLargest = heapFree * 2;
  return result;
}
#define sampleHeapCaps injectedHeapSample
#include "../OpenBrewWebhook.cpp"
#undef sampleHeapCaps
#include <cstdarg>

static std::vector<std::string> logLines;
void serialTraceCategoryf(shotstopper::LogLevel, shotstopper::DebugCategory,
                          const char *fmt, ...) {
  char line[256];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  logLines.emplace_back(line);
}

namespace shotstopper {
struct WebhookDispatcherTest {
  static void run(WebhookDispatcher &d) { d.task(); }
  static void finish(WebhookDispatcher &d) {
    d.releaseWorkerFromTask();
    assert(d.stop());
  }
  static bool send(WebhookDispatcher &d, bool stale = false) {
    WebhookDispatcher::QueuedWebhook item;
    item.event.type = WebhookEventType::TEST;
    item.configGeneration = d.configGeneration_ - (stale ? 1 : 0);
    return d.send(item, 0) == WebhookDispatcher::SendOutcome::DELIVERED;
  }
  static void stopOnNextIteration(WebhookDispatcher &d) {
    d.workerState_ = WebhookDispatcher::WorkerState::STOPPING;
  }
  static bool stopAfterDrain(WebhookDispatcher &d) { return d.stopAfterDrain_; }
  static bool payload(WebhookDispatcher &d, const WebhookEvent &event,
                      char *output, size_t capacity) {
    return d.buildPayload(event, output, capacity);
  }
};
}

using namespace shotstopper;
static WebhookConfig config(bool enabled, bool deferDuringShot = false) {
  WebhookConfig result;
  result.enabled = enabled;
  result.deferDuringShot = deferDuringShot;
  strcpy(result.url, "http://example.test/hook?token=secret");
  return result;
}
static void resetPlatform() {
  workerTrace.clear();
  logLines.clear();
  afterReceive = afterTimeout = afterDelay = duringPerform = {};
  workerNow = samples = 0;
  heapFree = 1000;
  httpResult = ESP_OK;
  httpStatus = 200;
  wifiStatus = WL_CONNECTED;
  failHttpAllocation = false;
  httpHeadersSent = true;
  dnsHeld = dnsFails = false;
  dnsAnswer = {};
}
static void testSamplingAndAccounting() {
  for (unsigned scenario = 0; scenario != 5; ++scenario) {
    resetPlatform();
    WebhookDispatcher d;
    assert(d.begin(config(true, scenario == 4)));
    if (scenario == 1) httpResult = ESP_FAIL;
    if (scenario == 2) failHttpAllocation = true;
    if (scenario == 3) wifiStatus = 0;
    duringPerform = [&]() {
      heapFree = 700;
      if (scenario == 4) d.setControlCritical(true);
    };
    const bool expectedSuccess = scenario == 0;
    assert(WebhookDispatcherTest::send(d) == expectedSuccess);
    const auto status = d.status();
    assert(samples == 2 && status.heapSamples == 1);
    assert(status.held == (scenario == 4) && status.queued == 0);
    assert(status.internalFreeBefore == 1000);
    assert(status.internalFreeAfter == (scenario == 2 || scenario == 3 ? 1000U : 700U));
    assert(status.internalLargestAfter == status.internalFreeAfter / 2);
    assert(status.psramLargestAfter == status.internalFreeAfter * 2);
    const HeapLifecycleAggregate heap = d.heapTelemetry();
    assert(heap.cycles == 1);
    assert(heap.lastEvent == HeapLifecycleEvent::TLS_REQUEST);
    assert(heap.lastDelta.freeBytes ==
           static_cast<int32_t>(status.internalFreeAfter) - 1000);
    assert(status.sent == (expectedSuccess ? 1U : 0U));
    assert(status.dropped == (expectedSuccess ? 0U : 1U));
    assert(strcmp(status.lastEvent, "test") == 0);
    assert(strcmp(status.lastEndpoint, "http://example.test/hook") == 0);
    const WebhookRequestPhase expectedPhase =
        expectedSuccess ? WebhookRequestPhase::RESPONSE
        : scenario == 2 ? WebhookRequestPhase::CLIENT
        : scenario == 3 ? WebhookRequestPhase::PREPARE
                        : WebhookRequestPhase::PERFORM;
    assert(status.lastPhase == expectedPhase);
    // The stub still reports 200 after a failed perform(): it must not leak.
    assert(status.lastHttpStatus == (expectedSuccess ? 200U : 0U));
    assert(status.lastCancellation == (scenario == 4
        ? WebhookCancellationReason::CONTROL_CRITICAL : WebhookCancellationReason::NONE));
    assert(status.lastDeliveryUnknown == (scenario == 1 || scenario == 4));
    assert(workerTrace.front() == "sample" && workerTrace.back() == "sample");
    workerTrace.clear();
    assert(!WebhookDispatcherTest::send(d, true));
    assert(samples == 2 && workerTrace.empty());
    assert(d.status().staleConfigDropped == 1);
    assert(d.status().heapSamples == 1);
    if (scenario == 4) {
      d.setControlCritical(false);
      wifiStatus = 0;
      assert(!WebhookDispatcherTest::send(d));
      assert(d.status().lastCancellation == WebhookCancellationReason::NONE);
    }
    WebhookDispatcherTest::finish(d);
  }
}
static void testTimeoutHasNoSecondSleep() {
  resetPlatform();
  WebhookDispatcher d;
  assert(d.begin(config(true)));
  WebhookEvent event;
  event.type = WebhookEventType::TEST;
  afterTimeout = [&]() { assert(d.enqueue(event)); };
  duringPerform = [&]() { WebhookDispatcherTest::stopOnNextIteration(d); };
  WebhookDispatcherTest::run(d);
  assert(workerNow == 50);
  assert(std::count(workerTrace.begin(), workerTrace.end(), "delay") == 0);
  assert(d.status().sent == 1 && d.status().workerStops == 1);
  assert(d.stop());
}
static void testAcquisitionInterruptsPartialPostWithoutReplay() {
  resetPlatform();
  WebhookDispatcher d;
  assert(d.begin(config(true)));
  httpResult = ESP_ERR_HTTP_EAGAIN;
  duringPerform = [&] { publishOutboundAcquisition(true, workerNow); };
  assert(!WebhookDispatcherTest::send(d));
  const auto result = d.status();
  assert(result.lastCancellation == WebhookCancellationReason::SCALE_CONNECTING);
  assert(result.lastDeliveryUnknown && result.lastDispatched);
  assert(std::count(workerTrace.begin(), workerTrace.end(), "perform") == 1);
  const auto &completion = outboundPauseCompletions[static_cast<unsigned>(OutboundClient::WEBHOOK)];
  assert(completion.generation.load() == outboundAcquisitionGeneration.load());
  publishOutboundAcquisition(false, workerNow);
  WebhookDispatcherTest::finish(d);
}
static void testPartialPostProgressUsesOneDeadline() {
  for (bool timeout : {false, true}) {
    resetPlatform();
    WebhookDispatcher d;
    assert(d.begin(config(true)));
    unsigned progress = 0;
    httpResult = ESP_ERR_HTTP_EAGAIN;
    duringPerform = [&] { if (++progress == 3 && !timeout) httpResult = ESP_OK; };
    assert(WebhookDispatcherTest::send(d) == !timeout);
    assert(progress == (timeout ? 1800U : 3U));
    assert(workerNow == (timeout ? 1800U : 2U));
    assert(d.status().lastDeliveryUnknown == timeout);
    assert(std::count(workerTrace.begin(), workerTrace.end(), "post") == 1);
    WebhookDispatcherTest::finish(d);
  }
  resetPlatform();
  WebhookDispatcher late;
  assert(late.begin(config(true)));
  duringPerform = [&] { workerNow += 1800; };
  assert(!WebhookDispatcherTest::send(late));
  assert(late.status().lastError == ESP_ERR_TIMEOUT && late.status().lastDeliveryUnknown);
  assert(std::count(workerTrace.begin(), workerTrace.end(), "perform") == 1);
  WebhookDispatcherTest::finish(late);
}
static void testHeldItemSurvivesDrainAndGate() {
  for (bool changeConfig : {false, true}) {
    resetPlatform();
    WebhookDispatcher d;
    assert(d.begin(config(false, true)));
    WebhookEvent event;
    event.type = WebhookEventType::TEST;
    assert(d.enqueue(event)); // Disabled test worker stops after draining.
    afterReceive = [&]() { d.setControlCritical(true); };
    unsigned delays = 0;
    afterDelay = [&]() {
      if (++delays == 2) {
        if (changeConfig) d.setConfig(config(true, true));
        d.setControlCritical(false);
      }
      assert(delays <= 2);
    };
    // Enabling during the drain keeps the worker running after the stale drop.
    bool keptRunning = false;
    if (changeConfig) afterTimeout = [&]() {
      assert(d.status().workerStops == 0 && !WebhookDispatcherTest::stopAfterDrain(d));
      keptRunning = true;
      WebhookDispatcherTest::stopOnNextIteration(d);
    };
    WebhookDispatcherTest::run(d);
    assert(delays == 2 && keptRunning == changeConfig);
    assert(d.status().workerStops == 1);
    assert(d.status().sent == (changeConfig ? 0U : 1U));
    assert(d.status().staleConfigDropped == (changeConfig ? 1U : 0U));
    assert(d.stop());
  }
}
static void testQueueCapacity() {
  resetPlatform();
  WebhookDispatcher d;
  assert(d.begin(config(false)));
  WebhookEvent event;
  event.type = WebhookEventType::TEST;
  for (unsigned i = 0; i != 4; ++i) assert(d.enqueue(event));
  assert(!d.enqueue(event));
  WebhookDispatcherTest::run(d);
  assert(d.status().sent == 4 && d.status().dropped == 1);
  assert(d.stop());
}
static void testActivationHistorySubscription() {
  resetPlatform();
  WebhookDispatcher d;
  WebhookConfig live = config(true);
  assert(d.begin(live));
  WebhookEvent event;
  event.type = WebhookEventType::ACTIVATION_HISTORY;
  event.activationId = 7;
  event.activationType = static_cast<uint8_t>(HistoryType::RINSE);
  assert(d.enqueue(event)); // Unsubscribed: consumed without a drop.
  assert(d.status().dropped == 0);
  live.presetChanges = true;
  d.setConfig(live);
  assert(d.enqueue(event));
  event.type = WebhookEventType::CONTROLLER_STARTED;
  assert(d.enqueue(event));
  wifiStatus = 0;
  afterReceive = [&]() { wifiStatus = WL_CONNECTED; };
  afterTimeout = [&]() { WebhookDispatcherTest::stopOnNextIteration(d); };
  WebhookDispatcherTest::run(d);
  assert(d.status().sent == 2 && d.status().dropped == 0);
  assert(d.stop());
}
static void testWorkerLeavesLifecycleLockToEnqueue() {
  resetPlatform();
  WebhookDispatcher d;
  assert(d.begin(config(true)));
  WebhookEvent event;
  event.type = WebhookEventType::BREWING;
  unsigned timeouts = 0, base = 0;
  afterTimeout = [&]() {
    if (++timeouts == 1) {
      base = semaphoreTakes;
      assert(d.enqueue(event));
      return;
    }
    // Receive, send and idle passes take no lock beyond enqueue's own.
    assert(semaphoreTakes - base == 1);
    WebhookDispatcherTest::stopOnNextIteration(d);
  };
  WebhookDispatcherTest::run(d);
  assert(d.status().sent == 1 && d.status().dropped == 0);
  assert(d.stop());
}
static void testDisableReleasesClient() {
  resetPlatform();
  WebhookDispatcher d;
  assert(d.begin(config(true)));
  assert(WebhookDispatcherTest::send(d));
  assert(d.status().clientCreates == 1);
  d.setConfig(config(false));
  afterTimeout = [&]() {
    assert(d.status().clientCleanups == 1);
    WebhookDispatcherTest::stopOnNextIteration(d);
  };
  WebhookDispatcherTest::run(d);
  assert(d.status().clientCleanups == 1);
  assert(d.stop());
}
static void testUnsentRequestsAreNotUnknown() {
  for (unsigned scenario = 0; scenario != 2; ++scenario) {
    resetPlatform();
    WebhookDispatcher d;
    assert(d.begin(config(true)));
    if (scenario == 0) {
      httpHeadersSent = false;
      httpResult = ESP_ERR_HTTP_CONNECT;
    } else {
      dnsFails = true;
    }
    assert(!WebhookDispatcherTest::send(d));
    const auto status = d.status();
    assert(!status.lastDispatched && !status.lastDeliveryUnknown);
    assert(status.lastError == ESP_ERR_HTTP_CONNECT && status.dropped == 1);
    assert(std::count(workerTrace.begin(), workerTrace.end(), "perform") ==
           (scenario == 0 ? 1 : 0));
    WebhookDispatcherTest::finish(d);
  }
}
static void testHandshakeWaitEndsAtHeadersSent() {
  resetPlatform();
  WebhookDispatcher d;
  assert(d.begin(config(true)));
  for (const bool headersSent : {false, true, true}) {
    httpHeadersSent = headersSent;
    httpResult = headersSent ? ESP_OK : ESP_ERR_HTTP_WRITE_DATA;
    assert(WebhookDispatcherTest::send(d) == headersSent);
    // Every request waits up to 500 ms for its handshake, then polls at 25 ms.
    assert(timeoutAtPerform == 500);
    assert(timeoutAfterHeaders == (headersSent ? 25 : 500));
  }
  WebhookDispatcherTest::finish(d);
}
static void testSlowLookupYieldsToShotWithinOnePoll() {
  resetPlatform();
  WebhookDispatcher d;
  assert(d.begin(config(true, true)));
  dnsHeld = true;
  afterDelay = [&]() { d.setControlCritical(true); };
  assert(!WebhookDispatcherTest::send(d));
  auto status = d.status();
  assert(workerNow == 10);
  assert(status.lastCancellation == WebhookCancellationReason::CONTROL_CRITICAL);
  assert(!status.lastDispatched && status.dropped == 0);
  assert(std::count(workerTrace.begin(), workerTrace.end(), "perform") == 0);
  afterDelay = {};
  d.setControlCritical(false);
  dnsAnswer();
  assert(WebhookDispatcherTest::send(d));
  assert(std::count(workerTrace.begin(), workerTrace.end(), "resolve") == 1);
  WebhookDispatcherTest::finish(d);
}
static void testBootHintWaitsForSta() {
  resetPlatform();
  WebhookDispatcher d;
  WebhookConfig live = config(true);
  live.presetChanges = true;
  assert(d.begin(live));
  WebhookEvent event;
  event.type = WebhookEventType::CONTROLLER_STARTED;
  assert(d.enqueue(event));
  wifiStatus = 0;
  unsigned delays = 0;
  afterDelay = [&]() {
    if (++delays == 2) wifiStatus = WL_CONNECTED;
    assert(delays <= 2);
  };
  duringPerform = [&]() { WebhookDispatcherTest::stopOnNextIteration(d); };
  WebhookDispatcherTest::run(d);
  assert(delays == 2 && d.status().sent == 1 && d.status().dropped == 0);
  assert(d.stop());
}
static void testSecretWebhookEndpointRedacted() {
  char safe[192] = {};
  formatSafeHttpEndpoint(
      "http://example.test:8123/api/webhook/secret-value?token=hidden", safe,
      sizeof(safe));
  assert(strcmp(safe, "http://example.test:8123/api/webhook/[redacted]") == 0);
}
static void testIntegrationPayloads() {
  resetPlatform();
  WebhookDispatcher d;
  WebhookEvent event;
  event.type = WebhookEventType::END;
  event.bootId = 7;
  event.cycleId = 9;
  event.presetId = 2;
  strcpy(event.presetName, "Double \"A\"");
  strcpy(event.shotType, "auto");
  strcpy(event.stopDetail, "normal_target");
  char payload[2048] = {};
  assert(WebhookDispatcherTest::payload(d, event, payload, sizeof(payload)));
  assert(strstr(payload, "\"bootId\":7") != nullptr);
  assert(strstr(payload, "\"presetName\":\"Double \\\"A\\\"\"") != nullptr);

  event = WebhookEvent{};
  event.type = WebhookEventType::PRESETS_CHANGED;
  event.presetId = 1;
  event.presetRevision = 12;
  event.presetCount = 1;
  event.presets[0].id = 1;
  event.presets[0].isFactory = true;
  strcpy(event.presets[0].name, "Single");
  assert(WebhookDispatcherTest::payload(d, event, payload, sizeof(payload)));
  assert(strstr(payload, "\"event\":\"presets_changed\"") != nullptr);
  assert(strstr(payload, "\"revision\":12") != nullptr);
  assert(strstr(payload, "\"isFactory\":true") != nullptr);

  event.presetCount = 8;
  for (uint8_t i = 0; i < event.presetCount; ++i) {
    event.presets[i].id = static_cast<uint8_t>(i + 1);
    memset(event.presets[i].name, 1, sizeof(event.presets[i].name) - 1);
    event.presets[i].name[sizeof(event.presets[i].name) - 1] = '\0';
  }
  assert(WebhookDispatcherTest::payload(d, event, payload, sizeof(payload)));
  assert(strlen(payload) > 1024);
  assert(strstr(payload, "\\u0001") != nullptr);

  strcpy(event.presets[0].name, "A\"B");
  strcpy(event.presets[1].name, "AB");
  assert(WebhookDispatcherTest::payload(d, event, payload, sizeof(payload)));
  assert(strstr(payload, "A\\\"B") != nullptr);
  assert(strstr(payload, "\"name\":\"AB\"") != nullptr);

  event = WebhookEvent{};
  event.type = WebhookEventType::QUICK_SETTINGS_CHANGED;
  event.quickSettings.revision = 13;
  event.quickSettings.activePresetId = 2;
  event.quickSettings.brewByWeight = true;
  event.quickSettings.noScaleBbwMode =
      static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE);
  event.quickSettings.fastExtractionGuardEnabled = true;
  assert(WebhookDispatcherTest::payload(d, event, payload, sizeof(payload)));
  assert(strstr(payload, "\"event\":\"quick_settings_changed\"") != nullptr);
  assert(strstr(payload, "\"noScaleBbwMode\":\"warn_once\"") != nullptr);

  event = WebhookEvent{};
  event.type = WebhookEventType::CONTROLLER_STARTED;
  event.bootId = 8;
  event.presetRevision = 13;
  assert(WebhookDispatcherTest::payload(d, event, payload, sizeof(payload)));
  assert(strstr(payload, "\"event\":\"controller_started\"") != nullptr);
  assert(strstr(payload, "\"revision\":13") != nullptr);

  event = WebhookEvent{};
  event.type = WebhookEventType::ACTIVATION_HISTORY;
  event.activationId = 9;
  event.activationType = static_cast<uint8_t>(HistoryType::RINSE);
  assert(WebhookDispatcherTest::payload(d, event, payload, sizeof(payload)));
  assert(strstr(payload, "\"event\":\"integration_history_end\"") != nullptr);
  assert(strstr(payload, "\"type\":\"rinse\"") != nullptr);
  event.activationType = static_cast<uint8_t>(HistoryType::BACKFLUSH);
  assert(WebhookDispatcherTest::payload(d, event, payload, sizeof(payload)));
  assert(strstr(payload, "\"type\":\"backflush\"") != nullptr);
}
static void testFailedDeliveryRetriesWithBackoff() {
  for (unsigned scenario = 0; scenario != 3; ++scenario) {
    resetPlatform();
    WebhookDispatcher d;
    assert(d.begin(config(true)));
    WebhookEvent event;
    event.type = WebhookEventType::END;
    assert(d.enqueue(event));
    // 0: recovers on the last retry; 1: every attempt fails; 2: permanent 404.
    httpResult = scenario == 2 ? ESP_OK : ESP_FAIL;
    httpStatus = scenario == 2 ? 404 : 200;
    std::vector<uint32_t> attempts;
    duringPerform = [&] {
      attempts.push_back(workerNow);
      if (scenario == 0 && attempts.size() == 4) httpResult = ESP_OK;
    };
    afterTimeout = [&] { WebhookDispatcherTest::stopOnNextIteration(d); };
    WebhookDispatcherTest::run(d);
    const unsigned expected = scenario == 2 ? 1 : 4;
    assert(attempts.size() == expected);
    for (unsigned i = 1; i < attempts.size(); ++i)
      assert(attempts[i] - attempts[i - 1] >= i * 3000U &&
             attempts[i] - attempts[i - 1] < i * 3000U + 100U);
    assert(d.status().sent == (scenario == 0 ? 1U : 0U));
    assert(d.status().dropped == (scenario == 0 ? 0U : 1U));
    assert(logLines.size() == (scenario == 2 ? 1U : 4U));
    assert(logLines.front().find("attempt 1/4") != std::string::npos);
    const char *last = scenario == 0 ? "delivered on attempt 4/4"
        : scenario == 1 ? "attempt 4/4 failed" : "http=404";
    assert(logLines.back().find(last) != std::string::npos);
    if (scenario != 0) assert(logLines.back().find("next=drop") != std::string::npos);
    if (scenario != 2) assert(logLines.front().find("next=retry retryInS=3") != std::string::npos);
    assert(d.stop());
  }
}
static void testNewerStateSupersedesRetry() {
  for (bool fullState : {true, false}) {
    resetPlatform();
    WebhookDispatcher d;
    assert(d.begin(config(true)));
    WebhookEvent event;
    event.type = fullState ? WebhookEventType::BREWING : WebhookEventType::END;
    assert(d.enqueue(event));
    httpResult = ESP_FAIL;
    std::vector<uint32_t> attempts;
    duringPerform = [&] {
      attempts.push_back(workerNow);
      // A newer copy arrives while the first one fails; the newer one succeeds.
      if (fullState && attempts.size() == 2) httpResult = ESP_OK;
      if (attempts.size() != 1) return;
      if (fullState) event.type = WebhookEventType::IDLE;
      assert(d.enqueue(event));
    };
    afterTimeout = [&] { WebhookDispatcherTest::stopOnNextIteration(d); };
    WebhookDispatcherTest::run(d);
    const bool superseded = std::any_of(logLines.begin(), logLines.end(),
        [](const std::string &line) { return line.find("superseded") != std::string::npos; });
    assert(superseded == fullState);
    if (fullState) {
      // The newer state is sent at once instead of after the 3 s backoff.
      assert(attempts.size() == 2 && attempts[1] - attempts[0] < 3000U);
      assert(d.status().sent == 1 && d.status().dropped == 0);
    } else {
      // Every end event matters: both are attempted to exhaustion in order.
      assert(attempts.size() == 8 && attempts[1] - attempts[0] >= 3000U);
      assert(d.status().dropped == 2);
    }
    assert(d.stop());
  }
}
int main() {
  testSamplingAndAccounting();
  testTimeoutHasNoSecondSleep();
  testAcquisitionInterruptsPartialPostWithoutReplay();
  testPartialPostProgressUsesOneDeadline();
  testHeldItemSurvivesDrainAndGate();
  testQueueCapacity();
  testActivationHistorySubscription();
  testWorkerLeavesLifecycleLockToEnqueue();
  testDisableReleasesClient();
  testUnsentRequestsAreNotUnknown();
  testHandshakeWaitEndsAtHeadersSent();
  testSlowLookupYieldsToShotWithinOnePoll();
  testBootHintWaitsForSta();
  testSecretWebhookEndpointRedacted();
  testIntegrationPayloads();
  testFailedDeliveryRetriesWithBackoff();
  testNewerStateSupersedesRetry();
}
