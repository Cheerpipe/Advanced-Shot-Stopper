#include "ShotStopperNetwork.h"
#include "ShotStopperDebugExport.h"
#include "ShotStopperMachineMomentaryConfig.h"
#include "ShotStopperMachinePaddleConfig.h"
#include "ShotStopperJsonArena.h"
#include "ShotStopperOta.h"
#include "ShotStopperPsram.h"
#include "ShotStopperRecovery.h"
#include "ShotStopperResetGuard.h"
#include "ShotStopperSerialCli.h"
#include "ShotStopperShotCurveTypes.h"
#include "ShotStopperVersion.h"
#include "ShotStopperWatchdog.h"

#include "ShotStopperWebAssetsGzip.h"

#include <Arduino.h>
#include <cJSON.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include "ShotStopperRfCoex.h"
#include <math.h>
#include <new>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

// The application logger is owned by shotStopper.cpp. Network diagnostics use
// it so they reach both the ESP-IDF serial backend and the optional RAM ring.
void serialTraceCategory(shotstopper::LogLevel level,
                         shotstopper::DebugCategory category,
                         const char *message);
void serialTraceCategoryf(shotstopper::LogLevel level,
                          shotstopper::DebugCategory category,
                          const char *fmt, ...);

namespace shotstopper {

WallClock g_wallClock;

struct NetworkWorkBuf {
  static constexpr size_t kStatusJson = 12288;
  static constexpr size_t kPresetsJson = 2800;
  static constexpr size_t kHistoryJson = 1400;
  static constexpr size_t kJsonItem = 1800;
  // Includes resumable-session identity (transfer id + SHA-256) as well as
  // two image tags. This buffer is in the shared external work area, never
  // used by the flash-writing path.
  static constexpr size_t kOtaJson = 1664;
  char statusJson[kStatusJson]{};
  char presetsJson[kPresetsJson]{};
  char historyJson[kHistoryJson]{};
  char jsonItem[kJsonItem]{};
  char otaJson[kOtaJson]{};
  DebugEvent logBatch[kNetworkLogBatchSize]{};
  ShotLogRecord shotRecords[SHOT_LOG_CAPACITY]{};
  ShotCurveRecord shotCurves[SHOT_CURVE_CAPACITY]{};
  ControlStatusSnapshot control{};
  TaskProfilerSnapshot taskProfiler{};
  DebugExportExtras debugExport{};
  ShotPresetBank presetBank{};
  ScaleHistoryEntry scaleHistory[SCALE_HISTORY_CAPACITY]{};
  BullseyeMelodyConfig bullseyeMelody{};
  char safeBullseyeRtttl[BULLSEYE_RTTTL_CAPACITY]{};
  // Must match ShotStopperNetwork::REQUEST_BODY_CAPACITY (asserted in begin()).
  char requestBody[2048]{};
  WifiScanSnapshot wifiScan{};
};

// Wi-Fi scan snapshots. Network task / httpd only; not BLE.
// AP records come from Arduino's SCAN_DONE cache (getScanInfoByIndex).
static SHOT_STOPPER_PSRAM_BSS WifiScanSnapshot g_wifiScan;
static SHOT_STOPPER_PSRAM_BSS WifiScanSnapshot g_wifiScanWorking;
static SHOT_STOPPER_PSRAM_BSS PersistedSettings g_networkSettings;

namespace {

NetworkWorkBuf *g_work = nullptr;

void quietIdfWifiDriverWarnings() {
  // Home APs often advertise 802.11r (FT-PSK). STA leaves FT off on purpose:
  // the machine does not roam, and enabling 11r has broken IoT joins on some
  // mesh/UniFi setups. The closed-source driver then WARNs on every matching
  // BSS and falls back to WPA2-PSK. Association still succeeds; Shot Stopper
  // logs join/fail itself. Drop the wifi tag to ERROR so that expected
  // fallback does not flood USB serial. Other IDF tags stay at WARN.
  esp_log_level_set("wifi", ESP_LOG_ERROR);
}

LogLevel networkTextLogLevel(const char *message) {
  if (message == nullptr) {
    return LogLevel::INFO;
  }
  if (strncmp(message, "ERR ", 4) == 0) {
    return LogLevel::ERROR;
  }
  if (strstr(message, " failed") != nullptr ||
      strstr(message, " mismatch") != nullptr ||
      strstr(message, "timed out") != nullptr ||
      strstr(message, "rollback impossible") != nullptr ||
      strstr(message, "rolling back") != nullptr) {
    return LogLevel::WARNING;
  }
  return LogLevel::INFO;
}

void formatWifiMac(const uint8_t mac[6], char *output, size_t outputCapacity) {
  if (mac == nullptr || output == nullptr || outputCapacity < 18) {
    return;
  }
  snprintf(output, outputCapacity, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0],
           mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void parseShotsPageQuery(httpd_req_t *request, size_t &offset, size_t &limit,
                         ShotLogSort &sort, ShotLogSortDir &dir) {
  offset = 0;
  limit = SHOT_LOG_PAGE_DEFAULT;
  sort = ShotLogSort::Date;
  dir = ShotLogSortDir::Desc;
  if (request == nullptr) {
    return;
  }
  const size_t queryLength = httpd_req_get_url_query_len(request);
  if (queryLength == 0 || queryLength >= 80) {
    return;
  }
  char query[80] = {};
  if (httpd_req_get_url_query_str(request, query, sizeof(query)) != ESP_OK) {
    return;
  }
  char value[16] = {};
  if (httpd_query_key_value(query, "offset", value, sizeof(value)) == ESP_OK) {
    char *end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end != value && *end == '\0') {
      offset = static_cast<size_t>(parsed);
    }
  }
  if (httpd_query_key_value(query, "limit", value, sizeof(value)) == ESP_OK) {
    char *end = nullptr;
    const unsigned long parsed = strtoul(value, &end, 10);
    if (end != value && *end == '\0') {
      limit = shotLogClampPageLimit(static_cast<size_t>(parsed));
    }
  }
  memset(value, 0, sizeof(value));
  if (httpd_query_key_value(query, "sort", value, sizeof(value)) == ESP_OK) {
    sort = shotLogSortFromName(value);
  }
  memset(value, 0, sizeof(value));
  if (httpd_query_key_value(query, "dir", value, sizeof(value)) == ESP_OK) {
    dir = shotLogSortDirFromName(value);
  }
}

const char *jsonParseFailureMessage(const char *fallback) {
  if (jsonArenaExhaustedRecently()) {
    return "JSON too large for device buffer";
  }
  return fallback;
}

constexpr const char *AP_SSID = "AdvancedShotStopperAP";
constexpr const char *AP_IP = "192.168.4.1";
constexpr const char *JSON_CONTENT_TYPE = "application/json";
constexpr const char *STATUS_OK = "200 OK";
constexpr const char *STATUS_NO_CONTENT = "204 No Content";
constexpr const char *STATUS_NOT_MODIFIED = "304 Not Modified";
constexpr const char *STATUS_ACCEPTED = "202 Accepted";
constexpr const char *STATUS_ALREADY_REPORTED = "208 Already Reported";
constexpr size_t IF_NONE_MATCH_CAPACITY = 80;
constexpr const char *STATUS_BAD_REQUEST = "400 Bad Request";
constexpr const char *STATUS_UNAUTHORIZED = "401 Unauthorized";
constexpr const char *STATUS_FORBIDDEN = "403 Forbidden";
constexpr const char *STATUS_TOO_LARGE = "413 Content Too Large";
constexpr const char *STATUS_SERVER_ERROR = "500 Internal Server Error";
constexpr const char *DEVICE_PASSWORD_HEADER = "X-Device-Password";
// Legacy header name kept so older CLI scripts still authenticate.
constexpr const char *DEVICE_PASSWORD_HEADER_LEGACY = "X-OTA-Token";
constexpr const char *OTA_ALLOW_DOWNGRADE_HEADER = "X-OTA-Allow-Downgrade";
constexpr const char *OTA_TRANSFER_HEADER = "X-OTA-Transfer";
constexpr const char *OTA_OFFSET_HEADER = "X-OTA-Offset";
constexpr const char *OTA_LENGTH_HEADER = "X-OTA-Length";
constexpr size_t OTA_STATUS_JSON_CAPACITY = NetworkWorkBuf::kOtaJson;
constexpr const char *STATUS_TOO_MANY = "429 Too Many Requests";
constexpr const char *STATUS_CONFLICT = "409 Conflict";
constexpr const char *STATUS_NOT_FOUND = "404 Not Found";
constexpr const char *STATUS_UNPROCESSABLE = "422 Unprocessable Entity";
constexpr const char *STATUS_UNAVAILABLE = "503 Service Unavailable";
// PersistedSettings is ≤ PERSISTED_SETTINGS_NVS_BUDGET (3072 B).
// processPersistedCommand() keeps one copy on the stack via settingsCopy();
// NVS dual-slot scratch is shared off-stack. 7 168 was too small (canary on
// FACTORY_RESET). 10 240 keeps headroom without the old 12 KiB margin.
// Keep this stack in internal RAM: the task writes NVS (flash cache disabled).
constexpr uint32_t NETWORK_MANAGER_TASK_STACK_SIZE = 10240;
// POST JSON bodies live in NetworkWorkBuf (PSRAM), so the httpd worker no
// longer needs a 2 KiB request-body frame on top of headers and send buffers.
// Stack stays internal: OTA flash writes run on this task.
constexpr uint32_t HTTP_SERVER_TASK_STACK_SIZE = 8192;

const char *scaleDisconnectReasonName(uint8_t reason) {
  // Mirrors ScaleDisconnectReason without coupling the network task to the
  // single-owner BLE implementation.
  switch (reason) {
    case 0: return "NONE";
    case 1: return "USER_REQUEST";
    case 2: return "SCAN_START_FAILED";
    case 3: return "SCAN_TIMEOUT";
    case 4: return "CONNECT_FAILED";
    case 5: return "DISCOVERY_FAILED";
    case 6: return "UNSUPPORTED_SCALE";
    case 7: return "SUBSCRIBE_FAILED";
    case 8: return "INITIALIZATION_WRITE_FAILED";
    case 9: return "REMOTE_DISCONNECTED";
    case 10: return "FIRST_PACKET_TIMEOUT";
    case 11: return "PACKET_TIMEOUT";
    case 12: return "INVALID_PACKET_STREAM";
    case 13: return "COMMAND_WRITE_FAILED";
    case 14: return "SUPERVISION_TIMEOUT";
    case 15: return "CONNECTION_FAILED_TO_ESTABLISH";
    case 16: return "RX_QUEUE_OVERFLOW";
    case 17: return "EVENT_QUEUE_OVERFLOW";
    case 18: return "HOST_RESET";
    case 19: return "OPERATION_TIMEOUT";
    case 20: return "MBUF_ALLOCATION_FAILED";
  }
  return "UNKNOWN";
}

bool jsonFieldPresent(cJSON *object, const char *name) {
  return object != nullptr && name != nullptr &&
         cJSON_GetObjectItemCaseSensitive(object, name) != nullptr;
}

bool jsonBoolean(cJSON *object, const char *name, bool &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsBool(item)) {
    return false;
  }
  output = cJSON_IsTrue(item);
  return true;
}

bool jsonUint32(cJSON *object, const char *name, uint32_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble) ||
      item->valuedouble < 0.0 || item->valuedouble > UINT32_MAX ||
      floor(item->valuedouble) != item->valuedouble) {
    return false;
  }
  output = static_cast<uint32_t>(item->valuedouble);
  return true;
}

