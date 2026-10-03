#define SHOT_STOPPER_HOST_TEST
#include "shot_stopper_host_stubs.h"
#include "../ShotStopperSerialCli.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <thread>

using namespace shotstopper;

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

  recordBootHeap(BootHeapStage::RESERVE_BEFORE, BootHeapResult::OK, 100, heap(120000, 66000));
  recordBootHeap(BootHeapStage::RESERVE_AFTER, BootHeapResult::OK, 101,
                 heap(60000, 32000), 0x3fcd8000, 60000);
  hostHeapCapsSnapshot() = heap(8000, 4000);
  serviceBootHeap(200);
  assert(bootHeapCapture.held.samples == 2);
  assert(bootHeapCapture.held.free == 8000);
  recordBootHeap(BootHeapStage::RELEASE_BEFORE, BootHeapResult::TIMEOUT, 60100,
                 heap(16000, 10000));
  recordBootHeap(BootHeapStage::RELEASE_AFTER, BootHeapResult::TIMEOUT, 60101,
                 heap(76000, 60000));
  hostHeapCapsSnapshot() = heap(70000, 46000);
  serviceBootHeap(60200);
  assert(bootHeapCapture.post.free == 70000);
  assert(bootHeapCapture.held.free == 8000);
  recordBootHeap(BootHeapStage::WS_DONE, BootHeapResult::FAILED, 60300, heap(71000, 47000));
  recordBootHeap(BootHeapStage::WS_DONE, BootHeapResult::OK, 60400, heap(72000, 48000));
  const auto &ws = bootHeapCapture.records[static_cast<size_t>(BootHeapStage::WS_DONE)];
  assert(ws.result == BootHeapResult::FAILED && ws.atMs == 60300);
  serviceBootHeap(120101);
  assert(bootHeapCapture.complete && !bootHeapSampling.load());
  const auto postSamples = bootHeapCapture.post.samples;
  hostHeapCapsSnapshot() = heap(0, 0);
  serviceBootHeap(120201);
  assert(bootHeapCapture.post.samples == postSamples);
  serialCliPrintBootHeap();
  assert(Serial.tx.find("status=complete requested=60000 address=0x3fcd8000") != std::string::npos);
  assert(Serial.tx.find("release_after,timeout,60101,0,76000,60000") != std::string::npos);
  assert(Serial.tx.find("held_min samples=3 free=8000") != std::string::npos);

  // Failed shaping still has a bounded post-start capture; elapsed time wraps safely.
  resetCapture();
  const uint32_t start = UINT32_MAX - 100;
  recordBootHeap(BootHeapStage::RESERVE_BEFORE, BootHeapResult::OK, start, heap(50000, 30000));
  recordBootHeap(BootHeapStage::RESERVE_AFTER, BootHeapResult::FAILED, start,
                 heap(50000, 30000), 0, 60000);
  recordBootHeap(BootHeapStage::RELEASE_AFTER, BootHeapResult::FAILED, start, heap(50000, 30000));
  serviceBootHeap(start + BOOT_HEAP_POST_RELEASE_MS - 1);
  assert(!bootHeapCapture.complete && bootHeapCapture.held.samples == 0);
  assert(bootHeapCapture.post.free == 0);  // Zero is a real exhausted sample.
  serviceBootHeap(start + BOOT_HEAP_POST_RELEASE_MS);
  assert(bootHeapCapture.complete);

  // Concurrent producers retain one immutable record per stage.
  resetCapture();
  recordBootHeap(BootHeapStage::RESERVE_BEFORE, BootHeapResult::OK, 1, heap(UINT32_MAX, UINT32_MAX));
  auto producer = [] {
    for (unsigned j = 0; j < 100; ++j) {
      for (size_t i = 1; i < static_cast<size_t>(BootHeapStage::COUNT); ++i)
        recordBootHeap(static_cast<BootHeapStage>(i), BootHeapResult::OK, UINT32_MAX,
                       heap(UINT32_MAX, UINT32_MAX), UINT32_MAX, UINT32_MAX);
    }
  };
  std::thread first(producer), second(producer);
  first.join();
  second.join();
  serialCliPrintBootHeap();
  assert(bootHeapCapture.stages == (1U << static_cast<unsigned>(BootHeapStage::COUNT)) - 1);
  assert(Serial.tx.size() < SERIAL_CLI_OUTPUT_CAPACITY);

  // The real service must enforce expiry before failed-startup/restart returns.
  const auto path = std::filesystem::path(__FILE__).parent_path().parent_path() /
                    "network/ShotStopperNetworkService.inc";
  std::ifstream input(path);
  assert(input);
  const std::string source((std::istreambuf_iterator<char>(input)), {});
  const auto service = source.find("void ShotStopperNetwork::service() {");
  const auto expiry = source.find("serviceHeapShaper(now);", service);
  assert(service != std::string::npos && expiry != std::string::npos);
  assert(expiry < source.find("if (restartPending_)", service));
  assert(expiry < source.find("if (!startupComplete_)", service));
}
