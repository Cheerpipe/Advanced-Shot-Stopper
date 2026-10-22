#define OPEN_BREW_BY_WEIGHT_HOST_TEST
#include "open_brew_by_weight_host_stubs.h"
#include "../OpenBrewByWeightSerialCli.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace openbrewbyweight;

static HeapCapSnapshot heap(uint32_t free, uint32_t largest) {
  HeapCapSnapshot sample;
  sample.internalFree = free;
  sample.internalLargest = largest;
  return sample;
}

static void resetCapture() {
  TaskLockGuard lock(bootHeapMutex);
  bootHeapCapture = {};
  bootHeapSampling.store(false);
  Serial.tx.clear();
}

int main() {
  // No late USB connection or debug setting is needed to obtain retained data.
  resetCapture();
  serviceBootHeap(100);
  assert(bootHeapCapture.stages == 0);
  serialCliPrintBootHeap();
  assert(Serial.tx.find("status=not_started") != std::string::npos);
  SerialCliRequest request;
  assert(serialCliParseLine("bOoT_hEaP", request));
  assert(request.verb == SerialCliVerb::BOOT_HEAP);
  assert(!serialCliParseLine("BOOT_HEAP extra", request));
  assert(request.verb == SerialCliVerb::INVALID_ARGS);
  assert(serialCliParseLine("task_profile ON", request));
  assert(request.verb == SerialCliVerb::TASK_PROFILE && strcmp(request.arg1, "ON") == 0);
  assert(serialCliParseLine("TASK_PROFILE maybe", request));
  assert(request.verb == SerialCliVerb::INVALID_ARGS);

  // Milestones before network start are not attributed to this boot's capture.
  recordBootHeap(BootHeapStage::WIFI_INIT, BootHeapResult::OK, 50, heap(1, 1));
  assert(bootHeapCapture.stages == 0);
  recordBootHeap(BootHeapStage::NETWORK_START, BootHeapResult::OK, 100, heap(120000, 66000));
  recordBootHeap(BootHeapStage::WIFI_INIT, BootHeapResult::OK, 101, heap(90000, 40000));
  hostHeapCapsSnapshot() = heap(70000, 46000);
  serviceBootHeap(200);
  assert(bootHeapCapture.post.samples == 3 && bootHeapCapture.post.free == 70000);
  recordBootHeap(BootHeapStage::WS_DONE, BootHeapResult::FAILED, 60000, heap(71000, 47000));
  recordBootHeap(BootHeapStage::WS_DONE, BootHeapResult::OK, 60050, heap(72000, 48000));
  const auto &ws = bootHeapCapture.records[static_cast<size_t>(BootHeapStage::WS_DONE)];
  assert(ws.result == BootHeapResult::FAILED && ws.atMs == 60000);
  assert(bootHeapCapture.ws.free == 71000);  // Earlier post-start samples are excluded.
  serviceBootHeap(60100);  // Past the post-start window: only the WS window samples.
  const auto postWindowSamples = bootHeapCapture.post.samples;
  assert(postWindowSamples == 4 && bootHeapCapture.ws.free == 70000);
  serviceBootHeap(119999);
  assert(!bootHeapCapture.complete);
  serviceBootHeap(120000);
  assert(bootHeapCapture.complete && !bootHeapSampling.load());
  assert(bootHeapCapture.post.samples == postWindowSamples);
  hostHeapCapsSnapshot() = heap(0, 0);
  serviceBootHeap(120400);
  assert(bootHeapCapture.post.samples == postWindowSamples);
  serialCliPrintBootHeap();
  assert(Serial.tx.find("status=complete postWindowMs=60000 wsWindowMs=60000") != std::string::npos);
  assert(Serial.tx.find("network_start,ok,100,120000,66000") != std::string::npos);
  assert(Serial.tx.find("post_min samples=4 free=70000") != std::string::npos);
  assert(Serial.tx.find("ws_min samples=3 free=70000") != std::string::npos);
  assert(Serial.tx.find("held") == std::string::npos);

  // The post-start window closes on time across the millis() wrap.
  resetCapture();
  const uint32_t start = UINT32_MAX - 100;
  recordBootHeap(BootHeapStage::NETWORK_START, BootHeapResult::OK, start, heap(0, 0));
  serviceBootHeap(start + BOOT_HEAP_WINDOW_MS - 1);
  assert(!bootHeapCapture.complete);
  assert(bootHeapCapture.post.free == 0);  // Zero is a real exhausted sample.
  serviceBootHeap(start + BOOT_HEAP_WINDOW_MS);
  assert(bootHeapCapture.complete);

  // A late WS attempt gets a bounded window even if it never reports an outcome.
  resetCapture();
  recordBootHeap(BootHeapStage::NETWORK_START, BootHeapResult::OK, 100, heap(80000, 32000));
  recordBootHeap(BootHeapStage::WS_START, BootHeapResult::OK, 60102, heap(80000, 32000));
  recordBootHeap(BootHeapStage::WS_INIT, BootHeapResult::OK, 60103, heap(78000, 30000));
  recordBootHeap(BootHeapStage::WS_TASK, BootHeapResult::OK, 60104, heap(72000, 24000));
  serviceBootHeap(60101);  // The callback timestamp can postdate the health clock sample.
  assert(!bootHeapCapture.complete && bootHeapCapture.ws.samples == 0);
  serviceBootHeap(120101);
  assert(!bootHeapCapture.complete);
  serviceBootHeap(120102);
  assert(bootHeapCapture.complete && bootHeapCapture.ws.samples == 0);
  serialCliPrintBootHeap();
  assert(Serial.tx.find("ws_init,ok,60103") != std::string::npos);
  assert(Serial.tx.find("ws_task,ok,60104") != std::string::npos);

  // Concurrent producers retain one immutable record per stage.
  resetCapture();
  recordBootHeap(BootHeapStage::NETWORK_START, BootHeapResult::OK, 1, heap(UINT32_MAX, UINT32_MAX));
  auto producer = [] {
    for (unsigned j = 0; j < 100; ++j) {
      for (size_t i = 1; i < static_cast<size_t>(BootHeapStage::COUNT); ++i)
        recordBootHeap(static_cast<BootHeapStage>(i), BootHeapResult::OK, UINT32_MAX,
                       heap(UINT32_MAX, UINT32_MAX));
    }
  };
  std::thread first(producer), second(producer);
  first.join();
  second.join();
  serialCliPrintBootHeap();
  assert(bootHeapCapture.stages == (1U << static_cast<unsigned>(BootHeapStage::COUNT)) - 1);
  assert(Serial.tx.size() < SERIAL_CLI_OUTPUT_CAPACITY);

  // Network start opens the capture before Wi-Fi, mDNS and HTTP bring-up.
  const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                    "network/OpenBrewByWeightWifi.inc";
  std::ifstream input(path);
  assert(input);
  const std::string source((std::istreambuf_iterator<char>(input)), {});
  const auto opened = source.find("recordBootHeap(BootHeapStage::NETWORK_START");
  assert(opened != std::string::npos);
  assert(opened < source.find("startStation(settings, now);", opened));
}
