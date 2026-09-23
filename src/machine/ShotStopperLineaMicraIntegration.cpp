#include "ShotStopperLineaMicraIntegration.h"

#include "ShotStopperMachineIntegration.h"
#include "ShotStopperMicraMachinePower.h"
#include "ShotStopperMicraScalePowerOn.h"
#include "ShotStopperMicraScaleShutdown.h"
#include "ShotStopperMicraService.h"
#include "ShotStopperPersistedSettings.h"
#include "ShotStopperScaleWorker.h"

#include <atomic>
#include <cstdint>

void serialTraceCategoryf(shotstopper::LogLevel level,
                          shotstopper::DebugCategory category,
                          const char *fmt, ...);

namespace shotstopper {
namespace {

ShotStopperMicraService service;
MicraScaleShutdownTracker scaleShutdown;
MicraScalePowerOnTracker scalePowerOn;
MicraMachinePowerTracker machinePower;
std::atomic<uint8_t> micraOptions{LINEA_MICRA_DEFAULT_OPTIONS};
std::atomic<uint8_t> micraScaleOptions{0};
std::atomic<bool> micraAccountConfigured{false};

// A standby command is pointless once the machine is already effectively off.
// Skipping it also keeps the remote disconnect that follows our own scale
// power-off command from re-arming the shutdown cycle.
bool machineEffectivelyOff() {
  const LineaMicraStatus status = service.status();
  return !status.effectiveOn &&
         (status.quality == LineaMicraObservationQuality::CURRENT ||
          status.quality == LineaMicraObservationQuality::OPTIMISTIC);
}

}  // namespace

bool initializeMachineIntegration() { return service.begin(); }

void publishMachineIntegrationConfig(const PersistedSettings &settings,
                                     uint32_t configGeneration) {
  micraOptions.store(settings.lineaMicra.options,
                     std::memory_order_relaxed);
  micraScaleOptions.store(settings.lineaMicra.scaleOptions,
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
  const uint8_t options = micraOptions.load(std::memory_order_relaxed);
  const bool accountConfigured =
      micraAccountConfigured.load(std::memory_order_relaxed);
  const MicraScaleShutdownTracker::Snapshot shutdownSnapshot{
      scaleDisconnectSequence, scaleDisconnectReason, scaleLinkUp,
      relayClosed};
  if (scaleShutdown.service(now, shutdownSnapshot, options,
                            accountConfigured) &&
      !machineEffectivelyOff()) {
    serialTraceCategoryf(LogLevel::INFO, DebugCategory::NETWORK,
                         "Micra scale shutdown: scale powered off, requesting StandBy");
    LineaMicraRequest request;
    request.type = LineaMicraRequestType::SET_STANDBY;
    service.queue(request);
  }
  const MicraScalePowerOnTracker::Snapshot powerOnSnapshot{
      scaleLinkUp, scaleDisconnectReason, relayClosed};
  if (scalePowerOn.service(powerOnSnapshot, options, accountConfigured)) {
    serialTraceCategoryf(LogLevel::INFO, DebugCategory::NETWORK,
                         "Micra scale power-on: scale powered on, requesting BrewingMode");
    LineaMicraRequest request;
    request.type = LineaMicraRequestType::SET_POWER_ON;
    service.queue(request);
  }
}

void serviceMachineIntegrationMachinePower(bool scaleLinkUp,
                                           bool scaleSupportsPowerOff,
                                           bool relayClosed) {
  if (!machinePower.service(service.status(),
                            micraScaleOptions.load(std::memory_order_relaxed),
                            micraAccountConfigured.load(
                                std::memory_order_relaxed))) {
    return;
  }
  if (!scaleLinkUp || relayClosed) {
    serialTraceCategoryf(LogLevel::INFO, DebugCategory::SCALE,
                         "Scale power-off skipped: scale %s",
                         scaleLinkUp ? "busy with an active cycle"
                                     : "not connected");
    return;
  }
  if (!scaleSupportsPowerOff) {
    serialTraceCategoryf(
        LogLevel::WARNING, DebugCategory::SCALE,
        "Scale power-off skipped: connected scale does not support power-off");
    return;
  }
  requestScalePowerOff();
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
