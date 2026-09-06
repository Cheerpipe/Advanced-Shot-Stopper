#define SHOT_STOPPER_HOST_TEST

#include <stdint.h>

extern uint32_t hostSafetyResetReasonCode;
extern bool hostSafetyResetReasonUnsafe;
extern bool hostSafetyResetReasonPowerOn;

#include "../ShotStopperResetGuard.h"

const volatile void *resetGuardAddressA() {
  return &shotstopper::safetyResetRecordForHost();
}

void resetGuardWriteFromA() {
  shotstopper::initializeSafetyResetRecord(
      shotstopper::SAFETY_RELAY_OPEN_MARKER, 7);
}
