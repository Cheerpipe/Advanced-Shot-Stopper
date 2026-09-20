#include "ShotStopperMicraService.h"

#include "ShotStopperJsonArena.h"
#include "ShotStopperDomain.h"
#include "ShotStopperMicraTiming.h"
#include "ShotStopperPsram.h"

#include <Arduino.h>
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_random.h>
#include <mbedtls/base64.h>
#include <psa/crypto.h>
#include <sys/time.h>

#include <cstdio>
#include <cmath>
#include <cstring>
#include <new>

void serialTraceCategoryf(shotstopper::LogLevel level,
                          shotstopper::DebugCategory category,
                          const char *fmt, ...);

namespace shotstopper {
namespace {

constexpr char kApiRoot[] = "https://lion.lamarzocco.io/api/customer-app";
constexpr char kRegisterUrl[] =
    "https://lion.lamarzocco.io/api/customer-app/auth/init";
constexpr char kSignInUrl[] =
    "https://lion.lamarzocco.io/api/customer-app/auth/signin";
constexpr char kRefreshUrl[] =
    "https://lion.lamarzocco.io/api/customer-app/auth/refreshtoken";
constexpr char kThingsUrl[] =
    "https://lion.lamarzocco.io/api/customer-app/things";
constexpr size_t kResponseCapacity = 16 * 1024;
constexpr size_t kResponseMaxValues = 1024;
constexpr size_t kTokenCapacity = 2048;
constexpr size_t kBodyCapacity = 4608;
constexpr size_t kWorkerStackBytes = 8192;

constexpr uint8_t kP256SpkiPrefix[] = {
    0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2a, 0x86, 0x48,
    0xce, 0x3d, 0x02, 0x01, 0x06, 0x08, 0x2a, 0x86, 0x48,
    0xce, 0x3d, 0x03, 0x01, 0x07, 0x03, 0x42, 0x00};

bool secureCopy(char *destination, size_t capacity, const char *source) {
  if (destination == nullptr || source == nullptr || capacity == 0) return false;
  const size_t length = strnlen(source, capacity);
  if (length >= capacity) return false;
  memset(destination, 0, capacity);
  memcpy(destination, source, length);
  return true;
}

void secureWipe(void *memory, size_t size) {
  volatile uint8_t *bytes = static_cast<volatile uint8_t *>(memory);
  while (size-- > 0) *bytes++ = 0;
}

bool base64Encode(const uint8_t *input, size_t inputLength, char *output,
                  size_t capacity) {
  size_t written = 0;
  if (mbedtls_base64_encode(reinterpret_cast<unsigned char *>(output),
                            capacity, &written, input, inputLength) != 0 ||
      written >= capacity) {
    return false;
  }
  output[written] = '\0';
  return true;
}

bool sha256(const void *input, size_t length, uint8_t output[32]) {
  size_t written = 0;
  return psa_hash_compute(PSA_ALG_SHA_256,
                          static_cast<const uint8_t *>(input), length, output,
                          32, &written) == PSA_SUCCESS && written == 32;
}

void formatUuid(const uint8_t source[16], char output[37]) {
  uint8_t bytes[16];
  memcpy(bytes, source, sizeof(bytes));
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0fU) | 0x40U);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3fU) | 0x80U);
  snprintf(output, 37,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-"
           "%02x%02x%02x%02x%02x%02x",
           bytes[0], bytes[1], bytes[2], bytes[3], bytes[4], bytes[5],
           bytes[6], bytes[7], bytes[8], bytes[9], bytes[10], bytes[11],
           bytes[12], bytes[13], bytes[14], bytes[15]);
  secureWipe(bytes, sizeof(bytes));
}

bool installationId(const LineaMicraPersistedSettings &settings,
                    char output[37]) {
  uint8_t digest[32];
  const bool ok = sha256(settings.installationPrivateKey,
                         sizeof(settings.installationPrivateKey), digest);
  if (ok) formatUuid(digest, output);
  secureWipe(digest, sizeof(digest));
  return ok;
}

void setKeyAttributes(psa_key_attributes_t &attributes) {
  psa_set_key_type(&attributes,
                   PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&attributes, 256);
  psa_set_key_usage_flags(&attributes,
                          PSA_KEY_USAGE_EXPORT | PSA_KEY_USAGE_SIGN_HASH);
  psa_set_key_algorithm(&attributes, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
}

bool importPrivateKey(const uint8_t privateKey[32],
                      mbedtls_svc_key_id_t &key) {
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  setKeyAttributes(attributes);
  const psa_status_t status =
      psa_import_key(&attributes, privateKey, 32, &key);
  psa_reset_key_attributes(&attributes);
  return status == PSA_SUCCESS;
}

bool publicKeyDer(const LineaMicraPersistedSettings &settings,
                  uint8_t output[91]) {
  mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
  if (!importPrivateKey(settings.installationPrivateKey, key)) return false;
  uint8_t publicKey[65];
  size_t publicLength = 0;
  const psa_status_t status =
      psa_export_public_key(key, publicKey, sizeof(publicKey), &publicLength);
  (void)psa_destroy_key(key);
  if (status != PSA_SUCCESS || publicLength != sizeof(publicKey) ||
      publicKey[0] != 0x04) {
    secureWipe(publicKey, sizeof(publicKey));
    return false;
  }
  memcpy(output, kP256SpkiPrefix, sizeof(kP256SpkiPrefix));
  memcpy(output + sizeof(kP256SpkiPrefix), publicKey, sizeof(publicKey));
  secureWipe(publicKey, sizeof(publicKey));
  return true;
}

bool deriveInstallationSecret(const LineaMicraPersistedSettings &settings,
                              const char *id, uint8_t secret[32],
                              char publicB64[128]) {
  uint8_t publicDer[91];
  uint8_t idHash[32];
  char idHashB64[48];
  char material[256];
  bool ok = publicKeyDer(settings, publicDer) &&
            base64Encode(publicDer, sizeof(publicDer), publicB64, 128) &&
            sha256(id, strlen(id), idHash) &&
            base64Encode(idHash, sizeof(idHash), idHashB64,
                         sizeof(idHashB64));
  if (ok) {
    const int length = snprintf(material, sizeof(material), "%s.%s.%s", id,
                                publicB64, idHashB64);
    ok = length > 0 && static_cast<size_t>(length) < sizeof(material) &&
         sha256(material, static_cast<size_t>(length), secret);
  }
  secureWipe(publicDer, sizeof(publicDer));
  secureWipe(idHash, sizeof(idHash));
  secureWipe(idHashB64, sizeof(idHashB64));
  secureWipe(material, sizeof(material));
  return ok;
}

bool requestProof(const char *input, const uint8_t secret[32],
                  char output[48]) {
  uint8_t work[32];
  memcpy(work, secret, sizeof(work));
  for (const uint8_t *cursor = reinterpret_cast<const uint8_t *>(input);
       *cursor != 0; ++cursor) {
    const uint8_t index = *cursor % 32U;
    const uint8_t shift = work[(index + 1U) % 32U] & 7U;
    const uint8_t value = static_cast<uint8_t>(*cursor ^ work[index]);
    work[index] = shift == 0
                      ? value
                      : static_cast<uint8_t>((value << shift) |
                                             (value >> (8U - shift)));
  }
  uint8_t digest[32];
  const bool ok = sha256(work, sizeof(work), digest) &&
                  base64Encode(digest, sizeof(digest), output, 48);
  secureWipe(work, sizeof(work));
  secureWipe(digest, sizeof(digest));
  return ok;
}

size_t encodeAsn1Integer(const uint8_t *raw, size_t rawLength,
                         uint8_t *output) {
  size_t start = 0;
  while (start + 1U < rawLength && raw[start] == 0) ++start;
  const bool pad = (raw[start] & 0x80U) != 0;
  const size_t valueLength = rawLength - start;
  output[0] = 0x02;
  output[1] = static_cast<uint8_t>(valueLength + (pad ? 1U : 0U));
  size_t offset = 2;
  if (pad) output[offset++] = 0;
  memcpy(output + offset, raw + start, valueLength);
  return offset + valueLength;
}

bool signRequest(const LineaMicraPersistedSettings &settings,
                 const char *message, char output[104]) {
  uint8_t digest[32];
  if (!sha256(message, strlen(message), digest)) return false;
  mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
  if (!importPrivateKey(settings.installationPrivateKey, key)) {
    secureWipe(digest, sizeof(digest));
    return false;
  }
  uint8_t raw[64];
  size_t rawLength = 0;
  const psa_status_t status = psa_sign_hash(
      key, PSA_ALG_ECDSA(PSA_ALG_SHA_256), digest, sizeof(digest), raw,
      sizeof(raw), &rawLength);
  (void)psa_destroy_key(key);
  secureWipe(digest, sizeof(digest));
  if (status != PSA_SUCCESS || rawLength != sizeof(raw)) {
    secureWipe(raw, sizeof(raw));
    return false;
  }
  uint8_t der[72];
  const size_t rLength = encodeAsn1Integer(raw, 32, der + 2);
  const size_t sLength = encodeAsn1Integer(raw + 32, 32, der + 2 + rLength);
  der[0] = 0x30;
  der[1] = static_cast<uint8_t>(rLength + sLength);
  const bool ok = base64Encode(der, 2 + rLength + sLength, output, 104);
  secureWipe(raw, sizeof(raw));
  secureWipe(der, sizeof(der));
  return ok;
}

bool jsonEscape(const char *input, char *output, size_t capacity) {
  size_t used = 0;
  if (input == nullptr || output == nullptr || capacity == 0) return false;
  for (; *input != '\0'; ++input) {
    const char *escape = nullptr;
    if (*input == '"') escape = "\\\"";
    else if (*input == '\\') escape = "\\\\";
    else if (*input == '\b') escape = "\\b";
    else if (*input == '\f') escape = "\\f";
    else if (*input == '\n') escape = "\\n";
    else if (*input == '\r') escape = "\\r";
    else if (*input == '\t') escape = "\\t";
    const size_t count = escape == nullptr ? 1U : strlen(escape);
    if (used + count >= capacity) return false;
    if (escape == nullptr) output[used++] = *input;
    else {
      memcpy(output + used, escape, count);
      used += count;
    }
  }
  output[used] = '\0';
  return true;
}

const char *jsonText(const cJSON *object, const char *name) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
  return cJSON_IsString(item) && item->valuestring != nullptr
             ? item->valuestring
             : nullptr;
}

