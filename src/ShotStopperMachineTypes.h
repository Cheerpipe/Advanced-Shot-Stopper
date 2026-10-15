#pragma once

#include <stdint.h>

namespace shotstopper {

// =============================================================================
// LAYER: Machine types (shared contracts)
// =============================================================================
// WHAT: Enums and snapshots shared by the machine façade and specializations
//       (UserIntent, MachineRunState, MachineSense).
//
// BOUNDARY: These types are the ONLY machine vocabulary brew/stopper/guards
// may use. They must never branch on paddle vs switch, paddle-mode settings,
// or compiled machine identity. UserIntent is the product of the activator:
// the specialization reads ACTIVATOR_GPIO and translates that signal here.
// MachineSense is a one-way push from the stopper — specializations must not
// reach into session or live scale globals.

// Normal-mode electrical circuit-closed cap. Compatibility mirroring has no
// duration cap; confirmed backflush has its own bound. Brew walls must not exceed this.
constexpr uint32_t HARD_MAX_CIRCUIT_CLOSED_MS = 60000;
constexpr uint32_t BACKFLUSH_MAX_CIRCUIT_CLOSED_MS = 180000;
enum class RelayPurpose : uint8_t { NORMAL, BACKFLUSH_CANDIDATE, BACKFLUSH_CONFIRMED };
#ifndef SHOT_STOPPER_DEFAULT_OPERATIONAL_WALL_MS
#define SHOT_STOPPER_DEFAULT_OPERATIONAL_WALL_MS 50000
#endif
#ifndef SHOT_STOPPER_DEFAULT_RINSE_GESTURE_MS
#define SHOT_STOPPER_DEFAULT_RINSE_GESTURE_MS 1000
#endif
constexpr uint32_t DEFAULT_OPERATIONAL_WALL_MS =
    SHOT_STOPPER_DEFAULT_OPERATIONAL_WALL_MS;
// Default paddle ON→OFF / idle long-press window that publishes REQUEST_RINSE.
constexpr uint32_t DEFAULT_RINSE_GESTURE_MS =
    SHOT_STOPPER_DEFAULT_RINSE_GESTURE_MS;

// Whether water is running through the group (shot or rinse), and whether that
// is confirmed. Not machine power: a powered-on machine is normally IDLE.
// Numeric values are stored in diagnostics snapshots; keep them stable.
enum class MachineRunState : uint8_t {
  IDLE = 0,
  RUNNING_UNCONFIRMED = 1,
  RUNNING = 2,
  UNKNOWN = 3,
  IDLE_UNCONFIRMED = 4
};

// Weight/brew snapshot the stopper pushes into machine each loop. Momentary
// inference must not read session or the live scale globals.
struct MachineSense {
  float weightG = 0.0f;
  bool weightFresh = false;
  bool accidentalHold = false;
  bool brewCycleActive = false;
  // BLE packet generation for this reading. Momentary flow uses this to ignore
  // control-loop resamples of the same packet; paddle does not read it.
  uint32_t weightSequence = 0;
  // Brew has latched first drops this cycle. Momentary uses it to pulse a
  // weight cut even if inferred state already fell back to RUNNING_UNCONFIRMED.
  bool firstDropSeen = false;
  // One control-loop tick: scale just became CONNECTED. Set by the stopper
  // after consuming a BLE-worker pending flag — never mutated on the BLE task.
  bool scaleConnectedEdge = false;
  // One control-loop tick: cup presence emitted REMOVED. Momentary-only uses
  // this to settle IDLE_UNCONFIRMED → IDLE without waiting for quiet pan.
  bool cupRemovedEdge = false;
};

inline bool machinePreferBleAirtime = false;

// Translated activator intention. Not a GPIO level: paddle, switch, or another
// compatible mechanism interprets the pin and publishes one of these values.
enum class UserIntent : uint8_t {
  NONE = 0,
  REQUEST_START = 1,
  REQUEST_STOP = 2,
  HOLD_ACTIVE = 3,
  STABLE_IDLE = 4,
  REQUEST_RINSE = 5
};

inline const char *machineRunStateName(MachineRunState state) {
  switch (state) {
    case MachineRunState::IDLE: return "IDLE";
    case MachineRunState::RUNNING_UNCONFIRMED: return "RUNNING_UNCONFIRMED";
    case MachineRunState::RUNNING: return "RUNNING";
    case MachineRunState::UNKNOWN: return "UNKNOWN";
    case MachineRunState::IDLE_UNCONFIRMED: return "IDLE_UNCONFIRMED";
  }
  return "UNKNOWN";
}

}  // namespace shotstopper