bool jsonUint8(cJSON *object, const char *name, uint8_t &output) {
  uint32_t value = 0;
  if (!jsonUint32(object, name, value) || value > UINT8_MAX) {
    return false;
  }
  output = static_cast<uint8_t>(value);
  return true;
}

bool jsonFloat(cJSON *object, const char *name, float &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item) || !isfinite(item->valuedouble)) {
    return false;
  }
  output = static_cast<float>(item->valuedouble);
  return true;
}

bool jsonInt16(cJSON *object, const char *name, int16_t &output) {
  if (object == nullptr || name == nullptr) {
    return false;
  }
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsNumber(item) || item->valuedouble != item->valueint) {
    return false;
  }
  const long value = item->valueint;
  if (value < INT16_MIN || value > INT16_MAX) {
    return false;
  }
  output = static_cast<int16_t>(value);
  return true;
}

// Compared in constant time so a wrong secret cannot be recovered one byte at a
// time by measuring how long the rejection takes.
bool secretsMatch(const char *candidate, const char *expected) {
  const size_t candidateLength = strnlen(candidate, WIFI_PASSWORD_CAPACITY);
  const size_t expectedLength = strnlen(expected, WIFI_PASSWORD_CAPACITY);
  uint32_t difference =
      static_cast<uint32_t>(candidateLength ^ expectedLength);
  const size_t span =
      candidateLength < expectedLength ? candidateLength : expectedLength;
  for (size_t index = 0; index < span; ++index) {
    difference |= static_cast<uint32_t>(
        static_cast<uint8_t>(candidate[index]) ^
        static_cast<uint8_t>(expected[index]));
  }
  return difference == 0 && expectedLength > 0;
}

