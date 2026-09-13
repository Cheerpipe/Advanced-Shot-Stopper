#pragma once

#include <stdint.h>

#if __has_include("ShotStopperBuildProfileGenerated.h")
#include "ShotStopperBuildProfileGenerated.h"
#endif

#ifndef SHOT_STOPPER_HARDWARE_PROFILE_ID
#define SHOT_STOPPER_HARDWARE_PROFILE_ID "legacy-arch"
#endif
#ifndef SHOT_STOPPER_MACHINE_PROFILE_ID
#define SHOT_STOPPER_MACHINE_PROFILE_ID "legacy-machine"
#endif
#ifndef SHOT_STOPPER_MACHINE_BRAND
#define SHOT_STOPPER_MACHINE_BRAND "Generic"
#endif
#ifndef SHOT_STOPPER_MACHINE_MODEL
#define SHOT_STOPPER_MACHINE_MODEL "Compile-time selection"
#endif
#ifndef SHOT_STOPPER_DEFAULT_RINSE_DURATION_MS
#define SHOT_STOPPER_DEFAULT_RINSE_DURATION_MS 4000
#endif

namespace shotstopper {

constexpr uint32_t DEFAULT_RINSE_DURATION_MS =
    SHOT_STOPPER_DEFAULT_RINSE_DURATION_MS;

}  // namespace shotstopper
