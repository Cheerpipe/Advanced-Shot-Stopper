#include "ShotStopperMachineIntegration.h"

#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE
#include "ShotStopperMicraService.h"
#include <Arduino.h>
#endif

#include <atomic>

namespace shotstopper {
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE
namespace {

std::atomic<uint32_t> maxExecutionUs{0};

ShotStopperMicraService service;

}  // namespace

bool initializeMachineIntegration() {
  return service.begin();
}

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

void publishMachineIntegrationConfig(
    const MachineIntegrationPersistedSettings &settings,
    uint32_t configGeneration) {
  service.publishConfig(settings, configGeneration);
}

bool queueMachineIntegrationRequest(const MachineIntegrationRequest &request) {
  return service.queue(request);
}

MachineIntegrationStatus machineIntegrationStatus() {
  return service.status();
}

static_assert(machineIntegrationTaskCount() == 0,
              "machine integration must reuse the BLE worker");
#endif

}  // namespace shotstopper