bool jsonString(cJSON *object, const char *name, char *output,
                size_t outputCapacity, bool allowEmpty) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  const size_t length = strlen(item->valuestring);
  if ((!allowEmpty && length == 0) || length >= outputCapacity) {
    return false;
  }
  memset(output, 0, outputCapacity);
  memcpy(output, item->valuestring, length);
  return true;
}

bool jsonHasOnlyUniqueFields(cJSON *object, const char *const *allowed,
                             size_t allowedCount) {
  // Two fixed words cover the broad config patch without heap allocation.
  // Keep the ceiling explicit so callers fail loudly if they outgrow it.
  constexpr size_t kSeenWords = 2;
  if (!cJSON_IsObject(object) || allowed == nullptr ||
      allowedCount > kSeenWords * 64U) {
    return false;
  }
  uint64_t seen[kSeenWords] = {};
  for (cJSON *item = object->child; item != nullptr; item = item->next) {
    if (item->string == nullptr) {
      return false;
    }
    size_t index = 0;
    while (index < allowedCount && strcmp(item->string, allowed[index]) != 0) {
      ++index;
    }
    if (index == allowedCount) {
      return false;
    }
    const size_t word = index / 64U;
    const uint64_t bit = uint64_t{1} << (index % 64U);
    if ((seen[word] & bit) != 0) {
      return false;
    }
    seen[word] |= bit;
  }
  return true;
}

