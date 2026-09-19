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

static_assert(sizeof(LineaMicraRequest) <= 24,
              "Linea Micra request must stay compact");
static_assert(sizeof(LineaMicraStatus) <= 32,
              "Linea Micra status must stay compact");
static_assert(std::is_trivially_copyable<LineaMicraRequest>::value,
              "Linea Micra worker requests cross a byte-copy boundary");
static_assert(std::is_trivially_copyable<LineaMicraStatus>::value,
              "Linea Micra status crosses a byte-copy boundary");

}  // namespace shotstopper
