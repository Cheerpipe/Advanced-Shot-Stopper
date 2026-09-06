#define SHOT_STOPPER_HOST_TEST

#include <stdint.h>

extern uint32_t hostSafetyResetReasonCode;
extern bool hostSafetyResetReasonUnsafe;
extern bool hostSafetyResetReasonPowerOn;

#include "../ShotStopperResetGuard.h"

const volatile void *resetGuardAddressB() {
  return &shotstopper::safetyResetRecordForHost();
}

bool resetGuardReadFromB() {
  return shotstopper::safetyResetRecordValid() &&
         shotstopper::safetyResetRecordForHost().unsafeResetCount == 7;
}