const char *configValidationMessage(ConfigValidationError error) {
  switch (error) {
    case ConfigValidationError::NONE:
      return "Configuration is valid.";
    case ConfigValidationError::GOAL_WEIGHT:
      return "Target must be an integer from 10 to 200 g.";
    case ConfigValidationError::WEIGHT_OFFSET:
      return "Internal offset is invalid; it cannot be edited from the Web UI.";
    case ConfigValidationError::RINSE_GESTURE:
      return "Rinse gesture must be from 0.1 to 5 s.";
    case ConfigValidationError::RINSE_DURATION:
      return "Rinse duration must be from 0.5 to 10 s.";
    case ConfigValidationError::RETARE_WINDOW:
      return "Retare window must be from 0.5 to 10 s.";
    case ConfigValidationError::MINIMUM_CUP_WEIGHT:
      return "Minimum cup weight must be from 1 to 500 g.";
    case ConfigValidationError::RETARE_STABILITY_SAMPLES:
      return "Retare stable samples must be from 2 to 10.";
    case ConfigValidationError::RETARE_STABILITY_TOLERANCE:
      return "Retare stability tolerance must be from 0.1 to 20 g.";
    case ConfigValidationError::RETARE_STABILITY_MAX_GAP:
      return "Retare sample gap must be from 0.1 to 5 s.";
    case ConfigValidationError::RETARE_STABILITY_MIN_DURATION:
      return "Retare min stable time must be from 0 to 2 s.";
    case ConfigValidationError::RETARE_STABILITY_RELATION:
      return "Retare min stable time must be ≤ retare window and ≤ "
             "samples × sample gap.";
    case ConfigValidationError::BBW_PROTECTION_TIMEOUT:
      return "BBW protection must be from 0.5 to 30 s.";
    case ConfigValidationError::BBW_PROTECTION_RETARE_RELATION:
      return "BBW protection must be at least effective retare "
             "window + 3 s.";
    case ConfigValidationError::OPERATIONAL_WALL:
      return "Max BBW time must be from 5 to 60 s.";
    case ConfigValidationError::PADDLE_REMINDER_INTERVAL:
      return "Paddle reminder interval must be from 5 to 60 s.";
    case ConfigValidationError::PADDLE_REMINDER_MAX_DURATION:
      return "Paddle reminder limit must be from 1 to 60 min and at least "
             "the reminder interval.";
    case ConfigValidationError::TIMING_RELATION:
      return "Required: rinse gesture < Max BBW time; rinse duration, retare "
             "window, and BBW protection each ≤ Max BBW time.";
    case ConfigValidationError::COMBINED_TARE_REQUIRES_AUTOTARE:
      return "The Bookoo combined command requires automatic tare.";
    case ConfigValidationError::POST_TARE_BASELINE_GRACE:
      return "Post-tare grace must be from 0.5 to 10 s.";
    case ConfigValidationError::SCALE_TIMER_STOP_EXTRA_DELAY:
      return "Scale timer stop extra delay must be from 0 to 1000 ms.";
    case ConfigValidationError::TIMEZONE_OFFSET:
      return "Timezone offset must be from -720 to +840 minutes.";
    case ConfigValidationError::NTP_SERVER_PRESET:
      return "NTP server preset must be pool, google, cloudflare, or nist.";
    case ConfigValidationError::NTP_SERVER_CUSTOM:
      return "Custom NTP hostname is invalid (letters, digits, '.', '-'; "
             "max 63 chars).";
    case ConfigValidationError::MAX_RECOVERY_WEIGHT:
      return "Max recovery must be from 10 to 200 g.";
    case ConfigValidationError::MIN_BBW_BREW_TIME:
      return "Min BBW brew time must be from 5 to 55 s.";
    case ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION:
      return "Fast guard requires max recovery > target, min BBW brew time < Max BBW "
             "time, and min BBW brew time ≥ BBW protection.";
    case ConfigValidationError::MIN_RECOVERY_WEIGHT:
      return "Min recovery must be from 10 to 200 g.";
    case ConfigValidationError::MAX_BBW_BREW_TIME:
      return "Max BBW brew time must be from 5 to 55 s.";
    case ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION:
      return "Slow guard requires min recovery < target, max BBW brew time < Max BBW "
             "time, max BBW brew time ≥ BBW protection, and max BBW brew time > min BBW brew time "
             "when Fast is on.";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_MODE:
      return "A→M limit mode must be manual or auto.";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT:
      return "A→M manual limit must be from 10 s up to Max BBW time.";
    case ConfigValidationError::AUTO_TO_MANUAL_GUARD_BASELINE:
      return "A→M baseline must be from 10 s up to Max BBW time.";
    case ConfigValidationError::WEIGHT_OFFSET_BASELINE:
      return "Offset baseline must be from 0 to 5.0 g.";
    case ConfigValidationError::SCALE_MAC_CACHE_MODE:
      return "Scale preference must be first, prefer, or only.";
    case ConfigValidationError::ALERT_OUTPUT_CHANNEL:
      return "Alert output channel must be scale_only, buzzer_only, or "
             "scale_priority.";
    case ConfigValidationError::EXTENDED_PULSE_RATE:
      return "Extended shot pulse must be disabled, slow, medium, fast, or "
             "rapid.";
    case ConfigValidationError::SLOW_EXTENDED_PULSE_RATE:
      return "Slow extended pulse must be disabled, slow, medium, fast, or "
             "rapid.";
    case ConfigValidationError::BOOKOO_CONNECT_BEEP_LEVEL:
      return "Bookoo scale volume must be disabled (0) or 1 to 5.";
    case ConfigValidationError::NO_SCALE_BBW_MODE:
      return "No-scale BBW mode must be off, warn_once, or require_scale.";
    case ConfigValidationError::LAST_SHOT_COOLDOWN:
      return "Last shot cooldown must be from 5 to 240 min.";
    case ConfigValidationError::DRIP_DELAY:
      return "Drip delay must be from 0 to 10 s.";
    case ConfigValidationError::SERIAL_LOG_LEVEL:
      return "Serial log level must be none, critical, error, warning, info, "
             "or debug.";
    case ConfigValidationError::RING_RETAIN_LOG_LEVEL:
      return "Ring log level must be none, critical, error, warning, info, or "
             "debug.";
    case ConfigValidationError::PADDLE_MODE:
      return "Paddle mode must be auto, natural or original.";
    case ConfigValidationError::CUP_PRESENT_WEIGHT:
      return "Cup-present threshold must be from 0.1 to 50 g.";
    case ConfigValidationError::CUP_REMOVED_WEIGHT:
      return "Cup-removed threshold must be from -50 to -0.1 g.";
    case ConfigValidationError::STOP_PULSE:
      return "Auto-stop pulse must be from 50 to 1000 ms.";
    case ConfigValidationError::MAX_SINGLE_PRESS:
      return "Single-press limit must be from 100 to 5000 ms.";
    case ConfigValidationError::REED_CONFIRM_TIMEOUT:
      return "Reed confirm timeout must be from 0.2 to 5 s.";
    case ConfigValidationError::SHOT_REACT_TIMEOUT:
      return "Shot reaction timeout must be 0 (compiled default) or from 3 to 30 s.";
  }
  return "Invalid configuration.";
}

