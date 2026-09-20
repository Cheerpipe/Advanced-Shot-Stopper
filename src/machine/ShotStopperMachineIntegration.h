#pragma once

#include <cstdint>

namespace shotstopper {

struct PersistedSettings;

#if defined(SHOT_STOPPER_HOST_TEST)
inline bool initializeMachineIntegration() { return true; }
inline void publishMachineIntegrationConfig(const PersistedSettings &,
                                            uint32_t) {}
inline void publishMachineIntegrationNetworkState(bool, bool, bool) {}
inline void serviceMachineIntegrationAbort() {}
inline constexpr uint8_t machineIntegrationTaskCount() { return 0; }
#else
bool initializeMachineIntegration();
void publishMachineIntegrationConfig(const PersistedSettings &settings,
                                     uint32_t configGeneration);
void publishMachineIntegrationNetworkState(bool staConnected, bool apActive,
                                           bool shotActive);
void serviceMachineIntegrationAbort();
uint8_t machineIntegrationTaskCount();
#endif

}  // namespace shotstopper
