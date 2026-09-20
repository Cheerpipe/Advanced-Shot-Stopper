#include "machine/ShotStopperMachineIntegration.h"

#include <cassert>

int main() {
  using namespace shotstopper;
  assert(machineIntegrationTaskCount() == 0);
  assert(initializeMachineIntegration());
  publishMachineIntegrationNetworkState(true, false, false);
  serviceMachineIntegrationAbort();
  return 0;
}
