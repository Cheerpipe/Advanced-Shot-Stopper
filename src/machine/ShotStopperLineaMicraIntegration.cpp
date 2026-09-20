#include "ShotStopperLineaMicraIntegration.h"

#include "ShotStopperMachineIntegration.h"
#include "ShotStopperMicraService.h"
#include "ShotStopperPersistedSettings.h"

namespace shotstopper {
namespace {

ShotStopperMicraService service;

}  // namespace

bool initializeMachineIntegration() { return service.begin(); }

void publishMachineIntegrationConfig(const PersistedSettings &settings,
                                     uint32_t configGeneration) {
  service.publishConfig(settings.lineaMicra, configGeneration);
}

void publishMachineIntegrationNetworkState(bool staConnected, bool apActive,
                                           bool shotActive) {
  service.publishNetworkState(staConnected, apActive, shotActive);
}

void serviceMachineIntegrationAbort() { service.serviceAbort(); }

uint8_t machineIntegrationTaskCount() { return 1; }

bool queueMachineIntegrationConnect(uint32_t requestId, const char *username,
                                    const char *password) {
  return service.queueConnect(requestId, username, password);
}

bool queueMachineIntegrationRequest(const LineaMicraRequest &request) {
  return service.queue(request);
}

bool selectMachineIntegrationDevice(
    const char *serial, LineaMicraPersistedSettings &settings) {
  return service.selectDiscoveredMachine(serial, settings);
}

void clearMachineIntegrationDiscovery() { service.clearDiscovery(); }

LineaMicraStatus machineIntegrationStatus() { return service.status(); }

LineaMicraDiscoverySnapshot machineIntegrationDiscovery() {
  return service.discovery();
}

}  // namespace shotstopper
