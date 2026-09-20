#include "machine/ShotStopperMachineIntegration.h"

#include <cassert>

int main() {
  using namespace shotstopper;
  assert(machineIntegrationTaskCount() == 0);
  assert(initializeMachineIntegration());
  publishMachineIntegrationNetworkState(true, false, false);
  requestMachineIntegrationPresetTemperature(2, 7, 935);
  serviceMachineIntegrationAbort();
  assert(machineIntegrationPhysicalStart() ==
         MachinePhysicalStartDisposition::NORMAL);
  return 0;
}
