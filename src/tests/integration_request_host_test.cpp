#include "../ShotStopperIntegrationRequest.h"

#include <cJSON.h>
#include <cstdlib>
#include <iostream>

using namespace shotstopper;

namespace {

int failures = 0;

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: "          \
                << #expression << '\n';                                       \
      ++failures;                                                             \
    }                                                                         \
  } while (false)

bool parse(const char *json, IntegrationQuickSettingRequest &request) {
  cJSON *root = cJSON_Parse(json);
  const bool valid = parseIntegrationQuickSettingRequest(root, request);
  cJSON_Delete(root);
  return valid;
}

void valid_quick_settings_map_exactly() {
  struct Case {
    const char *json;
    QuickSettingField field;
    uint8_t value;
  } cases[] = {
      {R"({"baseRevision":7,"brewByWeight":true})",
       QuickSettingField::BREW_BY_WEIGHT, 1},
      {R"({"baseRevision":7,"noScaleBbwMode":"require_scale"})",
       QuickSettingField::NO_SCALE_BBW_MODE,
       static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE)},
      {R"({"baseRevision":7,"autoToManualGuardEnabled":false})",
       QuickSettingField::AUTO_TO_MANUAL_GUARD, 0},
      {R"({"baseRevision":7,"slowExtractionGuardEnabled":false})",
       QuickSettingField::SLOW_EXTRACTION_GUARD, 0},
      {R"({"baseRevision":7,"fastExtractionGuardEnabled":false})",
       QuickSettingField::FAST_EXTRACTION_GUARD, 0},
      {R"({"baseRevision":7,"avoidAccidentalTouchEnabled":false})",
       QuickSettingField::AVOID_ACCIDENTAL_TOUCH, 0},
      {R"({"baseRevision":7,"cupProtectionEnabled":false})",
       QuickSettingField::CUP_PROTECTION, 0},
  };
  for (const auto &item : cases) {
    IntegrationQuickSettingRequest request;
    CHECK(parse(item.json, request));
    CHECK(request.baseRevision == 7);
    CHECK(request.field == item.field);
    CHECK(request.value == item.value);
  }
}

void malformed_quick_settings_fail_closed() {
  const char *cases[] = {
      "{}",
      R"({"baseRevision":7})",
      R"({"baseRevision":7,"brewByWeight":true,"cupProtectionEnabled":true})",
      R"({"baseRevision":7,"brewByWeight":true,"extra":false})",
      R"({"baseRevision":7,"brewByWeight":true,"brewByWeight":false})",
      R"({"baseRevision":true,"brewByWeight":true})",
      R"({"baseRevision":1.5,"brewByWeight":true})",
      R"({"baseRevision":-1,"brewByWeight":true})",
      R"({"baseRevision":4294967296,"brewByWeight":true})",
      R"({"baseRevision":7,"brewByWeight":1})",
      R"({"baseRevision":7,"noScaleBbwMode":"invalid"})",
      R"({"baseRevision":7,"noScaleBbwMode":false})",
      "[]",
      "null",
  };
  for (const char *json : cases) {
    IntegrationQuickSettingRequest request;
    CHECK(!parse(json, request));
  }
}

void restart_requires_exact_empty_object() {
  const char *cases[] = {"{}", "null", "[]", R"({"extra":true})"};
  for (size_t index = 0; index < 4; ++index) {
    cJSON *root = cJSON_Parse(cases[index]);
    CHECK(parseIntegrationRestartRequest(root) == (index == 0));
    cJSON_Delete(root);
  }
  CHECK(!parseIntegrationRestartRequest(nullptr));
}

void queue_rejection_rolls_back_only_its_staging_slot() {
  uint32_t staged = 17;
  rollbackIntegrationStagedRequest(staged, 16);
  CHECK(staged == 17);
  rollbackIntegrationStagedRequest(staged, 17);
  CHECK(staged == 0);
}

}  // namespace

int main() {
  valid_quick_settings_map_exactly();
  malformed_quick_settings_fail_closed();
  restart_requires_exact_empty_object();
  queue_rejection_rolls_back_only_its_staging_slot();
  std::cout << "integration request parser: " << failures << " failures\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
