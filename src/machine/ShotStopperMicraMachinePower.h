#pragma once

#include "ShotStopperLineaMicraSettings.h"
#include "ShotStopperLineaMicraTypes.h"

namespace shotstopper {

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
