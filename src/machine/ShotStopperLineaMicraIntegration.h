#pragma once

#include "ShotStopperLineaMicraTypes.h"

namespace shotstopper {

bool queueMachineIntegrationConnect(uint32_t requestId, const char *username,
                                    const char *password);
bool queueMachineIntegrationRequest(const LineaMicraRequest &request);
bool selectMachineIntegrationDevice(const char *serial,
                                    LineaMicraPersistedSettings &settings);
void clearMachineIntegrationDiscovery();
LineaMicraStatus machineIntegrationStatus();
LineaMicraDiscoverySnapshot machineIntegrationDiscovery();

}  // namespace shotstopper
