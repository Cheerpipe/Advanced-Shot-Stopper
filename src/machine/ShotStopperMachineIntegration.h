#pragma once

#include "ShotStopperMachineIntegrationTypes.h"
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE
#include "ShotStopperPersistedSettings.h"
#endif

#include <cstdint>

namespace shotstopper {

#if SHOT_STOPPER_MACHINE_INTEGRATION != SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE
struct MachineIntegrationPersistedSettings;
#endif

// Called before the shared BLE runtime starts so callback registration can be
// sealed for the host lifetime. None builds remain a zero-work implementation.
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE
bool initializeMachineIntegration();
// Existing BLE worker only. Scale link/command work must run before this step.
void serviceMachineIntegrationWorker();
uint32_t machineIntegrationMaxExecutionUs();
void publishMachineIntegrationConfig(
    const MachineIntegrationPersistedSettings &settings,
    uint32_t configGeneration);
bool queueMachineIntegrationRequest(const MachineIntegrationRequest &request);
MachineIntegrationStatus machineIntegrationStatus();
#else
inline bool initializeMachineIntegration() { return true; }
inline void serviceMachineIntegrationWorker() {}
inline uint32_t machineIntegrationMaxExecutionUs() { return 0; }
inline void publishMachineIntegrationConfig(
    const MachineIntegrationPersistedSettings &, uint32_t) {}
inline bool queueMachineIntegrationRequest(const MachineIntegrationRequest &) {
  return false;
}
inline MachineIntegrationStatus machineIntegrationStatus() { return {}; }
#endif

inline constexpr uint8_t machineIntegrationTaskCount() { return 0; }

}  // namespace shotstopper
