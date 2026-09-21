#include "ShotStopperMachineIntegration.h"

namespace shotstopper {

bool initializeMachineIntegration() { return true; }
void publishMachineIntegrationConfig(const PersistedSettings &, uint32_t) {}
void publishMachineIntegrationNetworkState(bool, bool, bool, bool) {}
void serviceMachineIntegrationScaleLink(uint32_t, bool, uint32_t, uint8_t,
                                        bool) {}
void requestMachineIntegrationPresetTemperature(uint8_t, uint32_t, uint16_t) {}
void serviceMachineIntegrationAbort() {}
uint8_t machineIntegrationTaskCount() { return 0; }
MachinePhysicalStartDisposition machineIntegrationPhysicalStart() {
  return MachinePhysicalStartDisposition::NORMAL;
}

}  // namespace shotstopper
