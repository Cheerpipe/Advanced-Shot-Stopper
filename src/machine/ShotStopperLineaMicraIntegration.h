#pragma once

#include "ShotStopperLineaMicraTypes.h"

namespace shotstopper {

bool queueMachineIntegrationRequest(const LineaMicraRequest &request);
LineaMicraStatus machineIntegrationStatus();
bool takeMachineIntegrationBinding(LineaMicraBindingResult &result);

}  // namespace shotstopper
