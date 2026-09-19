#include "machine/ShotStopperMachineIntegrationTypes.h"
#include "machine/ShotStopperMachineIntegration.h"
#include "machine/ShotStopperMicraTiming.h"

#include <cassert>

int main() {
  using namespace shotstopper;
  const MachineIntegrationCapabilities capabilities =
      compiledMachineIntegrationCapabilities();
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE
  assert(capabilities.kind == MachineIntegrationKind::LINEA_MICRA_BLE);
  assert(capabilities.brewTemperature && capabilities.operatingStateRead);
#else
  assert(capabilities.kind == MachineIntegrationKind::NONE);
  assert(!capabilities.brewTemperature && !capabilities.operatingStateRead);
#endif
  assert(MachineRecipeSettings{}.brewTargetDeciC == 930);
  assert(micra_timing::kMaxAttempts == 4);
  assert(micra_timing::kRetryDelaysMs[0] == 3000);
  assert(micra_timing::kRetryDelaysMs[2] == 9000);
  assert(machineIntegrationTaskCount() == 0);
  assert(initializeMachineIntegration());
  serviceMachineIntegrationWorker();
  assert(machineIntegrationMaxExecutionUs() == 0);
  return 0;
}
