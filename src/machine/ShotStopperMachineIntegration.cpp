#include "ShotStopperMachineIntegration.h"

namespace shotstopper {

bool initializeMachineIntegration() { return true; }
void serviceMachineIntegrationWorker() {}
uint32_t machineIntegrationMaxExecutionUs() { return 0; }
void publishMachineIntegrationConfig(const PersistedSettings &, uint32_t) {}

}  // namespace shotstopper
