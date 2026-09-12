#pragma once

#include "ShotStopperDomain.h"

namespace shotstopper {

enum class QuickSettingField : uint8_t {
  BREW_BY_WEIGHT,
  NO_SCALE_BBW_MODE,
  AUTO_TO_MANUAL_GUARD,
  SLOW_EXTRACTION_GUARD,
  FAST_EXTRACTION_GUARD,
  AVOID_ACCIDENTAL_TOUCH,
  CUP_PROTECTION
};

struct QuickSettingsSnapshot {
  uint32_t revision = 0;
  uint8_t activePresetId = 0;
  bool brewByWeight = false;
  uint8_t noScaleBbwMode = static_cast<uint8_t>(NoScaleBbwMode::OFF);
  bool autoToManualGuardEnabled = false;
  bool slowExtractionGuardEnabled = false;
  bool fastExtractionGuardEnabled = false;
  bool avoidAccidentalTouchEnabled = false;
  bool cupProtectionEnabled = false;
};

inline QuickSettingsSnapshot quickSettingsSnapshot(const RecipeSnapshot &recipe) {
  QuickSettingsSnapshot snapshot;
  snapshot.revision = recipe.runtime.revision;
  snapshot.activePresetId = recipe.presets.activeId;
  snapshot.brewByWeight = !recipe.runtime.timerOnly;
  snapshot.noScaleBbwMode = recipe.runtime.noScaleBbwMode;
  snapshot.autoToManualGuardEnabled = recipe.runtime.autoToManualGuardEnabled;
  snapshot.slowExtractionGuardEnabled = recipe.runtime.slowExtractionGuardEnabled;
  snapshot.fastExtractionGuardEnabled = recipe.runtime.fastExtractionGuardEnabled;
  snapshot.avoidAccidentalTouchEnabled =
      recipe.runtime.avoidAccidentalTouchEnabled;
  snapshot.cupProtectionEnabled = recipe.runtime.cupProtectionEnabled;
  return snapshot;
}

inline bool quickSettingsEqual(const QuickSettingsSnapshot &left,
                               const QuickSettingsSnapshot &right) {
  return left.activePresetId == right.activePresetId &&
         left.brewByWeight == right.brewByWeight &&
         left.noScaleBbwMode == right.noScaleBbwMode &&
         left.autoToManualGuardEnabled == right.autoToManualGuardEnabled &&
         left.slowExtractionGuardEnabled == right.slowExtractionGuardEnabled &&
         left.fastExtractionGuardEnabled == right.fastExtractionGuardEnabled &&
         left.avoidAccidentalTouchEnabled == right.avoidAccidentalTouchEnabled &&
         left.cupProtectionEnabled == right.cupProtectionEnabled;
}

}  // namespace shotstopper
