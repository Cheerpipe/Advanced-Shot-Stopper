#include "ShotStopperMachineIntegration.h"
#include "ShotStopperLineaMicraIntegration.h"

#include "ShotStopperMicraService.h"
#include "ShotStopperPersistedSettings.h"

#include <Arduino.h>
#include <atomic>

namespace shotstopper {
namespace {

std::atomic<uint32_t> maxExecutionUs{0};
ShotStopperMicraService service;

}  // namespace

bool initializeMachineIntegration() { return service.begin(); }

void serviceMachineIntegrationWorker() {
  const uint32_t startedUs = micros();
  service.service();
  const uint32_t elapsedUs = micros() - startedUs;
  uint32_t observed = maxExecutionUs.load(std::memory_order_relaxed);
  while (elapsedUs > observed &&
         !maxExecutionUs.compare_exchange_weak(
             observed, elapsedUs, std::memory_order_relaxed,
             std::memory_order_relaxed)) {
  }
}

uint32_t machineIntegrationMaxExecutionUs() {
  return maxExecutionUs.load(std::memory_order_relaxed);
}

void publishMachineIntegrationConfig(const PersistedSettings &settings,
                                     uint32_t configGeneration) {
  service.publishConfig(settings.lineaMicra, configGeneration);
}

bool queueMachineIntegrationRequest(const LineaMicraRequest &request) {
  return service.queue(request);
}

LineaMicraStatus machineIntegrationStatus() { return service.status(); }

bool takeMachineIntegrationBinding(LineaMicraBindingResult &result) {
  return service.takeBinding(result);
}

static_assert(machineIntegrationTaskCount() == 0,
              "machine integration must reuse the BLE worker");

}  // namespace shotstopper
