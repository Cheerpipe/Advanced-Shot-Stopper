// Fixed preset storage contract, included inside the domain namespace.
// Settings schema 1. The cut line is the target held through the shot time
// window [fastBeforeMs, slowAfterMs]; each side's margin (decigrams,
// relative to the target) applies only while its switch is on.
struct ShotPreset {
  uint8_t id = 0;
  char name[SHOT_PRESET_NAME_CAPACITY] = {};
  bool isFactory = false;
  bool brewByWeight = true;
  uint16_t goalWeightDg = DEFAULT_GOAL_WEIGHT_DG;
  uint16_t fastMarginDg = DEFAULT_FAST_MARGIN_DG;
  uint16_t slowMarginDg = DEFAULT_SLOW_MARGIN_DG;
  uint16_t doseDg = DEFAULT_DOSE_DG;  // Reference only; never affects the cut.
  uint32_t fastBeforeMs = DEFAULT_FAST_BEFORE_MS;
  uint32_t slowAfterMs = DEFAULT_SLOW_AFTER_MS;
  uint32_t bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  uint32_t operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  uint32_t scaleLossFixedMs = 0;  // 0 = use recent shots or the window.
  uint32_t learningEpoch = 0;     // First ShotLog record id of the statistics.
  float bbwEwmaOffsetG = DEFAULT_WEIGHT_OFFSET_G;
  float weightOffsetBaselineG = DEFAULT_WEIGHT_OFFSET_G;
  bool fastSideEnabled = true;
  bool slowSideEnabled = true;
  bool grindHintsEnabled = true;
  uint8_t bbwEwmaAlpha = DEFAULT_BBW_EWMA_ALPHA;
  uint8_t bbwAlphaLearned = 0;
  uint8_t bbwAlphaBaseline = DEFAULT_BBW_EWMA_ALPHA;  // Hundredths.
  // Deci-Celsius (80.0–100.0 °C), applied only by the Linea Micra integration.
  uint16_t lineaMicraBrewTargetDeciC = 930;
};

struct ShotPresetBank {
  uint8_t count = 0;
  uint8_t activeId = 0;
  uint8_t nextId = FACTORY_PRESET_COUNT + 1;
  ShotPreset presets[MAX_SHOT_PRESETS] = {};
};

static_assert(sizeof(RuntimeConfig) == 312,
              "RuntimeConfig NVS size changed; bump CONFIG_SCHEMA_VERSION");
static_assert(offsetof(RuntimeConfig, touchStopFallbackEnabled) == 17,
              "RuntimeConfig touchStopFallbackEnabled offset changed");
static_assert(sizeof(ShotPreset) <= 136, "ShotPreset too large");
static_assert(sizeof(ShotPresetBank) <= 1100, "ShotPresetBank too large");
static_assert(sizeof(ShotPreset) == 76, "Settings schema 1 preset size");
static_assert(offsetof(ShotPreset, goalWeightDg) == 28, "schema 1 preset goal");
static_assert(offsetof(ShotPreset, fastBeforeMs) == 36, "schema 1 preset window");
static_assert(offsetof(ShotPreset, bbwEwmaOffsetG) == 60, "schema 1 preset offset");
static_assert(offsetof(ShotPreset, lineaMicraBrewTargetDeciC) == 74,
              "schema 1 preset Micra target stays last");