cJSON *parseResponse(const char *response) {
  return parseJsonDocumentWithinLimits(response, kResponseCapacity - 1,
                                       kResponseMaxValues);
}

LineaMicraObservedMode parseMode(const char *mode) {
  if (mode == nullptr) return LineaMicraObservedMode::NONE;
  if (strcmp(mode, "StandBy") == 0) return LineaMicraObservedMode::STANDBY;
  if (strcmp(mode, "BrewingMode") == 0)
    return LineaMicraObservedMode::BREWING;
  if (strcmp(mode, "EcoMode") == 0) return LineaMicraObservedMode::ECO;
  return LineaMicraObservedMode::UNSUPPORTED;
}

bool sameTemperatureRequest(const LineaMicraRequest &left,
                            const LineaMicraRequest &right) {
  return left.requestId == right.requestId &&
         left.configGeneration == right.configGeneration &&
         left.targetDeciC == right.targetDeciC &&
         left.presetId == right.presetId && left.type == right.type;
}

bool sameSessionIdentity(const LineaMicraPersistedSettings &left,
                         const LineaMicraPersistedSettings &right) {
  return left.accountConfigured == right.accountConfigured &&
         strcmp(left.username, right.username) == 0 &&
         strcmp(left.password, right.password) == 0 &&
         memcmp(left.installationPrivateKey, right.installationPrivateKey,
                sizeof(left.installationPrivateKey)) == 0 &&
         strcmp(left.selectedSerial, right.selectedSerial) == 0;
}

void preserveTemperatureStatus(const LineaMicraStatus &current,
                               LineaMicraStatus &next) {
  next.requestedTargetDeciC = current.requestedTargetDeciC;
  next.appliedTargetDeciC = current.appliedTargetDeciC;
  next.temperatureTransportStatus = current.temperatureTransportStatus;
  next.temperatureHttpStatus = current.temperatureHttpStatus;
  next.temperatureState = current.temperatureState;
  next.temperatureError = current.temperatureError;
  next.temperatureCommandAccepted = current.temperatureCommandAccepted;
  next.temperatureRetryable = current.temperatureRetryable;
}

}  // namespace

struct ShotStopperMicraService::IoBuffer {
  union {
    char response[kResponseCapacity];
    char body[kBodyCapacity];
  };

  IoBuffer() : response{} {}
};

struct ShotStopperMicraService::WorkBuffer {
  char accessToken[kTokenCapacity] = {};
  char refreshToken[kTokenCapacity] = {};
  esp_http_client_handle_t client = nullptr;
  size_t responseUsed = 0;
  uint32_t accessTokenIssuedAtMs = 0;
  uint16_t httpStatus = 0;
  int32_t transportStatus = 0;
  bool responseOverflow = false;
};

struct ShotStopperMicraService::RequestStateGuard {
  ShotStopperMicraService &service;
  ~RequestStateGuard() { service.clearRequestState(); }
};

bool ShotStopperMicraService::begin() {
  if (task_ != nullptr || psa_crypto_init() != PSA_SUCCESS) {
    return false;
  }
  if (xTaskCreatePinnedToCore(taskEntry, "micra_cloud", kWorkerStackBytes, this,
                             tskIDLE_PRIORITY, &task_, 0) != pdPASS) {
    return false;
  }
  return true;
}

void ShotStopperMicraService::publishConfig(
    const LineaMicraPersistedSettings &settings, uint32_t configGeneration) {
  bool cancelTemperature = false;
  bool cancelObservation = false;
  bool identityChanged = false;
  {
    TaskLockGuard lock(mux_);
    const bool wasObserving =
        config_.accountConfigured &&
        (config_.options & LINEA_MICRA_OBSERVE_STATE) != 0;
    const bool configChanged =
        memcmp(&config_, &settings, sizeof(settings)) != 0;
    identityChanged = !sameSessionIdentity(config_, settings);
    config_ = settings;
    configGeneration_ = configGeneration;
    if (identityChanged) {
      ++identityGeneration_;
      published_ = {};
      powerState_.reset();
      observationSchedule_ = {};
      wipeLineaMicraSettings(pending_.credentials);
      pending_.present = false;
      desiredTemperature_ = {};
      cancelTemperature = true;
    } else if (desiredTemperature_.present) {
      desiredTemperature_.machineConfigGeneration = configGeneration;
    }
    published_.configGeneration = configGeneration;
    published_.identityGeneration = identityGeneration_;
    published_.accountConfigured = settings.accountConfigured;
    const bool observing =
        settings.accountConfigured &&
        (settings.options & LINEA_MICRA_OBSERVE_STATE) != 0;
    if (!configChanged) return;
    wipeLineaMicraSettings(candidate_);
    discovery_ = {};
    if (!settings.accountConfigured) {
      powerState_.reset();
      wipeLineaMicraSettings(pending_.credentials);
      pending_.present = false;
      desiredTemperature_ = {};
      published_.phase = LineaMicraPhase::Disabled;
      published_.quality = LineaMicraObservationQuality::UNCONFIGURED;
      published_.temperatureState = LineaMicraTemperatureState::Disabled;
      observationSchedule_ = {};
    } else if (identityChanged ||
               published_.phase == LineaMicraPhase::Disabled) {
      published_.phase = LineaMicraPhase::IDLE;
      published_.quality = LineaMicraObservationQuality::STALE;
      observationSchedule_.dueNow(millis());
    }
    if (!observing) {
      powerState_.reset();
      observationSchedule_ = {};
      if (wasObserving && settings.accountConfigured) {
        published_.phase = LineaMicraPhase::IDLE;
        published_.error = LineaMicraError::NONE;
      }
      if (pending_.present &&
          pending_.request.type == LineaMicraRequestType::OBSERVE_STATE) {
        wipeLineaMicraSettings(pending_.credentials);
        pending_.present = false;
      }
      cancelObservation = wasObserving;
    } else if (!wasObserving) {
      observationSchedule_.dueNow(millis());
    }
    if ((settings.options & LINEA_MICRA_APPLY_TEMPERATURE) == 0) {
      desiredTemperature_ = {};
      published_.temperatureState = LineaMicraTemperatureState::Disabled;
      published_.temperatureError = LineaMicraError::NONE;
      published_.temperatureTransportStatus = 0;
      published_.temperatureHttpStatus = 0;
      published_.temperatureCommandAccepted = false;
      published_.temperatureRetryable = false;
      cancelTemperature = true;
    } else if (settings.accountConfigured &&
               published_.temperatureState ==
                   LineaMicraTemperatureState::Disabled) {
      published_.temperatureState = LineaMicraTemperatureState::IDLE;
    }
  }
  const bool cloudDisabled =
      !settings.accountConfigured ||
      (settings.options & (LINEA_MICRA_APPLY_TEMPERATURE |
                           LINEA_MICRA_OBSERVE_STATE)) == 0;
  if (identityChanged || cloudDisabled) {
    clearSessionRequested_.store(true, std::memory_order_release);
    abortRequested_.store(true, std::memory_order_release);
  } else if ((cancelObservation &&
              observationActive_.load(std::memory_order_acquire)) ||
             (cancelTemperature &&
              temperatureActive_.load(std::memory_order_acquire))) {
    abortRequested_.store(true, std::memory_order_release);
  }
  if (task_ != nullptr) xTaskNotifyGive(task_);
}

