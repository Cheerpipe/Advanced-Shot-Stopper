// Fixed preset storage contract, included inside the domain namespace.
struct ShotPreset {
  uint8_t id = 0;
  char name[SHOT_PRESET_NAME_CAPACITY] = {};
  bool isFactory = false;
  bool brewByWeight = true;
  uint8_t goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  uint32_t bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  float weightOffsetBaselineG = DEFAULT_WEIGHT_OFFSET_G;
  float weightOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  bool fastExtractionGuardEnabled = true;
  // V9 names former padding; old schemas initialize it explicitly.
  uint8_t bbwAlgorithm = static_cast<uint8_t>(BbwAlgorithm::LINEAR_EWMA);
  float maxRecoveryWeightG = DEFAULT_MAX_RECOVERY_WEIGHT_G;
  uint32_t minBbwBrewTimeMs = DEFAULT_MIN_BBW_BREW_TIME_MS;
  bool slowExtractionGuardEnabled = true;
  float minRecoveryWeightG = DEFAULT_MIN_RECOVERY_WEIGHT_G;
  uint32_t maxBbwBrewTimeMs = DEFAULT_MAX_BBW_BREW_TIME_MS;
  bool autoToManualGuardEnabled = true;
  uint8_t autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  uint32_t autoToManualGuardManualLimitMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS;
  uint32_t autoToManualGuardBaselineMs =
      DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS;
  bool cupProtectionEnabled = true;
  bool stopIfCupRemoved = true;
  bool requireCupToStart = false;
  bool avoidAccidentalTouchEnabled = true;
  // V9 reuses the obsolete per-preset cup fields; cup mass is machine-owned.
  float bbwEwmaOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  uint8_t bbwEwmaAlpha = DEFAULT_BBW_EWMA_ALPHA;
  uint8_t bbwAlphaLearned = 0;
  uint8_t bbwProfileVersion = BBW_PROFILE_VERSION;
  uint8_t bbwAlphaBaseline = DEFAULT_BBW_EWMA_ALPHA;  // V10, hundredths.
  uint16_t autoToManualGuardSamplesDs[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS,
      AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS};
};

struct ShotPresetBank {
  uint8_t count = 0;
  uint8_t activeId = 0;
  uint8_t nextId = 3;
  ShotPreset presets[MAX_SHOT_PRESETS] = {};
};

static_assert(sizeof(ShotPreset) <= 136, "ShotPreset too large");
static_assert(sizeof(ShotPresetBank) <= 1100, "ShotPresetBank too large");
static_assert(offsetof(RuntimeConfig, bbwAlgorithm) == 251, "V9 runtime layout");
static_assert(offsetof(ShotPreset, bbwAlgorithm) == 45, "V9 preset selector");
static_assert(offsetof(ShotPreset, bbwEwmaOffsetG) == 84, "V9 preset offset");
static_assert(offsetof(ShotPreset, bbwEwmaAlpha) == 88, "V9 preset alpha");
static_assert(offsetof(ShotPreset, bbwAlphaBaseline) == 91, "V10 preset alpha baseline");
static_assert(sizeof(ShotPreset) == 104, "V9 preserves preset size");
