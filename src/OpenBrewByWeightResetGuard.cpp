#include "OpenBrewByWeightResetGuard.h"

#ifndef OPEN_BREW_BY_WEIGHT_HOST_TEST
namespace openbrewbyweight::detail {

RTC_NOINIT_ATTR volatile SafetyResetRecord safetyResetRecord;
uint32_t resetUptimeLastCheckpointMs = 0;

}  // namespace openbrewbyweight::detail
#endif