void ShotStopperMicraService::publishNetworkState(bool staConnected,
                                                  bool apActive,
                                                  bool shotActive,
                                                  bool scaleConnecting) {
  const bool wasSta = staConnected_.exchange(staConnected,
                                              std::memory_order_acq_rel);
  const bool wasAp = apActive_.exchange(apActive, std::memory_order_acq_rel);
  const bool wasActive = shotActive_.exchange(shotActive,
                                               std::memory_order_acq_rel);
  const bool wasScale = scaleConnecting_.exchange(
      scaleConnecting, std::memory_order_acq_rel);
  const bool wasEligible = wasSta && !wasAp && !wasActive && !wasScale;
  const bool eligible =
      staConnected && !apActive && !shotActive && !scaleConnecting;
  if ((!staConnected && wasSta) || (apActive && !wasAp) ||
      (shotActive && !wasActive) || (scaleConnecting && !wasScale)) {
    abortRequested_.store(true, std::memory_order_release);
    if (!staConnected || apActive) {
      clearSessionRequested_.store(true, std::memory_order_release);
    }
  } else if (eligible) {
    abortRequested_.store(false, std::memory_order_release);
    if (!wasEligible) {
      TaskLockGuard lock(mux_);
      if (config_.accountConfigured &&
          (config_.options & LINEA_MICRA_OBSERVE_STATE) != 0) {
        observationSchedule_.dueNow(millis());
      }
      if (desiredTemperature_.present) {
        desiredTemperature_.retryAtMs = millis();
      }
    }
  }
  if (task_ != nullptr) xTaskNotifyGive(task_);
}

bool ShotStopperMicraService::queueConnect(uint32_t requestId,
                                           const char *username,
                                           const char *password) {
  LineaMicraPersistedSettings credentials;
  if (!secureCopy(credentials.username, sizeof(credentials.username), username) ||
      !secureCopy(credentials.password, sizeof(credentials.password), password) ||
      !lineaMicraBoundedText(credentials.username,
                             sizeof(credentials.username), false) ||
      strchr(credentials.username, '@') == nullptr ||
      !lineaMicraBoundedText(credentials.password,
                             sizeof(credentials.password), false)) {
    wipeLineaMicraSettings(credentials);
    return false;
  }
  TaskLockGuard lock(mux_);
  if (pending_.present || active_ || task_ == nullptr) {
    wipeLineaMicraSettings(credentials);
    return false;
  }
  pending_.request.requestId = requestId;
  pending_.request.configGeneration = configGeneration_;
  pending_.request.type = LineaMicraRequestType::CONNECT;
  pending_.credentials = credentials;
  pending_.identityGeneration = identityGeneration_;
  pending_.present = true;
  published_.requestId = requestId;
  published_.phase = LineaMicraPhase::QUEUED;
  published_.error = LineaMicraError::NONE;
  discovery_ = {};
  wipeLineaMicraSettings(candidate_);
  wipeLineaMicraSettings(credentials);
  xTaskNotifyGive(task_);
  return true;
}

bool ShotStopperMicraService::queue(const LineaMicraRequest &request) {
  TaskLockGuard lock(mux_);
  if (request.type == LineaMicraRequestType::APPLY_TEMPERATURE) {
    if (task_ == nullptr || !config_.accountConfigured ||
        (config_.options & LINEA_MICRA_APPLY_TEMPERATURE) == 0 ||
        request.presetId == 0 ||
        request.targetDeciC < LINEA_MICRA_BREW_TARGET_MIN_DECI_C ||
        request.targetDeciC > LINEA_MICRA_BREW_TARGET_MAX_DECI_C) {
      return false;
    }
    if (desiredTemperature_.present &&
        static_cast<int32_t>(request.configGeneration -
                             desiredTemperature_.request.configGeneration) < 0) {
      return true;
    }
    desiredTemperature_.request = request;
    desiredTemperature_.request.requestId = nextAutomaticRequestId_++;
    desiredTemperature_.machineConfigGeneration = configGeneration_;
    desiredTemperature_.retryAtMs = millis();
    desiredTemperature_.present = true;
    desiredTemperature_.commandAccepted = false;
    published_.requestedTargetDeciC = request.targetDeciC;
    published_.temperatureState = LineaMicraTemperatureState::PENDING;
    published_.temperatureError = LineaMicraError::NONE;
    published_.temperatureTransportStatus = 0;
    published_.temperatureHttpStatus = 0;
    published_.temperatureCommandAccepted = false;
    published_.temperatureRetryable = true;
    if (temperatureActive_.load(std::memory_order_acquire)) {
      abortRequested_.store(true, std::memory_order_release);
    }
    xTaskNotifyGive(task_);
    return true;
  }
  if (pending_.present || active_ || task_ == nullptr ||
      !config_.accountConfigured ||
      (config_.options & LINEA_MICRA_OBSERVE_STATE) == 0 ||
      request.type != LineaMicraRequestType::OBSERVE_STATE) {
    return false;
  }
  pending_.request = request;
  pending_.identityGeneration = identityGeneration_;
  pending_.present = true;
  published_.requestId = request.requestId;
  published_.phase = LineaMicraPhase::QUEUED;
  published_.error = LineaMicraError::NONE;
  xTaskNotifyGive(task_);
  return true;
}

bool ShotStopperMicraService::selectDiscoveredMachine(
    const char *serial, LineaMicraPersistedSettings &settings) {
  TaskLockGuard lock(mux_);
  if (serial == nullptr || discovery_.count == 0 ||
      !lineaMicraPrivateKeyConfigured(candidate_)) {
    return false;
  }
  for (uint8_t index = 0; index < discovery_.count; ++index) {
    if (strcmp(discovery_.machines[index].serial, serial) != 0) continue;
    settings = candidate_;
    if (!secureCopy(settings.selectedSerial, sizeof(settings.selectedSerial),
                    discovery_.machines[index].serial) ||
        !secureCopy(settings.selectedName, sizeof(settings.selectedName),
                    discovery_.machines[index].name)) {
      wipeLineaMicraSettings(settings);
      return false;
    }
    settings.accountConfigured = true;
    return validLineaMicraSettings(settings);
  }
  return false;
}

void ShotStopperMicraService::clearDiscovery() {
  TaskLockGuard lock(mux_);
  wipeLineaMicraSettings(candidate_);
  discovery_ = {};
}

LineaMicraStatus ShotStopperMicraService::status() const {
  TaskLockGuard lock(mux_);
  LineaMicraStatus result = published_;
  const uint32_t now = millis();
  const bool observing = config_.accountConfigured &&
                         (config_.options & LINEA_MICRA_OBSERVE_STATE) != 0;
  result = powerState_.effectiveStatus(result, observing, now);
  result.staConnected = staConnected_.load(std::memory_order_acquire);
  result.apActive = apActive_.load(std::memory_order_acquire);
  result.shotPaused = shotActive_.load(std::memory_order_acquire);
  result.scalePaused = scaleConnecting_.load(std::memory_order_acquire);
  return result;
}

MachinePhysicalStartDisposition ShotStopperMicraService::physicalStart() {
  TaskLockGuard lock(mux_);
  const uint32_t now = millis();
  const bool observing = config_.accountConfigured &&
                         (config_.options & LINEA_MICRA_OBSERVE_STATE) != 0;
  if (!powerState_.notePhysicalStart(
          published_, observing,
          (config_.options & LINEA_MICRA_RECOGNIZE_WAKE) != 0, now)) {
    return MachinePhysicalStartDisposition::NORMAL;
  }
  observationSchedule_.armPostWake(now);
  return MachinePhysicalStartDisposition::WAKE_PASSTHROUGH;
}

LineaMicraDiscoverySnapshot ShotStopperMicraService::discovery() const {
  TaskLockGuard lock(mux_);
  return discovery_;
}

void ShotStopperMicraService::serviceAbort() {
  if (!abortRequested_.load(std::memory_order_acquire)) return;
  TaskLockGuard lock(clientMux_);
  if (activeClient_ != nullptr) {
    (void)esp_http_client_cancel_request(activeClient_);
  }
}

void ShotStopperMicraService::taskEntry(void *context) {
  static_cast<ShotStopperMicraService *>(context)->taskLoop();
}

