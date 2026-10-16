#pragma once

#include "ShotStopperIntegrationState.h"

#include <cJSON.h>
#include <cmath>
#include <cstring>

namespace shotstopper {

struct IntegrationQuickSettingRequest {
  uint32_t baseRevision = 0;
  QuickSettingField field = QuickSettingField::BREW_BY_WEIGHT;
  uint8_t value = 0;
};

inline bool parseIntegrationQuickSettingRequest(
    const cJSON *root, IntegrationQuickSettingRequest &output) {
  if (!cJSON_IsObject(root) || cJSON_GetArraySize(root) != 2) return false;
  const cJSON *revision = cJSON_GetObjectItemCaseSensitive(root, "baseRevision");
  if (!cJSON_IsNumber(revision) || !std::isfinite(revision->valuedouble) ||
      revision->valuedouble < 0 || revision->valuedouble > UINT32_MAX ||
      std::floor(revision->valuedouble) != revision->valuedouble) return false;

  uint8_t selected = 0;
  for (const cJSON *item = root->child; item != nullptr; item = item->next) {
    if (item->string == nullptr || strcmp(item->string, "baseRevision") == 0)
      continue;
    struct BooleanField {
      const char *name;
      QuickSettingField field;
    };
    static constexpr BooleanField fields[] = {
        {"brewByWeight", QuickSettingField::BREW_BY_WEIGHT},
        {"autoToManualGuardEnabled", QuickSettingField::AUTO_TO_MANUAL_GUARD},
        {"slowExtractionGuardEnabled", QuickSettingField::SLOW_EXTRACTION_GUARD},
        {"fastExtractionGuardEnabled", QuickSettingField::FAST_EXTRACTION_GUARD},
        {"avoidAccidentalTouchEnabled", QuickSettingField::AVOID_ACCIDENTAL_TOUCH},
        {"cupProtectionEnabled", QuickSettingField::CUP_PROTECTION},
    };
    bool matched = false;
    for (const auto &candidate : fields) {
      if (strcmp(item->string, candidate.name) != 0) continue;
      if (!cJSON_IsBool(item)) return false;
      output.field = candidate.field;
      output.value = cJSON_IsTrue(item) ? 1 : 0;
      matched = true;
      break;
    }
    if (!matched && strcmp(item->string, "noScaleBbwMode") == 0) {
      if (!cJSON_IsString(item) || item->valuestring == nullptr) return false;
      output.field = QuickSettingField::NO_SCALE_BBW_MODE;
      if (strcmp(item->valuestring, "off") == 0) {
        output.value = static_cast<uint8_t>(NoScaleBbwMode::OFF);
      } else if (strcmp(item->valuestring, "warn_once") == 0) {
        output.value = static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE);
      } else if (strcmp(item->valuestring, "require_scale") == 0) {
        output.value = static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
      } else {
        return false;
      }
      matched = true;
    }
    if (!matched || ++selected != 1) return false;
  }
  output.baseRevision = static_cast<uint32_t>(revision->valuedouble);
  return selected == 1;
}

inline bool parseIntegrationRestartRequest(const cJSON *root) {
  return cJSON_IsObject(root) && cJSON_GetArraySize(root) == 0;
}

inline void rollbackIntegrationStagedRequest(uint32_t &stagedRequestId,
                                             uint32_t rejectedRequestId) {
  if (stagedRequestId == rejectedRequestId) stagedRequestId = 0;
}

}  // namespace shotstopper
