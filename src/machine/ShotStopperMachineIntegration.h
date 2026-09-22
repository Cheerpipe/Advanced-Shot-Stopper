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
inline void serviceMachineIntegrationScaleLink(uint32_t, bool, uint32_t,
                                               uint8_t, bool) {}
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
// Forwards one scale link observation per control loop so the machine
// integration can apply its own scale power-off policy. The stopper stays a
// coordinator and owns no machine logic.
void serviceMachineIntegrationScaleLink(uint32_t now, bool scaleLinkUp,
                                        uint32_t scaleDisconnectSequence,
                                        uint8_t scaleDisconnectReason,
                                        bool relayClosed);
void requestMachineIntegrationPresetTemperature(
    uint8_t presetId, uint32_t configGeneration, uint16_t targetDeciC);
void serviceMachineIntegrationAbort();
uint8_t machineIntegrationTaskCount();
MachinePhysicalStartDisposition machineIntegrationPhysicalStart();
#endif

}  // namespace shotstopper
