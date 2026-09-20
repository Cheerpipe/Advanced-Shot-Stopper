#pragma once

#include <cstdint>

namespace shotstopper {

struct PersistedSettings;

enum class MachinePhysicalStartDisposition : uint8_t {
  NORMAL,
  WAKE_PASSTHROUGH
};

#if defined(SHOT_STOPPER_HOST_TEST)
inline bool initializeMachineIntegration() { return true; }
inline void publishMachineIntegrationConfig(const PersistedSettings &,
                                            uint32_t) {}
inline void publishMachineIntegrationNetworkState(bool, bool, bool,
                                                  bool = false) {}
inline uint32_t hostMachineTemperatureRequestCount = 0;
inline uint8_t hostMachineTemperaturePresetId = 0;
inline uint32_t hostMachineTemperatureGeneration = 0;
inline uint16_t hostMachineTemperatureTargetDeciC = 0;
inline void requestMachineIntegrationPresetTemperature(
    uint8_t presetId, uint32_t configGeneration, uint16_t targetDeciC) {
  ++hostMachineTemperatureRequestCount;
  hostMachineTemperaturePresetId = presetId;
  hostMachineTemperatureGeneration = configGeneration;
  hostMachineTemperatureTargetDeciC = targetDeciC;
}
inline void serviceMachineIntegrationAbort() {}
inline constexpr uint8_t machineIntegrationTaskCount() { return 0; }
inline MachinePhysicalStartDisposition
    hostMachinePhysicalStartDisposition =
        MachinePhysicalStartDisposition::NORMAL;
inline uint32_t hostMachinePhysicalStartCount = 0;
inline MachinePhysicalStartDisposition machineIntegrationPhysicalStart() {
  ++hostMachinePhysicalStartCount;
  return hostMachinePhysicalStartDisposition;
}
#else
bool initializeMachineIntegration();
void publishMachineIntegrationConfig(const PersistedSettings &settings,
                                     uint32_t configGeneration);
void publishMachineIntegrationNetworkState(bool staConnected, bool apActive,
                                           bool shotActive,
                                           bool scaleConnecting = false);
void requestMachineIntegrationPresetTemperature(
    uint8_t presetId, uint32_t configGeneration, uint16_t targetDeciC);
void serviceMachineIntegrationAbort();
uint8_t machineIntegrationTaskCount();
MachinePhysicalStartDisposition machineIntegrationPhysicalStart();
#endif

}  // namespace shotstopper