void ShotStopperMicraService::taskLoop() {
  for (;;) {
    if (clearSessionRequested_.exchange(false, std::memory_order_acq_rel)) {
      releaseWorkBuffer();
    }
    PendingRequest pending;
    bool haveRequest = false;
    bool haveTemperature = false;
    uint32_t temperatureMachineConfigGeneration = 0;
    const uint32_t now = millis();
    LineaMicraError observationGate = LineaMicraError::NONE;
    const bool networkReady = networkEligible(observationGate);
    const bool localActivity = shotActive_.load(std::memory_order_acquire);
    const bool staEligible = staConnected_.load(std::memory_order_acquire) &&
                             !apActive_.load(std::memory_order_acquire);
    {
      TaskLockGuard lock(mux_);
      const bool pendingObservation =
          pending_.present &&
          pending_.request.type == LineaMicraRequestType::OBSERVE_STATE;
      const bool observationReady =
          networkReady && !localActivity &&
          observationSchedule_.observationAllowed(now);
      if (pending_.present && (!pendingObservation || observationReady)) {
        pending = pending_;
        wipeLineaMicraSettings(pending_.credentials);
        pending_.present = false;
        if (pendingObservation) observationSchedule_.observationStarted(now);
        observationActive_.store(pendingObservation,
                                 std::memory_order_release);
        active_ = true;
        haveRequest = true;
      } else if (staEligible && !shotActive_.load(std::memory_order_acquire) &&
                 !scaleConnecting_.load(std::memory_order_acquire) &&
                 desiredTemperature_.present &&
                 static_cast<int32_t>(now - desiredTemperature_.retryAtMs) >= 0) {
        pending.request = desiredTemperature_.request;
        temperatureMachineConfigGeneration =
            desiredTemperature_.machineConfigGeneration;
        active_ = true;
        haveRequest = true;
        haveTemperature = true;
        temperatureActive_.store(true, std::memory_order_release);
      } else if (observationReady && config_.accountConfigured &&
                 (config_.options & LINEA_MICRA_OBSERVE_STATE) != 0 &&
                 observationSchedule_.automaticDue(now)) {
        pending.request.requestId = nextAutomaticRequestId_++;
        pending.request.configGeneration = configGeneration_;
        pending.request.type = LineaMicraRequestType::OBSERVE_STATE;
        pending.identityGeneration = identityGeneration_;
        observationSchedule_.observationStarted(now);
        observationActive_.store(true, std::memory_order_release);
        active_ = true;
        haveRequest = true;
      }
    }
    if (haveRequest) {
      abortRequested_.store(false, std::memory_order_release);
      if (haveTemperature) {
        executeTemperatureApplication(pending.request,
                                      temperatureMachineConfigGeneration);
        temperatureActive_.store(false, std::memory_order_release);
      } else {
        execute(pending);
        observationActive_.store(false, std::memory_order_release);
      }
      wipeLineaMicraSettings(pending.credentials);
      TaskLockGuard lock(mux_);
      active_ = false;
      continue;
    }
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(250));
  }
}

bool ShotStopperMicraService::networkEligible(LineaMicraError &error) const {
  if (apActive_.load(std::memory_order_acquire)) {
    error = LineaMicraError::AP_MODE;
    return false;
  }
  if (!staConnected_.load(std::memory_order_acquire)) {
    error = LineaMicraError::NO_STA;
    return false;
  }
  timeval now{};
  gettimeofday(&now, nullptr);
  if (now.tv_sec < 1700000000L) {
    error = LineaMicraError::CLOCK_UNSYNCED;
    return false;
  }
  error = LineaMicraError::NONE;
  return true;
}

bool ShotStopperMicraService::temperatureEligible(
    LineaMicraError &error) const {
  if (!networkEligible(error)) return false;
  if (shotActive_.load(std::memory_order_acquire) ||
      scaleConnecting_.load(std::memory_order_acquire)) {
    error = LineaMicraError::CANCELED;
    return false;
  }
  return true;
}

void ShotStopperMicraService::execute(PendingRequest &pending) {
  LineaMicraStatus status;
  {
    TaskLockGuard lock(mux_);
    status = published_;
  }
  status.requestId = pending.request.requestId;
  status.configGeneration = pending.request.configGeneration;
  status.identityGeneration = pending.identityGeneration;
  status.error = LineaMicraError::NONE;
  status.transportStatus = 0;
  status.httpStatus = 0;
  LineaMicraError gateError = LineaMicraError::NONE;
  if (!networkEligible(gateError)) {
    if (pending.request.type == LineaMicraRequestType::OBSERVE_STATE) {
      deferObservation(pending, status, gateError);
      return;
    }
    fail(status, gateError);
    scheduleAutomatic(millis(), true);
    return;
  }
  if (pending.request.type == LineaMicraRequestType::OBSERVE_STATE &&
      shotActive_.load(std::memory_order_acquire)) {
    deferObservation(pending, status, LineaMicraError::CANCELED);
    return;
  }
  if (!ensureWorkBuffer()) {
    fail(status, LineaMicraError::HTTP_ERROR);
    scheduleAutomatic(
        millis(), pending.request.type == LineaMicraRequestType::CONNECT);
    return;
  }
  work_->transportStatus = 0;
  work_->httpStatus = 0;
  const bool connecting = pending.request.type == LineaMicraRequestType::CONNECT;
  const bool success = connecting ? executeConnect(pending)
                                  : executeObservation(pending);
  if (!success) scheduleAutomatic(millis(), connecting);
  if (connecting) releaseWorkBuffer();
}

bool ShotStopperMicraService::temperatureRequestCurrent(
    const LineaMicraRequest &request,
    uint32_t machineConfigGeneration) const {
  TaskLockGuard lock(mux_);
  return desiredTemperature_.present &&
         desiredTemperature_.machineConfigGeneration ==
             machineConfigGeneration &&
         sameTemperatureRequest(desiredTemperature_.request, request) &&
         configGeneration_ == machineConfigGeneration &&
         config_.accountConfigured &&
         (config_.options & LINEA_MICRA_APPLY_TEMPERATURE) != 0;
}

bool ShotStopperMicraService::identityCurrent(
    uint32_t identityGeneration) const {
  TaskLockGuard lock(mux_);
  return identityGeneration == identityGeneration_;
}

bool ShotStopperMicraService::observationCurrent(
    uint32_t identityGeneration) const {
  TaskLockGuard lock(mux_);
  return identityGeneration == identityGeneration_ &&
         config_.accountConfigured &&
         (config_.options & LINEA_MICRA_OBSERVE_STATE) != 0;
}

void ShotStopperMicraService::deferTemperature(
    const LineaMicraRequest &request, LineaMicraError error, uint32_t delayMs,
    bool retryable) {
  TaskLockGuard lock(mux_);
  if (!desiredTemperature_.present ||
      !sameTemperatureRequest(desiredTemperature_.request, request)) {
    return;
  }
  published_.temperatureCommandAccepted =
      desiredTemperature_.commandAccepted;
  published_.temperatureRetryable = retryable;
  if (retryable) {
    desiredTemperature_.retryAtMs = millis() + delayMs;
  } else {
    desiredTemperature_ = {};
  }
  published_.temperatureState = error == LineaMicraError::CANCELED
                                    ? LineaMicraTemperatureState::CANCELED
                                    : LineaMicraTemperatureState::FAILED;
  published_.temperatureError = error;
  if (work_ != nullptr) {
    published_.temperatureTransportStatus = work_->transportStatus;
    published_.temperatureHttpStatus = work_->httpStatus;
  }
}