bool jsonAutoToManualGuardLimitMode(cJSON *object, const char *name,
                                    uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  if (strcmp(item->valuestring, "manual") == 0) {
    output = static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL);
    return true;
  }
  if (strcmp(item->valuestring, "auto") == 0) {
    output = static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
    return true;
  }
  return false;
}

bool jsonNoScaleBbwMode(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  if (strcmp(item->valuestring, "off") == 0) {
    output = static_cast<uint8_t>(NoScaleBbwMode::OFF);
    return true;
  }
  if (strcmp(item->valuestring, "warn_once") == 0) {
    output = static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE);
    return true;
  }
  if (strcmp(item->valuestring, "require_scale") == 0) {
    output = static_cast<uint8_t>(NoScaleBbwMode::REQUIRE_SCALE);
    return true;
  }
  return false;
}

const char *autoToManualGuardLimitModeId(uint8_t mode) {
  return mode == static_cast<uint8_t>(AutoToManualGuardLimitMode::MANUAL)
             ? "manual"
             : "auto";
}

bool jsonPaddleMode(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parsePaddleMode(item->valuestring, output);
}

bool jsonStopPulseMs(cJSON *object, RuntimeConfig &config) {
  uint32_t ms = 0;
  if (!jsonUint32(object, "stopPulseMs", ms) || ms < 50 || ms > 1000) {
    return false;
  }
  setRuntimeStopPulseMs(config, ms);
  return true;
}

bool jsonMaxSinglePressMs(cJSON *object, RuntimeConfig &config) {
  uint32_t ms = 0;
  if (!jsonUint32(object, "maxSinglePressMs", ms) || ms < 100 || ms > 5000) {
    return false;
  }
  setRuntimeMaxSinglePressMs(config, ms);
  return true;
}

bool jsonReedConfirmTimeoutMs(cJSON *object, RuntimeConfig &config) {
  uint32_t ms = 0;
  if (!jsonUint32(object, "reedConfirmTimeoutMs", ms) || ms < 200 ||
      ms > 5000) {
    return false;
  }
  setRuntimeReedConfirmTimeoutMs(config, ms);
  return true;
}

bool jsonAssumeIdleWhenScaleConnects(cJSON *object, RuntimeConfig &config) {
  return jsonBoolean(object, "assumeIdleWhenScaleConnects",
                     config.assumeIdleWhenScaleConnects);
}

bool jsonShotReactTimeoutS(cJSON *object, RuntimeConfig &config) {
  uint8_t seconds = 0;
  if (!jsonUint8(object, "shotReactTimeoutS", seconds)) {
    return false;
  }
  if (seconds != 0 && (seconds < MIN_SHOT_REACT_TIMEOUT_S ||
                       seconds > MAX_SHOT_REACT_TIMEOUT_S)) {
    return false;
  }
  config.shotReactTimeoutS = seconds;
  return true;
}

bool jsonMomentaryStartEdge(cJSON *object, RuntimeConfig &config) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, "momentaryStartEdge");
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseMomentaryStartEdge(item->valuestring, config.momentaryStartOnPress);
}

