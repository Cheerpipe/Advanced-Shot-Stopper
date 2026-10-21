#pragma once

#include "OpenBrewByWeightLineaMicraTypes.h"
#include "OpenBrewByWeightPsram.h"
#include "OpenBrewByWeightMicraWebSocket.h"

namespace openbrewbyweight {

bool queueMachineIntegrationConnect(uint32_t requestId, const char *username,
                                    const char *password);
bool queueMachineIntegrationRelist(uint32_t requestId);
bool queueMachineIntegrationRequest(const LineaMicraRequest &request);
bool selectMachineIntegrationDevice(const char *serial,
                                    LineaMicraPersistedSettings &settings);
void clearMachineIntegrationDiscovery();
LineaMicraStatus machineIntegrationStatus();
LineaMicraCloudCall machineIntegrationCloudCall();
MicraWebSocketStatus machineIntegrationWebSocketStatus(bool includeTraffic = true);
HeapLifecycleAggregate machineIntegrationHeapTelemetry();
LineaMicraDiscoverySnapshot machineIntegrationDiscovery();

}  // namespace openbrewbyweight