bool ShotStopperMicraService::executeTemperatureApplication(
    const LineaMicraRequest &request,
    uint32_t machineConfigGeneration) {
  LineaMicraPersistedSettings settings;
  bool commandAccepted = false;
  {
    TaskLockGuard lock(mux_);
    if (!desiredTemperature_.present ||
        desiredTemperature_.machineConfigGeneration !=
            machineConfigGeneration ||
        !sameTemperatureRequest(desiredTemperature_.request, request) ||
        configGeneration_ != machineConfigGeneration) {
      return true;
    }
    settings = config_;
    commandAccepted = desiredTemperature_.commandAccepted;
    published_.requestId = request.requestId;
    published_.requestedTargetDeciC = request.targetDeciC;
    published_.temperatureState = LineaMicraTemperatureState::RUNNING;
    published_.temperatureError = LineaMicraError::NONE;
    published_.temperatureCommandAccepted = commandAccepted;
    published_.temperatureRetryable = true;
  }
  if (!ensureWorkBuffer()) {
    wipeLineaMicraSettings(settings);
    deferTemperature(request, LineaMicraError::HTTP_ERROR,
                     micra_timing::kExhaustedCooldownMs);
    return false;
  }

  bool commandAttempted = false;
  int32_t commandTransportStatus = 0;
  uint16_t commandHttpStatus = 0;
  for (size_t attempt = 0; attempt < micra_timing::kMaxAttempts; ++attempt) {
    LineaMicraError gateError = LineaMicraError::NONE;
    if (!temperatureRequestCurrent(request, machineConfigGeneration)) {
      wipeLineaMicraSettings(settings);
      return true;
    }
    if (!temperatureEligible(gateError)) {
      wipeLineaMicraSettings(settings);
      deferTemperature(request, gateError,
                       gateError == LineaMicraError::CANCELED
                           ? micra_timing::kGateRetryMs
                           : micra_timing::kExhaustedCooldownMs);
      return false;
    }

    LineaMicraStatus verification;
    {
      TaskLockGuard lock(mux_);
      verification = published_;
    }
    bool success = false;
    if (ensureSession(settings, false) &&
        temperatureRequestCurrent(request, machineConfigGeneration)) {
      if (!commandAccepted) {
        commandAttempted = true;
        commandAccepted = writeTemperature(settings, request.targetDeciC);
        commandTransportStatus = work_->transportStatus;
        commandHttpStatus = work_->httpStatus;
        if (commandAccepted) {
          TaskLockGuard lock(mux_);
          if (desiredTemperature_.present &&
              sameTemperatureRequest(desiredTemperature_.request, request)) {
            desiredTemperature_.commandAccepted = true;
            published_.temperatureCommandAccepted = true;
          }
        }
      }
      success = commandAccepted &&
                temperatureRequestCurrent(request, machineConfigGeneration) &&
                readDashboard(settings, verification) &&
                verification.targetValid &&
                verification.targetDeciC == request.targetDeciC;
    }
    if (success) {
      wipeLineaMicraSettings(settings);
      TaskLockGuard lock(mux_);
      if (desiredTemperature_.present &&
          sameTemperatureRequest(desiredTemperature_.request, request)) {
        desiredTemperature_ = {};
        published_.targetValid = true;
        published_.targetDeciC = request.targetDeciC;
        published_.temperatureAtMs = millis();
        published_.appliedTargetDeciC = request.targetDeciC;
        published_.temperatureState = LineaMicraTemperatureState::CONFIRMED;
        published_.temperatureError = LineaMicraError::NONE;
        published_.temperatureTransportStatus = work_->transportStatus;
        published_.temperatureHttpStatus = work_->httpStatus;
        published_.temperatureCommandAccepted = true;
        published_.temperatureRetryable = false;
      }
      return true;
    }

    if (!temperatureRequestCurrent(request, machineConfigGeneration)) {
      wipeLineaMicraSettings(settings);
      return true;
    }
    if (!temperatureEligible(gateError)) {
      wipeLineaMicraSettings(settings);
      deferTemperature(request, gateError,
                       micra_timing::kGateRetryMs);
      return false;
    }
    if (work_->httpStatus == 401) {
      secureWipe(work_->accessToken, sizeof(work_->accessToken));
      work_->accessTokenIssuedAtMs = 0;
    }
    if (!commandAccepted && commandAttempted &&
        commandTransportStatus == ESP_OK && commandHttpStatus != 0 &&
        !lineaMicraTemperatureHttpRetryable(commandHttpStatus)) {
      break;
    }
    if (attempt + 1U >= micra_timing::kMaxAttempts) break;
    (void)ulTaskNotifyTake(
        pdTRUE, pdMS_TO_TICKS(micra_timing::kRetryDelaysMs[attempt]));
  }

  wipeLineaMicraSettings(settings);
  const uint16_t failureHttpStatus =
      commandAttempted ? commandHttpStatus : work_->httpStatus;
  const int32_t failureTransportStatus =
      commandAttempted ? commandTransportStatus : work_->transportStatus;
  const LineaMicraError error =
      commandAccepted
          ? LineaMicraError::UNCONFIRMED
          : failureHttpStatus == 401
                ? LineaMicraError::INVALID_AUTH
                : failureTransportStatus == ESP_OK &&
                          failureHttpStatus != 0 &&
                          !lineaMicraTemperatureHttpRetryable(failureHttpStatus)
                      ? LineaMicraError::REJECTED
                      : LineaMicraError::HTTP_ERROR;
  deferTemperature(request, error, micra_timing::kExhaustedCooldownMs,
                   lineaMicraTemperatureCycleRetryable(error));
  return false;
}

bool ShotStopperMicraService::executeConnect(PendingRequest &pending) {
  LineaMicraStatus status;
  {
    TaskLockGuard lock(mux_);
    status = published_;
  }
  status.requestId = pending.request.requestId;
  status.identityGeneration = pending.identityGeneration;
  status.phase = LineaMicraPhase::AUTHENTICATING;
  status.error = LineaMicraError::NONE;
  publish(status);
  if (!identityCurrent(pending.identityGeneration)) return true;
  if (!generateInstallationKey(pending.credentials)) {
    fail(status, LineaMicraError::HTTP_ERROR);
    return false;
  }
  if (!ensureSession(pending.credentials, true)) {
    LineaMicraError error = LineaMicraError::NONE;
    if (networkEligible(error)) {
      error = work_ != nullptr && work_->httpStatus == 401
                  ? LineaMicraError::INVALID_AUTH
                  : LineaMicraError::HTTP_ERROR;
    }
    fail(status, error);
    return false;
  }
  if (!identityCurrent(pending.identityGeneration)) return true;
  status.phase = LineaMicraPhase::LISTING;
  publish(status);
  LineaMicraDiscoverySnapshot found;
  found.requestId = pending.request.requestId;
  if (!listMachines(pending.credentials, found)) {
    LineaMicraError error = LineaMicraError::NONE;
    if (networkEligible(error)) {
      error = work_ != nullptr && work_->httpStatus == 401
                  ? LineaMicraError::INVALID_AUTH
                  : LineaMicraError::HTTP_ERROR;
    }
    fail(status, error);
    return false;
  }
  if (!identityCurrent(pending.identityGeneration)) return true;
  if (found.count == 0) {
    fail(status, LineaMicraError::NO_MACHINES);
    return false;
  }
  {
    TaskLockGuard lock(mux_);
    if (pending.identityGeneration != identityGeneration_) return true;
    candidate_ = pending.credentials;
    candidate_.options = config_.options;
    discovery_ = found;
    ++discovery_.generation;
  }
  status.phase = LineaMicraPhase::CONFIRMED;
  status.error = LineaMicraError::NONE;
  status.quality = LineaMicraObservationQuality::UNCONFIGURED;
  publish(status);
  return true;
}

bool ShotStopperMicraService::executeObservation(PendingRequest &pending) {
  LineaMicraPersistedSettings settings;
  LineaMicraStatus status;
  uint32_t powerGeneration = 0;
  {
    TaskLockGuard lock(mux_);
    settings = config_;
    status = published_;
    powerGeneration = powerState_.generation();
  }
  status.requestId = pending.request.requestId;
  status.identityGeneration = pending.identityGeneration;
  if (!observationCurrent(pending.identityGeneration)) return true;
  status.phase = LineaMicraPhase::RUNNING;
  status.error = LineaMicraError::NONE;
  publish(status);
  bool success = false;
  bool sessionRenewed = false;
  const bool initialSample = status.sampleAtMs == 0;
  for (size_t attempt = 0; attempt < micra_timing::kMaxAttempts; ++attempt) {
    if (!observationCurrent(pending.identityGeneration)) {
      wipeLineaMicraSettings(settings);
      return true;
    }
    if (shotActive_.load(std::memory_order_acquire)) {
      wipeLineaMicraSettings(settings);
      deferObservation(pending, status, LineaMicraError::CANCELED);
      return true;
    }
    LineaMicraError gateError = LineaMicraError::NONE;
    if (!networkEligible(gateError)) {
      wipeLineaMicraSettings(settings);
      deferObservation(pending, status, gateError);
      return true;
    }
    success = ensureSession(settings, false, &sessionRenewed) &&
              ((sessionRenewed && !initialSample) ||
               readDashboard(settings, status));
    if (success) break;
    if (!observationCurrent(pending.identityGeneration)) {
      wipeLineaMicraSettings(settings);
      return true;
    }
    if (shotActive_.load(std::memory_order_acquire)) continue;
    if (!networkEligible(gateError)) {
      wipeLineaMicraSettings(settings);
      deferObservation(pending, status, gateError);
      return true;
    }
    if (work_ != nullptr && work_->httpStatus == 401) {
      secureWipe(work_->accessToken, sizeof(work_->accessToken));
      work_->accessTokenIssuedAtMs = 0;
    }
    if (attempt + 1U >= micra_timing::kMaxAttempts) break;
    status.phase = LineaMicraPhase::BACKOFF;
    publish(status);
    (void)ulTaskNotifyTake(pdTRUE,
                          pdMS_TO_TICKS(micra_timing::kRetryDelaysMs[attempt]));
    status.phase = LineaMicraPhase::RUNNING;
    publish(status);
  }
  wipeLineaMicraSettings(settings);
  if (!observationCurrent(pending.identityGeneration)) return true;
  if (!success) {
    LineaMicraError error = LineaMicraError::NONE;
    if (networkEligible(error)) {
      error = work_ != nullptr && work_->httpStatus == 401
                  ? LineaMicraError::INVALID_AUTH
                  : LineaMicraError::HTTP_ERROR;
    }
    fail(status, error);
    return false;
  }
  if (sessionRenewed && !initialSample) {
    status.phase = LineaMicraPhase::IDLE;
    status.error = LineaMicraError::NONE;
    publish(status);
    scheduleAutomatic(millis(), false);
    return true;
  }
  status.phase = LineaMicraPhase::CONFIRMED;
  status.error = LineaMicraError::NONE;
  status.quality = status.observedMode == LineaMicraObservedMode::UNSUPPORTED
                       ? LineaMicraObservationQuality::UNSUPPORTED
                       : LineaMicraObservationQuality::CURRENT;
  status.sampleAtMs = millis();
  publishObservation(status, powerGeneration);
  scheduleAutomatic(status.sampleAtMs, false);
  return true;
}