bool jsonNtpPreset(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  if (strcmp(item->valuestring, "pool") == 0) {
    output = static_cast<uint8_t>(NtpServerPreset::POOL);
    return true;
  }
  if (strcmp(item->valuestring, "google") == 0) {
    output = static_cast<uint8_t>(NtpServerPreset::GOOGLE);
    return true;
  }
  if (strcmp(item->valuestring, "cloudflare") == 0) {
    output = static_cast<uint8_t>(NtpServerPreset::CLOUDFLARE);
    return true;
  }
  if (strcmp(item->valuestring, "nist") == 0) {
    output = static_cast<uint8_t>(NtpServerPreset::NIST);
    return true;
  }
  return false;
}

bool jsonScaleMacCacheMode(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseScaleMacCacheMode(item->valuestring, output);
}

bool jsonAlertOutputChannel(cJSON *object, const char *name, uint8_t &output,
                            bool optional) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (item == nullptr) {
    return optional;
  }
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseAlertOutputChannel(item->valuestring, output);
}

bool jsonLogLevel(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseLogLevel(item->valuestring, output);
}

bool jsonExtendedPulseRate(cJSON *object, const char *name, uint8_t &output) {
  cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  if (!cJSON_IsString(item) || item->valuestring == nullptr) {
    return false;
  }
  return parseExtendedPulseRate(item->valuestring, output);
}

const char *ntpPresetId(uint8_t preset) {
  switch (preset) {
    case static_cast<uint8_t>(NtpServerPreset::GOOGLE):
      return "google";
    case static_cast<uint8_t>(NtpServerPreset::CLOUDFLARE):
      return "cloudflare";
    case static_cast<uint8_t>(NtpServerPreset::NIST):
      return "nist";
    case static_cast<uint8_t>(NtpServerPreset::POOL):
    default:
      return "pool";
  }
}

void formatIp(const IPAddress &ip, char output[16]) {
  snprintf(output, 16, "%u.%u.%u.%u", static_cast<unsigned>(ip[0]),
           static_cast<unsigned>(ip[1]), static_cast<unsigned>(ip[2]),
           static_cast<unsigned>(ip[3]));
}

bool registerHandler(httpd_handle_t server, const char *uri,
                     httpd_method_t method, esp_err_t (*handler)(httpd_req_t *)) {
  httpd_uri_t descriptor = {};
  descriptor.uri = uri;
  descriptor.method = method;
  descriptor.handler = handler;
  return httpd_register_uri_handler(server, &descriptor) == ESP_OK;
}

// lwIP on ESP32-S3 cannot DMA a PSRAM or flash pointer in one tcp_write.
// Stream through a small internal bounce buffer; no full-body staging in DRAM.
// Single httpd server task (HTTPD_DEFAULT_CONFIG). Do not raise workers without
// giving this bounce its own lock or per-handler stack storage.
constexpr size_t HTTP_DRAM_BOUNCE_BYTES = 512;

static uint8_t g_httpSendBounce[HTTP_DRAM_BOUNCE_BYTES];

esp_err_t sendCopiedChunk(httpd_req_t *request, const void *data,
                          size_t length) {
  const auto *src = static_cast<const uint8_t *>(data);
  while (length > 0) {
    const size_t n =
        length < HTTP_DRAM_BOUNCE_BYTES ? length : HTTP_DRAM_BOUNCE_BYTES;
    memcpy(g_httpSendBounce, src, n);
    if (httpd_resp_send_chunk(request,
                              reinterpret_cast<const char *>(g_httpSendBounce),
                              n) != ESP_OK) {
      return ESP_FAIL;
    }
    src += n;
    length -= n;
  }
  return ESP_OK;
}

esp_err_t sendCopiedBody(httpd_req_t *request, const void *data,
                         size_t length) {
  if (length == 0) {
    return httpd_resp_send(request, nullptr, 0);
  }
  if (sendCopiedChunk(request, data, length) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, nullptr, 0);
}

esp_err_t sendJsonStringChunk(httpd_req_t *request, const char *value) {
  if (value == nullptr) {
    value = "";
  }
  if (httpd_resp_send_chunk(request, "\"", 1) != ESP_OK) {
    return ESP_FAIL;
  }
  const unsigned char *cursor =
      reinterpret_cast<const unsigned char *>(value);
  const unsigned char *segment = cursor;
  while (*cursor != '\0') {
    const char *escape = nullptr;
    char unicodeEscape[7] = {};
    switch (*cursor) {
      case '\"': escape = "\\\""; break;
      case '\\': escape = "\\\\"; break;
      case '\b': escape = "\\b"; break;
      case '\f': escape = "\\f"; break;
      case '\n': escape = "\\n"; break;
      case '\r': escape = "\\r"; break;
      case '\t': escape = "\\t"; break;
      default:
        if (*cursor < 0x20) {
          snprintf(unicodeEscape, sizeof(unicodeEscape), "\\u%04x", *cursor);
          escape = unicodeEscape;
        }
        break;
    }
    if (escape != nullptr) {
      if (cursor > segment &&
          sendCopiedChunk(request, segment,
                          static_cast<size_t>(cursor - segment)) != ESP_OK) {
        return ESP_FAIL;
      }
      if (sendCopiedChunk(request, escape, strlen(escape)) != ESP_OK) {
        return ESP_FAIL;
      }
      segment = cursor + 1;
    }
    ++cursor;
  }
  if (cursor > segment &&
      sendCopiedChunk(request, segment,
                      static_cast<size_t>(cursor - segment)) != ESP_OK) {
    return ESP_FAIL;
  }
  return httpd_resp_send_chunk(request, "\"", 1);
}

