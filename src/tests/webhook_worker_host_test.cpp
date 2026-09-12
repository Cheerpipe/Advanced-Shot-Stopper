#define SHOT_STOPPER_HOST_TEST
#define SHOT_STOPPER_WEBHOOK_TEST_PLATFORM "tests/webhook_worker_platform.h"
#include "../ShotStopperWebhook.h"
#include "../ShotStopperPsram.h"
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
#include "../ShotStopperWebhook.cpp"
#undef sampleHeapCaps

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
    return d.send(item);
  }
  static void stopOnNextIteration(WebhookDispatcher &d) {
    d.workerState_ = WebhookDispatcher::WorkerState::STOPPING;
  }
  static bool payload(WebhookDispatcher &d, const WebhookEvent &event,
                      char *output, size_t capacity) {
    return d.buildPayload(event, output, capacity);
  }
};
}

using namespace shotstopper;
static WebhookConfig config(bool enabled) {
  WebhookConfig result;
  result.enabled = enabled;
  strcpy(result.url, "http://example.test/hook");
  return result;
}
static void resetPlatform() {
  workerTrace.clear();
  afterReceive = afterTimeout = afterDelay = duringPerform = {};
  workerNow = samples = 0;
  heapFree = 1000;
  httpResult = ESP_OK;
  httpStatus = 200;
  wifiStatus = WL_CONNECTED;
  failHttpAllocation = false;
}
static void testSamplingAndAccounting() {
  for (unsigned scenario = 0; scenario != 5; ++scenario) {
    resetPlatform();
    WebhookDispatcher d;
    assert(d.begin(config(true)));
    if (scenario == 1) httpResult = ESP_FAIL;
    if (scenario == 2) failHttpAllocation = true;
    if (scenario == 3) wifiStatus = 0;
    duringPerform = [&]() {
      heapFree = 700;
      if (scenario == 4) d.setScaleConnecting(true);
    };
    assert(WebhookDispatcherTest::send(d) == (scenario == 0));
    const auto status = d.status();
    assert(samples == 2 && status.heapSamples == 1);
    assert(status.internalFreeBefore == 1000);
    assert(status.internalFreeAfter == (scenario == 2 || scenario == 3 ? 1000U : 700U));
    assert(status.internalLargestAfter == status.internalFreeAfter / 2);
    assert(status.psramLargestAfter == status.internalFreeAfter * 2);
    assert(status.sent == (scenario == 0 ? 1U : 0U));
    assert(status.dropped == (scenario == 0 ? 0U : 1U));
    assert(workerTrace.front() == "sample" && workerTrace.back() == "sample");
    workerTrace.clear();
    assert(!WebhookDispatcherTest::send(d, true));
    assert(samples == 2 && workerTrace.empty());
    assert(d.status().staleConfigDropped == 1);
    assert(d.status().heapSamples == 1);
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
static void testHeldItemSurvivesDrainAndGate() {
  for (bool changeConfig : {false, true}) {
    resetPlatform();
    WebhookDispatcher d;
    assert(d.begin(config(false)));
    WebhookEvent event;
    event.type = WebhookEventType::TEST;
    assert(d.enqueue(event)); // Disabled test worker stops after draining.
    afterReceive = [&]() { d.setScaleConnecting(true); };
    unsigned delays = 0;
    afterDelay = [&]() {
      if (++delays == 2) {
        if (changeConfig) d.setConfig(config(true));
        d.setScaleConnecting(false);
      }
      assert(delays <= 2);
    };
    // Configuring enabled clears stopAfterDrain; stop after the stale drop.
    if (changeConfig) afterTimeout = [&]() { WebhookDispatcherTest::stopOnNextIteration(d); };
    WebhookDispatcherTest::run(d);
    assert(delays == 2 && d.status().workerStops == 1);
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
}
int main() {
  testSamplingAndAccounting();
  testTimeoutHasNoSecondSleep();
  testHeldItemSurvivesDrainAndGate();
  testQueueCapacity();
  testIntegrationPayloads();
}
