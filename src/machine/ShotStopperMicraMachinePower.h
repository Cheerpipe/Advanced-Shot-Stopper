#pragma once

#include "ShotStopperLineaMicraSettings.h"
#include "ShotStopperLineaMicraTypes.h"

namespace shotstopper {

// Scale-discovery duty requested from the machine power state. NONE leaves
// the saved BLE scan mode in charge.
enum class MicraScanOverride : uint8_t { NONE, AGGRESSIVE, RELAXED };

// One compact discovery override computed from the persisted scale options
// and the final resolved power state. A live optimistic overlay still leaves
// the last confirmed value in powerState, so the overlay flags replace it;
// together they are the same resolved state Home shows. Only a known ON or
// OFF state can override; UNKNOWN — whatever effectiveOn says — no account,
// observation disabled, unsupported, and failed observations all resolve
// UNKNOWN and keep NONE.
inline MicraScanOverride micraScanOverride(const LineaMicraStatus &machine,
                                           uint8_t micraScaleOptions) {
  LineaMicraPowerState resolved = machine.powerState;
  if (machine.optimisticOn) {
    resolved = LineaMicraPowerState::ON;
  } else if (machine.optimisticOff) {
    resolved = LineaMicraPowerState::OFF;
  }
  if (resolved == LineaMicraPowerState::ON &&
      (micraScaleOptions & LINEA_MICRA_SCALE_SCAN_BOOST_WHEN_ON) != 0) {
    return MicraScanOverride::AGGRESSIVE;
  }
  if (resolved == LineaMicraPowerState::OFF &&
      (micraScaleOptions & LINEA_MICRA_SCALE_SCAN_RELAX_WHEN_OFF) != 0) {
    return MicraScanOverride::RELAXED;
  }
  return MicraScanOverride::NONE;
}

// Machine-side policy for powering the scale off when the machine powers off.
// Only current-quality dashboard classifications change the confirmed state;
// stale, optimistic, unknown, or failed observations never arm or fire the
// trigger. The scale worker owns the actual power-off command and its
// protocol support check.
class MicraMachinePowerTracker {
 public:
  // Returns true exactly once per confirmed ON -> OFF transition observed
  // while the option and account stayed active. A transition observed with
  // the option off is consumed, never deferred.
  bool service(const LineaMicraStatus &machine, uint8_t micraScaleOptions,
               bool accountConfigured) {
    if (machine.quality != LineaMicraObservationQuality::CURRENT) return false;
    if (machine.powerState == LineaMicraPowerState::ON) {
      confirmedOn_ = true;
    } else if (machine.powerState == LineaMicraPowerState::OFF &&
               confirmedOn_) {
      confirmedOn_ = false;
      return accountConfigured &&
             (micraScaleOptions & LINEA_MICRA_SCALE_OFF_WITH_MACHINE) != 0;
    }
    return false;
  }

 private:
  bool confirmedOn_ = false;
};

}  // namespace shotstopper
