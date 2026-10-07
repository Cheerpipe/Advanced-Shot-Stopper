#pragma once

#include "ShotStopperLineaMicraTypes.h"
#include "ShotStopperPsram.h"
#include "ShotStopperMicraWebSocket.h"

namespace shotstopper {

bool queueMachineIntegrationConnect(uint32_t requestId, const char *username,
                                    const char *password);
bool queueMachineIntegrationRequest(const LineaMicraRequest &request);
bool selectMachineIntegrationDevice(const char *serial,
                                    LineaMicraPersistedSettings &settings);
void clearMachineIntegrationDiscovery();
LineaMicraStatus machineIntegrationStatus();
LineaMicraCloudCall machineIntegrationCloudCall();
MicraWebSocketStatus machineIntegrationWebSocketStatus(bool includeTraffic = true);
HeapLifecycleAggregate machineIntegrationHeapTelemetry();
LineaMicraDiscoverySnapshot machineIntegrationDiscovery();

}  // namespace shotstopper
