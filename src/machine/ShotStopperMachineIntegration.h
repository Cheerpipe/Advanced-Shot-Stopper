#pragma once

#include <cstdint>

namespace shotstopper {

struct PersistedSettings;

// Called before the shared BLE runtime starts so callback registration can be
// sealed for the host lifetime. The build selects exactly one adapter; feature
// APIs and data types remain private to that machine's module.
#if defined(SHOT_STOPPER_HOST_TEST)
inline bool initializeMachineIntegration() { return true; }
inline void serviceMachineIntegrationWorker() {}
inline uint32_t machineIntegrationMaxExecutionUs() { return 0; }
inline void publishMachineIntegrationConfig(const PersistedSettings &,
                                            uint32_t) {}
#else
bool initializeMachineIntegration();
// Existing BLE worker only. Scale link/command work must run before this step.
void serviceMachineIntegrationWorker();
uint32_t machineIntegrationMaxExecutionUs();
void publishMachineIntegrationConfig(const PersistedSettings &settings,
                                     uint32_t configGeneration);
#endif

inline constexpr uint8_t machineIntegrationTaskCount() { return 0; }

}  // namespace shotstopper