void ShotStopperMicraService::publish(const LineaMicraStatus &status) {
  TaskLockGuard lock(mux_);
  if (status.identityGeneration != identityGeneration_) return;
  LineaMicraStatus next = status;
  preserveTemperatureStatus(published_, next);
  published_ = next;
  published_.accountConfigured = config_.accountConfigured;
}

void ShotStopperMicraService::publishObservation(
    const LineaMicraStatus &status, uint32_t powerGeneration) {
  TaskLockGuard lock(mux_);
  if (status.identityGeneration != identityGeneration_ ||
      !config_.accountConfigured ||
      (config_.options & LINEA_MICRA_OBSERVE_STATE) == 0) {
    return;
  }
  LineaMicraStatus next = status;
  preserveTemperatureStatus(published_, next);
  if (!powerState_.acceptAuthoritative(powerGeneration)) {
    next.sampleAtMs = published_.sampleAtMs;
    next.powerState = published_.powerState;
    next.observedMode = published_.observedMode;
    next.quality = published_.quality;
    next.effectiveOn = published_.effectiveOn;
  }
  published_ = next;
  published_.accountConfigured = config_.accountConfigured;
}

void ShotStopperMicraService::deferObservation(
    const PendingRequest &pending, LineaMicraStatus status,
    LineaMicraError reason) {
  TaskLockGuard lock(mux_);
  if (pending.identityGeneration != identityGeneration_ ||
      !config_.accountConfigured ||
      (config_.options & LINEA_MICRA_OBSERVE_STATE) == 0) {
    return;
  }
  if (!pending_.present) pending_ = pending;
  pending_.present = true;
  status.phase = reason == LineaMicraError::CANCELED
                     ? LineaMicraPhase::PAUSED
                     : LineaMicraPhase::QUEUED;
  status.error = LineaMicraError::NONE;
  status.shotPaused = reason == LineaMicraError::CANCELED;
  preserveTemperatureStatus(published_, status);
  published_ = status;
  published_.accountConfigured = config_.accountConfigured;
}

void ShotStopperMicraService::fail(LineaMicraStatus &status,
                                   LineaMicraError error) {
  const LineaMicraPhase failedStage = status.phase;
  status.phase = LineaMicraPhase::FAILED;
  status.error = error;
  status.quality = LineaMicraObservationQuality::COMMUNICATION_ERROR;
  status.powerState = LineaMicraPowerState::UNKNOWN;
  status.effectiveOn = true;
  if (work_ != nullptr) {
    status.httpStatus = work_->httpStatus;
    status.transportStatus = work_->transportStatus;
  }
  publish(status);
  serialTraceCategoryf(LogLevel::WARNING, DebugCategory::NETWORK,
                       "Micra cloud request failed stage=%s error=%s http=%u raw=%ld",
                       lineaMicraPhaseName(failedStage),
                       lineaMicraErrorName(error),
                       static_cast<unsigned>(status.httpStatus),
                       static_cast<long>(status.transportStatus));
}

void ShotStopperMicraService::scheduleAutomatic(uint32_t now, bool failed) {
  TaskLockGuard lock(mux_);
  if (failed) {
    observationSchedule_.dueNow(now + micra_timing::kExhaustedCooldownMs);
  } else {
    observationSchedule_.scheduleNext(now);
  }
}

bool ShotStopperMicraService::generateInstallationKey(
    LineaMicraPersistedSettings &settings) {
  psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
  setKeyAttributes(attributes);
  mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
  const psa_status_t generated = psa_generate_key(&attributes, &key);
  psa_reset_key_attributes(&attributes);
  if (generated != PSA_SUCCESS) return false;
  size_t length = 0;
  const psa_status_t exported =
      psa_export_key(key, settings.installationPrivateKey,
                     sizeof(settings.installationPrivateKey), &length);
  (void)psa_destroy_key(key);
  return exported == PSA_SUCCESS &&
         length == sizeof(settings.installationPrivateKey);
}

bool ShotStopperMicraService::ensureSession(
    LineaMicraPersistedSettings &settings, bool registerKey, bool *renewed) {
  if (renewed != nullptr) *renewed = false;
  if (work_ == nullptr) return false;
  if (registerKey) {
    clearSession();
    return registerInstallation(settings) && signIn(settings);
  }
  const uint32_t age = millis() - work_->accessTokenIssuedAtMs;
  if (work_->accessToken[0] != '\0' &&
      !micra_timing::accessTokenRefreshDue(age)) return true;
  const bool ok = work_->refreshToken[0] != '\0'
                      ? refreshToken(settings) || signIn(settings)
                      : signIn(settings);
  if (ok && renewed != nullptr) *renewed = true;
  return ok;
}

bool ShotStopperMicraService::registerInstallation(
    const LineaMicraPersistedSettings &settings) {
  if (!ensureIoBuffer()) return false;
  const bool ok =
      request(settings, kRegisterUrl, HTTP_METHOD_POST, nullptr, false, true);
  releaseIoBuffer();
  return ok;
}

bool ShotStopperMicraService::signIn(
    const LineaMicraPersistedSettings &settings) {
  if (!ensureIoBuffer()) return false;
  char username[2 * LINEA_MICRA_USERNAME_CAPACITY];
  char password[2 * LINEA_MICRA_PASSWORD_CAPACITY];
  if (!jsonEscape(settings.username, username, sizeof(username)) ||
      !jsonEscape(settings.password, password, sizeof(password))) {
    secureWipe(username, sizeof(username));
    secureWipe(password, sizeof(password));
    releaseIoBuffer();
    return false;
  }
  const int length = snprintf(io_->body, sizeof(io_->body),
                              "{\"username\":\"%s\",\"password\":\"%s\"}",
                              username, password);
  secureWipe(username, sizeof(username));
  secureWipe(password, sizeof(password));
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(io_->body)) {
    releaseIoBuffer();
    return false;
  }
  if (!request(settings, kSignInUrl, HTTP_METHOD_POST, io_->body, false)) {
    releaseIoBuffer();
    return false;
  }
  cJSON *root = parseResponse(io_->response);
  const char *access = root == nullptr ? nullptr : jsonText(root, "accessToken");
  const char *refresh = root == nullptr ? nullptr : jsonText(root, "refreshToken");
  const bool ok = access != nullptr && refresh != nullptr &&
                  secureCopy(work_->accessToken, sizeof(work_->accessToken),
                             access) &&
                  secureCopy(work_->refreshToken, sizeof(work_->refreshToken),
                             refresh);
  if (root != nullptr) cJSON_Delete(root);
  work_->responseUsed = 0;
  releaseIoBuffer();
  if (!ok) {
    secureWipe(work_->accessToken, sizeof(work_->accessToken));
    secureWipe(work_->refreshToken, sizeof(work_->refreshToken));
    return false;
  }
  work_->accessTokenIssuedAtMs = millis();
  return true;
}

bool ShotStopperMicraService::refreshToken(
    const LineaMicraPersistedSettings &settings) {
  if (work_->refreshToken[0] == '\0') return false;
  if (!ensureIoBuffer()) return false;
  cJSON *body = cJSON_CreateObject();
  const bool bodyOk = body != nullptr &&
      cJSON_AddStringToObject(body, "username", settings.username) != nullptr &&
      cJSON_AddStringToObject(body, "refreshToken", work_->refreshToken) != nullptr &&
      cJSON_PrintPreallocated(body, io_->body, sizeof(io_->body), false);
  if (body != nullptr) cJSON_Delete(body);
  if (!bodyOk) {
    releaseIoBuffer();
    return false;
  }
  if (!request(settings, kRefreshUrl, HTTP_METHOD_POST, io_->body, false)) {
    releaseIoBuffer();
    return false;
  }
  cJSON *root = parseResponse(io_->response);
  const char *access = root == nullptr ? nullptr : jsonText(root, "accessToken");
  const char *newRefresh =
      root == nullptr ? nullptr : jsonText(root, "refreshToken");
  const bool ok = access != nullptr && newRefresh != nullptr &&
                  secureCopy(work_->accessToken, sizeof(work_->accessToken),
                             access) &&
                  secureCopy(work_->refreshToken, sizeof(work_->refreshToken),
                             newRefresh);
  if (root != nullptr) cJSON_Delete(root);
  work_->responseUsed = 0;
  releaseIoBuffer();
  if (ok) work_->accessTokenIssuedAtMs = millis();
  return ok;
}

