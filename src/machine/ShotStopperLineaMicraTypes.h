#pragma once

#include "ShotStopperLineaMicraSettings.h"

#include <cstdint>
#include <type_traits>

namespace shotstopper {

constexpr size_t LINEA_MICRA_MAX_ACCOUNT_MACHINES = 8;

enum class LineaMicraRequestType : uint8_t {
  CONNECT,
  OBSERVE_STATE,
  APPLY_TEMPERATURE,
  SET_STANDBY
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
  OPTIMISTIC,
  STALE,
  COMMUNICATION_ERROR,
  UNSUPPORTED
};
enum class LineaMicraPhase : uint8_t {
  Disabled,
  IDLE,
  QUEUED,
  PAUSED,
  AUTHENTICATING,
  LISTING,
  RUNNING,
  BACKOFF,
  CONFIRMED,
  FAILED
};
enum class LineaMicraError : uint8_t {
  NONE,
  NO_STA,
  AP_MODE,
  CLOCK_UNSYNCED,
  INVALID_AUTH,
  NO_MACHINES,
  HTTP_ERROR,
  CANCELED,
  REJECTED,
  UNCONFIRMED
};
enum class LineaMicraTemperatureState : uint8_t {
  Disabled,
  IDLE,
  PENDING,
  RUNNING,
  CONFIRMED,
  FAILED,
  CANCELED
};

inline LineaMicraPowerState lineaMicraPowerStateForMode(
    LineaMicraObservedMode mode) {
  if (mode == LineaMicraObservedMode::STANDBY)
    return LineaMicraPowerState::OFF;
  if (mode == LineaMicraObservedMode::BREWING)
    return LineaMicraPowerState::ON;
  return LineaMicraPowerState::UNKNOWN;
}

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
    case LineaMicraObservationQuality::OPTIMISTIC: return "optimistic";
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
    case LineaMicraPhase::PAUSED: return "paused";
    case LineaMicraPhase::AUTHENTICATING: return "authenticating";
    case LineaMicraPhase::LISTING: return "listing";
    case LineaMicraPhase::RUNNING: return "running";
    case LineaMicraPhase::BACKOFF: return "backoff";
    case LineaMicraPhase::CONFIRMED: return "confirmed";
    case LineaMicraPhase::FAILED: return "failed";
  }
  return "disabled";
}

inline const char *lineaMicraErrorName(LineaMicraError error) {
  switch (error) {
    case LineaMicraError::NONE: return "none";
    case LineaMicraError::NO_STA: return "sta_required";
    case LineaMicraError::AP_MODE: return "ap_mode";
    case LineaMicraError::CLOCK_UNSYNCED: return "clock_unsynchronized";
    case LineaMicraError::INVALID_AUTH: return "invalid_auth";
    case LineaMicraError::NO_MACHINES: return "no_machines";
    case LineaMicraError::HTTP_ERROR: return "communication_error";
    case LineaMicraError::CANCELED: return "canceled";
    case LineaMicraError::REJECTED: return "rejected";
    case LineaMicraError::UNCONFIRMED: return "unconfirmed";
  }
  return "unknown";
}

inline const char *lineaMicraTemperatureStateName(
    LineaMicraTemperatureState state) {
  switch (state) {
    case LineaMicraTemperatureState::Disabled: return "disabled";
    case LineaMicraTemperatureState::IDLE: return "idle";
    case LineaMicraTemperatureState::PENDING: return "pending";
    case LineaMicraTemperatureState::RUNNING: return "running";
    case LineaMicraTemperatureState::CONFIRMED: return "confirmed";
    case LineaMicraTemperatureState::FAILED: return "failed";
    case LineaMicraTemperatureState::CANCELED: return "canceled";
  }
  return "disabled";
}

inline bool lineaMicraTemperatureHttpRetryable(uint16_t status) {
  return status < 200 || status == 401 || status == 408 || status == 425 ||
         status == 429 || status >= 500;
}

inline bool lineaMicraTemperatureCycleRetryable(LineaMicraError error) {
  return error == LineaMicraError::HTTP_ERROR ||
         error == LineaMicraError::UNCONFIRMED;
}

struct LineaMicraRequest {
  uint32_t requestId = 0;
  uint32_t configGeneration = 0;
  uint16_t targetDeciC = 0;
  uint8_t presetId = 0;
  LineaMicraRequestType type = LineaMicraRequestType::OBSERVE_STATE;
};

struct LineaMicraMachineSummary {
  char serial[LINEA_MICRA_SERIAL_CAPACITY] = {};
  char name[LINEA_MICRA_NAME_CAPACITY] = {};
};

struct LineaMicraDiscoverySnapshot {
  uint32_t requestId = 0;
  uint32_t generation = 0;
  uint8_t count = 0;
  LineaMicraMachineSummary machines[LINEA_MICRA_MAX_ACCOUNT_MACHINES] = {};
};

struct LineaMicraStatus {
  uint32_t requestId = 0;
  uint32_t sampleAtMs = 0;
  uint32_t temperatureAtMs = 0;
  uint32_t configGeneration = 0;
  uint32_t identityGeneration = 0;
  uint16_t targetDeciC = 0;
  uint16_t requestedTargetDeciC = 0;
  uint16_t appliedTargetDeciC = 0;
  int32_t transportStatus = 0;
  int32_t temperatureTransportStatus = 0;
  uint16_t httpStatus = 0;
  uint16_t temperatureHttpStatus = 0;
  LineaMicraPhase phase = LineaMicraPhase::Disabled;
  LineaMicraError error = LineaMicraError::NONE;
  LineaMicraPowerState powerState = LineaMicraPowerState::UNKNOWN;
  LineaMicraObservedMode observedMode = LineaMicraObservedMode::NONE;
  LineaMicraObservationQuality quality = LineaMicraObservationQuality::Disabled;
  LineaMicraTemperatureState temperatureState =
      LineaMicraTemperatureState::Disabled;
  LineaMicraError temperatureError = LineaMicraError::NONE;
  bool effectiveOn = true;
  bool targetValid = false;
  bool accountConfigured = false;
  bool staConnected = false;
  bool apActive = false;
  bool shotPaused = false;
  bool scalePaused = false;
  bool optimisticOn = false;
  bool temperatureCommandAccepted = false;
  bool temperatureRetryable = false;
};

static_assert(sizeof(LineaMicraRequest) <= 16,
              "Linea Micra request must stay compact");
static_assert(sizeof(LineaMicraStatus) <= 64,
              "Linea Micra status must stay compact");
static_assert(std::is_trivially_copyable<LineaMicraRequest>::value);
static_assert(std::is_trivially_copyable<LineaMicraStatus>::value);
static_assert(std::is_trivially_copyable<LineaMicraDiscoverySnapshot>::value);

}  // namespace shotstopper
