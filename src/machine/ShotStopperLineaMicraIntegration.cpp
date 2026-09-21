#include "ShotStopperLineaMicraIntegration.h"

#include "ShotStopperMachineIntegration.h"
#include "ShotStopperMicraScaleShutdown.h"
#include "ShotStopperMicraService.h"
#include "ShotStopperPersistedSettings.h"

#include <atomic>
#include <cstdint>

void serialTraceCategoryf(shotstopper::LogLevel level,
                          shotstopper::DebugCategory category,
                          const char *fmt, ...);

namespace shotstopper {
namespace {

ShotStopperMicraService service;
MicraScaleShutdownTracker scaleShutdown;
std::atomic<uint8_t> micraOptions{LINEA_MICRA_DEFAULT_OPTIONS};
std::atomic<bool> micraAccountConfigured{false};

}  // namespace

bool initializeMachineIntegration() { return service.begin(); }

void publishMachineIntegrationConfig(const PersistedSettings &settings,
                                     uint32_t configGeneration) {
  micraOptions.store(settings.lineaMicra.options,
                     std::memory_order_relaxed);
  micraAccountConfigured.store(settings.lineaMicra.accountConfigured,
                               std::memory_order_relaxed);
  service.publishConfig(settings.lineaMicra, configGeneration);
}

void publishMachineIntegrationNetworkState(bool staConnected, bool apActive,
                                           bool shotActive,
                                           bool scaleConnecting) {
  service.publishNetworkState(staConnected, apActive, shotActive,
                              scaleConnecting);
}

void requestMachineIntegrationPresetTemperature(
    uint8_t presetId, uint32_t configGeneration, uint16_t targetDeciC) {
  LineaMicraRequest request;
  request.configGeneration = configGeneration;
  request.targetDeciC = targetDeciC;
  request.presetId = presetId;
  request.type = LineaMicraRequestType::APPLY_TEMPERATURE;
  service.queue(request);
}

void serviceMachineIntegrationAbort() { service.serviceAbort(); }

void serviceMachineIntegrationScaleLink(uint32_t now, bool scaleLinkUp,
                                        uint32_t scaleDisconnectSequence,
                                        uint8_t scaleDisconnectReason,
                                        bool relayClosed) {
  const MicraScaleShutdownTracker::Snapshot snapshot{
      scaleDisconnectSequence, scaleDisconnectReason, scaleLinkUp,
      relayClosed};
  if (!scaleShutdown.service(now, snapshot,
                             micraOptions.load(std::memory_order_relaxed),
                             micraAccountConfigured.load(
                                 std::memory_order_relaxed))) {
    return;
  }
  serialTraceCategoryf(LogLevel::INFO, DebugCategory::NETWORK,
                       "Micra scale shutdown: scale powered off, requesting StandBy");
  LineaMicraRequest request;
  request.type = LineaMicraRequestType::SET_STANDBY;
  service.queue(request);
}

uint8_t machineIntegrationTaskCount() { return 1; }

MachinePhysicalStartDisposition machineIntegrationPhysicalStart() {
  return service.physicalStart();
}

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

HeapLifecycleAggregate machineIntegrationHeapTelemetry() {
  return service.heapTelemetry();
}

LineaMicraDiscoverySnapshot machineIntegrationDiscovery() {
  return service.discovery();
}

}  // namespace shotstopper
