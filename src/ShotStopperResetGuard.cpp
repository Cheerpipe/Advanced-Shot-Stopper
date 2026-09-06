#include "ShotStopperResetGuard.h"

#ifndef SHOT_STOPPER_HOST_TEST
namespace shotstopper::detail {

RTC_NOINIT_ATTR volatile SafetyResetRecord safetyResetRecord;
uint32_t resetUptimeLastCheckpointMs = 0;

}  // namespace shotstopper::detail
#endif