bool ShotStopperMicraService::listMachines(
    const LineaMicraPersistedSettings &settings,
    LineaMicraDiscoverySnapshot &result) {
  if (!ensureIoBuffer()) return false;
  if (!request(settings, kThingsUrl, HTTP_METHOD_GET, nullptr, true)) {
    releaseIoBuffer();
    return false;
  }
  cJSON *root = parseResponse(io_->response);
  if (!cJSON_IsArray(root)) {
    if (root != nullptr) cJSON_Delete(root);
    work_->responseUsed = 0;
    releaseIoBuffer();
    return false;
  }
  const cJSON *thing = nullptr;
  cJSON_ArrayForEach(thing, root) {
    if (result.count >= LINEA_MICRA_MAX_ACCOUNT_MACHINES ||
        !cJSON_IsObject(thing)) {
      continue;
    }
    const char *type = jsonText(thing, "type");
    const char *model = jsonText(thing, "modelCode");
    const char *serial = jsonText(thing, "serialNumber");
    const char *name = jsonText(thing, "name");
    if (type == nullptr || strcmp(type, "CoffeeMachine") != 0 ||
        model == nullptr || strcmp(model, "LINEAMICRA") != 0 ||
        serial == nullptr || !validLineaMicraSerial(serial)) {
      continue;
    }
    LineaMicraMachineSummary &machine = result.machines[result.count];
    if (!secureCopy(machine.serial, sizeof(machine.serial), serial) ||
        !secureCopy(machine.name, sizeof(machine.name),
                    name == nullptr ? serial : name)) {
      continue;
    }
    ++result.count;
  }
  cJSON_Delete(root);
  work_->responseUsed = 0;
  releaseIoBuffer();
  return true;
}

bool ShotStopperMicraService::readDashboard(
    const LineaMicraPersistedSettings &settings, LineaMicraStatus &result) {
  if (!ensureIoBuffer()) return false;
  char url[sizeof(kApiRoot) + LINEA_MICRA_SERIAL_CAPACITY + 24];
  const int length = snprintf(url, sizeof(url), "%s/things/%s/dashboard",
                              kApiRoot, settings.selectedSerial);
  if (length <= 0 || static_cast<size_t>(length) >= sizeof(url) ||
      !request(settings, url, HTTP_METHOD_GET, nullptr, true)) {
    releaseIoBuffer();
    return false;
  }
  cJSON *root = parseResponse(io_->response);
  cJSON *widgets =
      root == nullptr ? nullptr : cJSON_GetObjectItemCaseSensitive(root, "widgets");
  if (!cJSON_IsArray(widgets)) {
    if (root != nullptr) cJSON_Delete(root);
    work_->responseUsed = 0;
    releaseIoBuffer();
    return false;
  }
  LineaMicraObservedMode mode = LineaMicraObservedMode::NONE;
  bool targetValid = false;
  uint16_t targetDeciC = 0;
  const cJSON *widget = nullptr;
  cJSON_ArrayForEach(widget, widgets) {
    const char *code = jsonText(widget, "code");
    cJSON *output = cJSON_GetObjectItemCaseSensitive(widget, "output");
    if (code == nullptr || !cJSON_IsObject(output)) continue;
    if (strcmp(code, "CMMachineStatus") == 0) {
      mode = parseMode(jsonText(output, "mode"));
    } else if (strcmp(code, "CMCoffeeBoiler") == 0) {
      const cJSON *target =
          cJSON_GetObjectItemCaseSensitive(output, "targetTemperature");
      if (cJSON_IsNumber(target)) {
        const long rounded = lround(target->valuedouble * 10.0);
        if (rounded >= 0 && rounded <= UINT16_MAX) {
          targetDeciC = static_cast<uint16_t>(rounded);
          targetValid = true;
        }
      }
    }
  }
  cJSON_Delete(root);
  work_->responseUsed = 0;
  releaseIoBuffer();
  if (mode == LineaMicraObservedMode::NONE) return false;
  result.observedMode = mode;
  result.powerState = lineaMicraPowerStateForMode(mode);
  result.effectiveOn = result.powerState != LineaMicraPowerState::OFF;
  result.targetValid = targetValid;
  result.targetDeciC = targetDeciC;
  result.temperatureAtMs = targetValid ? millis() : 0;
  return true;
}

bool ShotStopperMicraService::writeTemperature(
    const LineaMicraPersistedSettings &settings, uint16_t targetDeciC) {
  if (!ensureIoBuffer()) return false;
  char url[sizeof(kApiRoot) + LINEA_MICRA_SERIAL_CAPACITY + 80];
  const int urlLength = snprintf(
      url, sizeof(url),
      "%s/things/%s/command/"
      "CoffeeMachineSettingCoffeeBoilerTargetTemperature",
      kApiRoot, settings.selectedSerial);
  const int bodyLength = snprintf(
      io_->body, sizeof(io_->body),
      "{\"boilerIndex\":1,\"targetTemperature\":%u.%u}",
      static_cast<unsigned>(targetDeciC / 10U),
      static_cast<unsigned>(targetDeciC % 10U));
  work_->transportStatus = 0;
  work_->httpStatus = 0;
  const bool ok = urlLength > 0 &&
                  static_cast<size_t>(urlLength) < sizeof(url) &&
                  bodyLength > 0 &&
                  static_cast<size_t>(bodyLength) < sizeof(io_->body) &&
                  request(settings, url, HTTP_METHOD_POST, io_->body, true);
  const bool accepted =
      ok || (work_->transportStatus == ESP_OK && work_->httpStatus >= 200 &&
             work_->httpStatus < 300);
  releaseIoBuffer();
  return accepted;
}

bool ShotStopperMicraService::applySignedHeaders(
    const LineaMicraPersistedSettings &settings) {
  char id[37];
  char publicB64[128];
  uint8_t secret[32];
  if (!installationId(settings, id) ||
      !deriveInstallationSecret(settings, id, secret, publicB64)) {
    secureWipe(secret, sizeof(secret));
    return false;
  }
  uint8_t nonceBytes[16];
  esp_fill_random(nonceBytes, sizeof(nonceBytes));
  char nonce[37];
  formatUuid(nonceBytes, nonce);
  secureWipe(nonceBytes, sizeof(nonceBytes));
  timeval current{};
  gettimeofday(&current, nullptr);
  const unsigned long long timestamp =
      static_cast<unsigned long long>(current.tv_sec) * 1000ULL +
      static_cast<unsigned long long>(current.tv_usec / 1000);
  char proofInput[96];
  const int proofInputLength = snprintf(proofInput, sizeof(proofInput),
                                        "%s.%s.%llu", id, nonce, timestamp);
  char proof[48];
  char signatureInput[160];
  bool ok = proofInputLength > 0 &&
            static_cast<size_t>(proofInputLength) < sizeof(proofInput) &&
            requestProof(proofInput, secret, proof);
  const int signatureLength =
      ok ? snprintf(signatureInput, sizeof(signatureInput), "%s.%s", proofInput,
                    proof)
         : -1;
  char signature[104];
  ok = ok && signatureLength > 0 &&
       static_cast<size_t>(signatureLength) < sizeof(signatureInput) &&
       signRequest(settings, signatureInput, signature);
  char timestampText[24];
  snprintf(timestampText, sizeof(timestampText), "%llu", timestamp);
  if (ok) {
    ok = esp_http_client_set_header(work_->client, "X-App-Installation-Id", id) ==
             ESP_OK &&
         esp_http_client_set_header(work_->client, "X-Timestamp", timestampText) ==
             ESP_OK &&
         esp_http_client_set_header(work_->client, "X-Nonce", nonce) == ESP_OK &&
         esp_http_client_set_header(work_->client, "X-Request-Signature",
                                    signature) == ESP_OK;
  }
  secureWipe(secret, sizeof(secret));
  secureWipe(publicB64, sizeof(publicB64));
  secureWipe(proofInput, sizeof(proofInput));
  secureWipe(proof, sizeof(proof));
  secureWipe(signatureInput, sizeof(signatureInput));
  secureWipe(signature, sizeof(signature));
  return ok;
}