void sanitizeJsonEmbed(const char *input, char *output, size_t capacity) {
  if (capacity == 0) {
    return;
  }
  size_t written = 0;
  for (size_t index = 0; input != nullptr && input[index] != '\0' &&
                          written + 1 < capacity;
       ++index) {
    const unsigned char byte = static_cast<unsigned char>(input[index]);
    if (byte == '\"' || byte == '\\' || byte < 0x20) {
      continue;
    }
    output[written++] = static_cast<char>(byte);
  }
  output[written] = '\0';
}

enum class StatusPage : uint8_t { Home, Settings, Admin, Diagnostic, Unknown };

StatusPage parseStatusPage(const char *uri) {
  if (uri == nullptr) {
    return StatusPage::Unknown;
  }
  const char *path = uri;
  const char *query = strchr(uri, '?');
  char stack[64] = {};
  if (query != nullptr) {
    const size_t len = static_cast<size_t>(query - uri);
    if (len >= sizeof(stack)) {
      return StatusPage::Unknown;
    }
    memcpy(stack, uri, len);
    stack[len] = '\0';
    path = stack;
  }
  if (strcmp(path, "/api/v1/status/home") == 0) {
    return StatusPage::Home;
  }
  if (strcmp(path, "/api/v1/status/settings") == 0) {
    return StatusPage::Settings;
  }
  if (strcmp(path, "/api/v1/status/admin") == 0) {
    return StatusPage::Admin;
  }
  if (strcmp(path, "/api/v1/status/diagnostic") == 0) {
    return StatusPage::Diagnostic;
  }
  return StatusPage::Unknown;
}

