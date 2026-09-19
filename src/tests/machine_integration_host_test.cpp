#include "machine/ShotStopperMachineIntegration.h"

#include <cassert>

int main() {
  using namespace shotstopper;
  assert(machineIntegrationTaskCount() == 0);
  assert(initializeMachineIntegration());
  serviceMachineIntegrationWorker();
  assert(machineIntegrationMaxExecutionUs() == 0);
  return 0;
}