bool ShotStopperMicraService::request(
    const LineaMicraPersistedSettings &settings, const char *url,
    esp_http_client_method_t method, const char *body, bool authenticated,
    bool installationInit) {
  if (work_ == nullptr || io_ == nullptr || url == nullptr) return false;
  if (work_->client == nullptr) {
    esp_http_client_config_t config{};
    config.url = kApiRoot;
    config.timeout_ms = static_cast<int>(micra_timing::kHttpTimeoutMs);
    config.disable_auto_redirect = true;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.event_handler = httpEvent;
    config.user_data = this;
    config.buffer_size = 1024;
    config.buffer_size_tx = 1024;
    config.save_client_session = true;
    work_->client = esp_http_client_init(&config);
    if (work_->client == nullptr) return false;
    if (esp_http_client_set_header(work_->client, "Accept", "application/json") !=
            ESP_OK ||
        esp_http_client_set_header(work_->client, "User-Agent",
                                   "AdvancedShotStopper/1") != ESP_OK) {
      esp_http_client_cleanup(work_->client);
      work_->client = nullptr;
      return false;
    }
  }
  // Blocking esp_http_client_perform sends the complete POST body before its
  // response callback runs, so those mutually exclusive phases share storage.
  const bool bodySharesResponse = body == io_->body;
  work_->responseUsed = 0;
  if (!bodySharesResponse) io_->response[0] = '\0';
  work_->responseOverflow = false;
  work_->httpStatus = 0;
  work_->transportStatus = 0;
  clearRequestState();
  RequestStateGuard requestState{*this};
  if (esp_http_client_set_url(work_->client, url) != ESP_OK ||
      esp_http_client_set_method(work_->client, method) != ESP_OK) {
    return false;
  }
  if (installationInit) {
    char id[37];
    char publicB64[128];
    uint8_t secret[32];
    uint8_t publicDer[91];
    uint8_t publicHash[32];
    char publicHashB64[48];
    char base[224];
    char proof[48];
    bool ok = installationId(settings, id) &&
              deriveInstallationSecret(settings, id, secret, publicB64) &&
              publicKeyDer(settings, publicDer) &&
              sha256(publicDer, sizeof(publicDer), publicHash) &&
              base64Encode(publicHash, sizeof(publicHash), publicHashB64,
                           sizeof(publicHashB64));
    const int baseLength =
        ok ? snprintf(base, sizeof(base), "%s.%s", id, publicHashB64) : -1;
    ok = ok && baseLength > 0 && static_cast<size_t>(baseLength) < sizeof(base) &&
         requestProof(base, secret, proof);
    const int bodyLength =
        ok ? snprintf(io_->body, sizeof(io_->body), "{\"pk\":\"%s\"}",
                      publicB64)
           : -1;
    ok = ok && bodyLength > 0 &&
         static_cast<size_t>(bodyLength) < sizeof(io_->body) &&
         esp_http_client_set_header(work_->client, "X-App-Installation-Id", id) ==
             ESP_OK &&
         esp_http_client_set_header(work_->client, "X-Request-Proof", proof) ==
             ESP_OK;
    secureWipe(secret, sizeof(secret));
    secureWipe(publicDer, sizeof(publicDer));
    secureWipe(publicHash, sizeof(publicHash));
    secureWipe(publicHashB64, sizeof(publicHashB64));
    secureWipe(base, sizeof(base));
    secureWipe(proof, sizeof(proof));
    if (!ok) {
      secureWipe(io_->body, sizeof(io_->body));
      return false;
    }
    body = io_->body;
  } else if (!applySignedHeaders(settings)) {
    return false;
  }
  if (authenticated) {
    static_assert(kResponseCapacity >= kTokenCapacity + 8);
    const int length = snprintf(io_->response, sizeof(io_->response),
                                "Bearer %s", work_->accessToken);
    const bool ok = length > 0 &&
                    static_cast<size_t>(length) < sizeof(io_->response) &&
                    esp_http_client_set_header(work_->client, "Authorization",
                                               io_->response) == ESP_OK;
    if (length > 0 && static_cast<size_t>(length) < sizeof(io_->response)) {
      secureWipe(io_->response, static_cast<size_t>(length) + 1U);
    } else {
      io_->response[0] = '\0';
    }
    if (!ok) return false;
  }
  if (body != nullptr &&
      (esp_http_client_set_header(work_->client, "Content-Type",
                                  "application/json") != ESP_OK ||
       esp_http_client_set_post_field(work_->client, body, strlen(body)) !=
           ESP_OK)) {
    return false;
  }
  {
    TaskLockGuard lock(clientMux_);
    activeClient_ = work_->client;
  }
  LineaMicraError gateError = LineaMicraError::NONE;
  if (!networkEligible(gateError) ||
      shotActive_.load(std::memory_order_acquire) ||
      scaleConnecting_.load(std::memory_order_acquire) ||
      abortRequested_.load(std::memory_order_acquire)) {
    TaskLockGuard lock(clientMux_);
    activeClient_ = nullptr;
    if (installationInit) secureWipe(io_->body, sizeof(io_->body));
    return false;
  }
  const esp_err_t performed = esp_http_client_perform(work_->client);
  {
    TaskLockGuard lock(clientMux_);
    activeClient_ = nullptr;
  }
  work_->transportStatus = performed;
  work_->httpStatus = static_cast<uint16_t>(
      esp_http_client_get_status_code(work_->client));
  if (work_->responseUsed < sizeof(io_->response)) {
    io_->response[work_->responseUsed] = '\0';
  }
  const bool ok = performed == ESP_OK && !work_->responseOverflow &&
                  work_->httpStatus >= 200 && work_->httpStatus < 300;
  if (installationInit) {
    secureWipe(io_->response, sizeof(io_->response));
    work_->responseUsed = 0;
  }
  return ok;
}

void ShotStopperMicraService::clearRequestState() {
  if (work_ == nullptr || work_->client == nullptr) return;
  (void)esp_http_client_delete_header(work_->client, "Authorization");
  (void)esp_http_client_delete_header(work_->client, "Content-Type");
  (void)esp_http_client_delete_header(work_->client,
                                      "X-App-Installation-Id");
  (void)esp_http_client_delete_header(work_->client, "X-Timestamp");
  (void)esp_http_client_delete_header(work_->client, "X-Nonce");
  (void)esp_http_client_delete_header(work_->client,
                                      "X-Request-Signature");
  (void)esp_http_client_delete_header(work_->client, "X-Request-Proof");
  (void)esp_http_client_set_post_field(work_->client, nullptr, 0);
}

bool ShotStopperMicraService::ensureWorkBuffer() {
  if (work_ != nullptr) return true;
  work_ = static_cast<WorkBuffer *>(
      allocExternal(sizeof(WorkBuffer), AllocationOwner::NETWORK));
  if (work_ == nullptr) return false;
  new (work_) WorkBuffer{};
  return true;
}

bool ShotStopperMicraService::ensureIoBuffer() {
  if (io_ != nullptr) return true;
  io_ = static_cast<IoBuffer *>(
      allocExternal(sizeof(IoBuffer), AllocationOwner::NETWORK));
  if (io_ == nullptr) return false;
  new (io_) IoBuffer{};
  return true;
}

void ShotStopperMicraService::clearSession() {
  releaseIoBuffer();
  if (work_ == nullptr) return;
  abortRequested_.store(false, std::memory_order_release);
  secureWipe(work_->accessToken, sizeof(work_->accessToken));
  secureWipe(work_->refreshToken, sizeof(work_->refreshToken));
  work_->responseUsed = 0;
  work_->accessTokenIssuedAtMs = 0;
  work_->httpStatus = 0;
  work_->transportStatus = 0;
  work_->responseOverflow = false;
  if (work_->client != nullptr) {
    TaskLockGuard lock(clientMux_);
    esp_http_client_cleanup(work_->client);
    work_->client = nullptr;
  }
}

void ShotStopperMicraService::releaseIoBuffer() {
  if (io_ == nullptr) return;
  secureWipe(io_->response, sizeof(io_->response));
  io_->~IoBuffer();
  heapCapsFree(io_);
  io_ = nullptr;
}

void ShotStopperMicraService::releaseWorkBuffer() {
  clearSession();
  if (work_ == nullptr) return;
  work_->~WorkBuffer();
  heapCapsFree(work_);
  work_ = nullptr;
}

esp_err_t ShotStopperMicraService::httpEvent(esp_http_client_event_t *event) {
  if (event == nullptr || event->user_data == nullptr) return ESP_FAIL;
  auto *self = static_cast<ShotStopperMicraService *>(event->user_data);
  if (event->event_id != HTTP_EVENT_ON_DATA || event->data == nullptr ||
      event->data_len <= 0 || self->work_ == nullptr || self->io_ == nullptr) {
    return ESP_OK;
  }
  WorkBuffer &work = *self->work_;
  IoBuffer &io = *self->io_;
  const size_t length = static_cast<size_t>(event->data_len);
  if (length >= sizeof(io.response) - work.responseUsed) {
    work.responseOverflow = true;
    return ESP_FAIL;
  }
  memcpy(io.response + work.responseUsed, event->data, length);
  work.responseUsed += length;
  io.response[work.responseUsed] = '\0';
  return ESP_OK;
}

}  // namespace shotstopper
