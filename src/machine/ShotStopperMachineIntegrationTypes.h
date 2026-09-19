#pragma once

#include "ShotStopperBuildProfile.h"

#include <cstdint>
#include <type_traits>

namespace shotstopper {

constexpr uint16_t MACHINE_BREW_TARGET_MIN_DECI_C = 800;
constexpr uint16_t MACHINE_BREW_TARGET_MAX_DECI_C = 1000;
constexpr uint16_t MACHINE_BREW_TARGET_DEFAULT_DECI_C = 930;

enum class MachineIntegrationKind : uint8_t { NONE = 0, LINEA_MICRA_BLE = 1 };

struct MachineIntegrationCapabilities {
  MachineIntegrationKind kind = MachineIntegrationKind::NONE;
  bool brewTemperature = false;
  bool operatingStateRead = false;
};

constexpr MachineIntegrationCapabilities compiledMachineIntegrationCapabilities() {
#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE
  return {MachineIntegrationKind::LINEA_MICRA_BLE, true, true};
#else
  return {};
#endif
}

struct MachineRecipeSettings {
  uint16_t brewTargetDeciC = MACHINE_BREW_TARGET_DEFAULT_DECI_C;
};

enum class MachineIntegrationRequestType : uint8_t {
  TEST,
  RECONCILE_TARGET,
  OBSERVE_STATE
};
enum class MachineIntegrationRequestReason : uint8_t {
  USER,
  BOOT,
  PRESET,
  SCALE_READY,
  PERIODIC,
  POST_ACTIVITY
};
enum class MachinePowerState : uint8_t { UNKNOWN, ON, OFF };
enum class MachineObservedMode : uint8_t { NONE, STANDBY, BREWING, ECO, UNSUPPORTED };
enum class MachineObservationQuality : uint8_t {
  Disabled,
  UNCONFIGURED,
  CURRENT,
  STALE,
  COMMUNICATION_ERROR,
  UNSUPPORTED
};
enum class MachineIntegrationPhase : uint8_t {
  Disabled,
  IDLE,
  QUEUED,
  RUNNING,
  BACKOFF,
  CONFIRMED,
  FAILED,
  CANCELED
};

struct MachineIntegrationRequest {
  uint32_t requestId = 0;
  uint32_t configGeneration = 0;
  uint32_t activityGeneration = 0;
  uint16_t brewTargetDeciC = MACHINE_BREW_TARGET_DEFAULT_DECI_C;
  uint8_t presetId = 0;
  MachineIntegrationRequestType type = MachineIntegrationRequestType::TEST;
  MachineIntegrationRequestReason reason = MachineIntegrationRequestReason::USER;
};

struct MachineIntegrationStatus {
  uint32_t requestId = 0;
  uint32_t sampleAtMs = 0;
  uint32_t configGeneration = 0;
  uint32_t activityGeneration = 0;
  uint16_t measuredDeciC = 0;
  uint16_t targetDeciC = 0;
  MachineIntegrationPhase phase = MachineIntegrationPhase::Disabled;
  MachinePowerState powerState = MachinePowerState::UNKNOWN;
  MachineObservedMode observedMode = MachineObservedMode::NONE;
  MachineObservationQuality quality = MachineObservationQuality::Disabled;
  bool effectiveOn = true;
  bool measuredValid = false;
  bool targetValid = false;
};

static_assert(SHOT_STOPPER_MACHINE_INTEGRATION ==
                      SHOT_STOPPER_MACHINE_INTEGRATION_NONE ||
                  SHOT_STOPPER_MACHINE_INTEGRATION ==
                      SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE,
              "unknown compiled machine integration");
static_assert(sizeof(MachineRecipeSettings) == 2, "recipe extension must remain two bytes");
static_assert(sizeof(MachineIntegrationRequest) <= 24, "machine request must stay compact");
static_assert(sizeof(MachineIntegrationStatus) <= 32, "machine status must stay compact");
static_assert(std::is_trivially_copyable<MachineIntegrationRequest>::value,
              "worker requests cross a byte-copy boundary");
static_assert(std::is_trivially_copyable<MachineIntegrationStatus>::value,
              "published status crosses a byte-copy boundary");

}  // namespace shotstopper
