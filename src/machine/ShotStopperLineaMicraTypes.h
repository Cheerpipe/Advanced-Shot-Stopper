#pragma once

#include "ShotStopperLineaMicraSettings.h"

#include <cstdint>
#include <type_traits>

namespace shotstopper {

enum class LineaMicraRequestType : uint8_t {
  TEST,
  RECONCILE_TARGET,
  OBSERVE_STATE
};
enum class LineaMicraRequestReason : uint8_t {
  USER,
  BOOT,
  PRESET,
  SCALE_READY,
  PERIODIC,
  POST_ACTIVITY
};
enum class LineaMicraPowerState : uint8_t { UNKNOWN, ON, OFF };
enum class LineaMicraObservedMode : uint8_t {
  NONE,
  STANDBY,
  BREWING,
  ECO,
  UNSUPPORTED
};
enum class LineaMicraObservationQuality : uint8_t {
  Disabled,
  UNCONFIGURED,
  CURRENT,
  STALE,
  COMMUNICATION_ERROR,
  UNSUPPORTED
};
enum class LineaMicraPhase : uint8_t {
  Disabled,
  IDLE,
  QUEUED,
  RUNNING,
  BACKOFF,
  CONFIRMED,
  FAILED,
  CANCELED
};

inline const char *lineaMicraPowerStateName(LineaMicraPowerState state) {
  switch (state) {
    case LineaMicraPowerState::ON: return "ON";
    case LineaMicraPowerState::OFF: return "OFF";
    case LineaMicraPowerState::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

inline const char *lineaMicraObservedModeName(LineaMicraObservedMode mode) {
  switch (mode) {
    case LineaMicraObservedMode::STANDBY: return "StandBy";
    case LineaMicraObservedMode::BREWING: return "BrewingMode";
    case LineaMicraObservedMode::ECO: return "EcoMode";
    case LineaMicraObservedMode::UNSUPPORTED: return "unsupported";
    case LineaMicraObservedMode::NONE: return "none";
  }
  return "none";
}

inline const char *lineaMicraObservationQualityName(
    LineaMicraObservationQuality quality) {
  switch (quality) {
    case LineaMicraObservationQuality::Disabled: return "disabled";
    case LineaMicraObservationQuality::UNCONFIGURED: return "unconfigured";
    case LineaMicraObservationQuality::CURRENT: return "current";
    case LineaMicraObservationQuality::STALE: return "stale";
    case LineaMicraObservationQuality::COMMUNICATION_ERROR:
      return "communication_error";
    case LineaMicraObservationQuality::UNSUPPORTED: return "unsupported";
  }
  return "unavailable";
}

inline const char *lineaMicraPhaseName(LineaMicraPhase phase) {
  switch (phase) {
    case LineaMicraPhase::Disabled: return "disabled";
    case LineaMicraPhase::IDLE: return "idle";
    case LineaMicraPhase::QUEUED: return "queued";
    case LineaMicraPhase::RUNNING: return "running";
    case LineaMicraPhase::BACKOFF: return "backoff";
    case LineaMicraPhase::CONFIRMED: return "confirmed";
    case LineaMicraPhase::FAILED: return "failed";
    case LineaMicraPhase::CANCELED: return "canceled";
  }
  return "disabled";
}

struct LineaMicraRequest {
  uint32_t requestId = 0;
  uint32_t configGeneration = 0;
  uint32_t activityGeneration = 0;
  uint16_t brewTargetDeciC = LINEA_MICRA_BREW_TARGET_DEFAULT_DECI_C;
  uint8_t presetId = 0;
  LineaMicraRequestType type = LineaMicraRequestType::TEST;
  LineaMicraRequestReason reason = LineaMicraRequestReason::USER;
};

struct LineaMicraStatus {
  uint32_t requestId = 0;
  uint32_t sampleAtMs = 0;
  uint32_t temperatureAtMs = 0;
  uint32_t configGeneration = 0;
  uint32_t activityGeneration = 0;
  uint16_t measuredDeciC = 0;
  uint16_t targetDeciC = 0;
  LineaMicraPhase phase = LineaMicraPhase::Disabled;
  LineaMicraPowerState powerState = LineaMicraPowerState::UNKNOWN;
  LineaMicraObservedMode observedMode = LineaMicraObservedMode::NONE;
  LineaMicraObservationQuality quality = LineaMicraObservationQuality::Disabled;
  bool effectiveOn = true;
  bool measuredValid = false;
  bool targetValid = false;
};

struct LineaMicraBindingResult {
  uint32_t requestId = 0;
  uint32_t configGeneration = 0;
  uint8_t address[6] = {};
  char identity[LINEA_MICRA_IDENTITY_CAPACITY] = {};
  uint8_t addressType = 0;
};

static_assert(sizeof(LineaMicraRequest) <= 24,
              "Linea Micra request must stay compact");
static_assert(sizeof(LineaMicraStatus) <= 32,
              "Linea Micra status must stay compact");
static_assert(std::is_trivially_copyable<LineaMicraRequest>::value,
              "Linea Micra worker requests cross a byte-copy boundary");
static_assert(std::is_trivially_copyable<LineaMicraStatus>::value,
              "Linea Micra status crosses a byte-copy boundary");
static_assert(std::is_trivially_copyable<LineaMicraBindingResult>::value,
              "Linea Micra binding result crosses a byte-copy boundary");

}  // namespace shotstopper