bool statusJsonAppend(size_t *used, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

bool __attribute__((format(printf, 2, 3)))
statusJsonAppend(size_t *used, const char *fmt, ...) {
  if (g_work == nullptr || used == nullptr ||
      *used >= NetworkWorkBuf::kStatusJson) {
    return false;
  }
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(g_work->statusJson + *used,
                          NetworkWorkBuf::kStatusJson - *used, fmt, args);
  va_end(args);
  if (n < 0 ||
      static_cast<size_t>(n) >= NetworkWorkBuf::kStatusJson - *used) {
    return false;
  }
  *used += static_cast<size_t>(n);
  return true;
}

bool jsonScratchAppend(char *buf, size_t cap, size_t *used, const char *fmt,
                       ...) {
  if (buf == nullptr || used == nullptr || *used >= cap) {
    return false;
  }
  va_list args;
  va_start(args, fmt);
  const int n = vsnprintf(buf + *used, cap - *used, fmt, args);
  va_end(args);
  if (n < 0 || static_cast<size_t>(n) >= cap - *used) {
    return false;
  }
  *used += static_cast<size_t>(n);
  return true;
}

bool formatTaskProfilerObject(char *buf, size_t cap, size_t *used,
                              const TaskProfilerSnapshot &tasks) {
  if (!jsonScratchAppend(
          buf, cap, used,
          "{\"state\":\"%s\",\"stopReason\":\"%s\","
          "\"elapsedMs\":%lu,\"remainingMs\":%lu,\"sampleCount\":%lu,"
          "\"currentTotalCpuPct\":%.1f,\"averageTotalCpuPct\":%.1f,"
          "\"unreportedCurrentCpuPct\":%.1f,\"unreportedAverageCpuPct\":%.1f,"
          "\"truncated\":%s,\"lastCaptureUs\":%lu,\"maxCaptureUs\":%lu,"
          "\"rows\":[",
          taskProfilerStateName(tasks.state),
          taskProfilerStopReasonName(tasks.stopReason),
          static_cast<unsigned long>(tasks.elapsedMs),
          static_cast<unsigned long>(tasks.remainingMs),
          static_cast<unsigned long>(tasks.sampleCount),
          static_cast<double>(tasks.currentTotalCpuPct),
          static_cast<double>(tasks.averageTotalCpuPct),
          static_cast<double>(tasks.unreportedCurrentCpuPct),
          static_cast<double>(tasks.unreportedAverageCpuPct),
          tasks.truncated ? "true" : "false",
          static_cast<unsigned long>(tasks.lastCaptureUs),
          static_cast<unsigned long>(tasks.maxCaptureUs))) {
    return false;
  }
  for (uint8_t i = 0; i < tasks.rowCount; ++i) {
    char safeName[TASK_PROFILER_NAME_CAPACITY * 2] = {};
    sanitizeJsonEmbed(tasks.rows[i].name, safeName, sizeof(safeName));
    if (!jsonScratchAppend(
            buf, cap, used,
            "%s{\"name\":\"%s\",\"core\":%d,\"currentCpuPct\":%.1f,"
            "\"averageCpuPct\":%.1f,\"stackMinWords\":%lu}",
            i == 0 ? "" : ",", safeName, static_cast<int>(tasks.rows[i].core),
            static_cast<double>(tasks.rows[i].currentCpuPct),
            static_cast<double>(tasks.rows[i].averageCpuPct),
            static_cast<unsigned long>(tasks.rows[i].stackMinWords))) {
      return false;
    }
  }
  return jsonScratchAppend(buf, cap, used, "]}");
}

bool statusJsonAppendTaskProfiler(size_t *used,
                                  const TaskProfilerSnapshot &tasks) {
  if (!statusJsonAppend(used, ",\"tasks\":")) {
    return false;
  }
  return formatTaskProfilerObject(g_work->statusJson, NetworkWorkBuf::kStatusJson,
                                  used, tasks);
}

void buildSlimPresetsJson(const ShotPresetBank &presets) {
  if (g_work == nullptr) {
    return;
  }
  char *buf = g_work->presetsJson;
  const size_t cap = NetworkWorkBuf::kPresetsJson;
  buf[0] = '{';
  buf[1] = '}';
  buf[2] = 0;
  size_t used = 0;
  int n = snprintf(buf, cap, "{\"activeId\":%u,\"items\":[",
                   static_cast<unsigned>(presets.activeId));
  if (n > 0) {
    used = static_cast<size_t>(n);
  }
  for (uint8_t i = 0; i < presets.count && i < MAX_SHOT_PRESETS; ++i) {
    const ShotPreset &p = presets.presets[i];
    char safeName[SHOT_PRESET_NAME_CAPACITY * 2] = {};
    sanitizeJsonEmbed(p.name, safeName, sizeof(safeName));
    n = snprintf(
        buf + used, cap - used,
        "%s{\"id\":%u,\"name\":\"%s\",\"isFactory\":%s,\"brewByWeight\":%s,"
        "\"goalWeightG\":%u,\"minBbwBrewTimeMs\":%lu,\"maxRecoveryWeightG\":%.1f}",
        i == 0 ? "" : ",", static_cast<unsigned>(p.id), safeName,
        p.isFactory ? "true" : "false", p.brewByWeight ? "true" : "false",
        static_cast<unsigned>(p.goalWeightG),
        static_cast<unsigned long>(p.minBbwBrewTimeMs),
        static_cast<double>(p.maxRecoveryWeightG));
    if (n < 0 || static_cast<size_t>(n) >= cap - used) {
      break;
    }
    used += static_cast<size_t>(n);
  }
  if (used + 2 < cap) {
    buf[used++] = ']';
    buf[used++] = '}';
    buf[used] = 0;
  }
}

void buildScaleHistoryJson(const ScaleHistoryEntry *entries) {
  if (g_work == nullptr) {
    return;
  }
  char *buf = g_work->historyJson;
  const size_t cap = NetworkWorkBuf::kHistoryJson;
  buf[0] = '[';
  buf[1] = ']';
  buf[2] = 0;
  size_t used = 0;
  int n = snprintf(buf, cap, "[");
  if (n > 0) {
    used = static_cast<size_t>(n);
  }
  bool first = true;
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    const ScaleHistoryEntry &entry = entries[i];
    if (entry.mac[0] == '\0') {
      continue;
    }
    char safeMac[PREFERRED_SCALE_MAC_CAPACITY * 2] = {};
    char safeName[PREFERRED_SCALE_NAME_CAPACITY * 2] = {};
    sanitizeJsonEmbed(entry.mac, safeMac, sizeof(safeMac));
    sanitizeJsonEmbed(entry.name, safeName, sizeof(safeName));
    n = snprintf(buf + used, cap - used, "%s{\"mac\":\"%s\",\"name\":\"%s\"}",
                 first ? "" : ",", safeMac, safeName);
    if (n <= 0 || static_cast<size_t>(n) >= cap - used) {
      break;
    }
    used += static_cast<size_t>(n);
    first = false;
  }
  if (used + 1 < cap) {
    buf[used++] = ']';
    buf[used] = 0;
  }
}


}  // namespace

ShotStopperNetwork *ShotStopperNetwork::instance_ = nullptr;


// Behavior-preserving service fragments; kept in this translation unit.
#include "network/ShotStopperNetworkService.inc"
#include "network/ShotStopperWifi.inc"
#include "network/ShotStopperHttpLifecycle.inc"
#include "network/ShotStopperHttpAuthAssets.inc"
#include "network/ShotStopperStatus.inc"
#include "diagnostics/ShotStopperNetworkDiagnostics.inc"
#include "network/ShotStopperConfiguration.inc"
#include "network/ShotStopperNetworkOta.inc"
