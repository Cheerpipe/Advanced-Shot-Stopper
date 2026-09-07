#define SHOT_STOPPER_HOST_TEST
#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#define SHOT_STOPPER_OTA_HOST_TEST

#include "../ShotStopperOta.cpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>

namespace shotstopper {
struct OtaHostTestAccess {
  static std::unique_ptr<ShotStopperOta> fresh() {
    std::unique_ptr<ShotStopperOta> ota(new ShotStopperOta);
    ota->begin();
    // Host tests must not regenerate the tracked version header to pick a board.
    std::strcpy(ota->runningTag_.arch, "n16r8");
    return ota;
  }
};
}  // namespace shotstopper

using namespace shotstopper;

#define CHECK(condition) do { if (!(condition)) { \
  std::cerr << __func__ << ':' << __LINE__ << ": " << #condition << '\n'; \
  std::exit(EXIT_FAILURE); \
} } while (false)

void reset(esp_ota_img_states_t state = ESP_OTA_IMG_VALID) {
  ota_host::reset();
  persistence_host::reset();
  ota_host::state = state;
  g_hostFlashIoMutexAvailable = true;
  hostMillis = 100;
}

struct Image {
  std::vector<uint8_t> bytes;
  OtaSessionIdentity identity;
  explicit Image(size_t size = 65536) : bytes(size, 0xA5) {
    std::fill(bytes.begin(), bytes.begin() + OTA_IMAGE_PREFIX_BYTES, 0);
    bytes[0] = OTA_ESP_IMAGE_MAGIC;
    bytes[12] = 9;
    const uint32_t magic = OTA_APP_DESC_MAGIC;
    std::memcpy(bytes.data() + OTA_APP_DESC_OFFSET, &magic, sizeof(magic));
    std::strcpy(reinterpret_cast<char *>(bytes.data() + OTA_APP_DESC_PROJECT_NAME_OFFSET),
                OTA_EXPECTED_PROJECT_NAME);
    char prefix[OTA_TAG_PREFIX_CAPACITY];
    otaTagPrefix(prefix, sizeof(prefix));
    const std::string tag = std::string(prefix) + "arch=n16r8|ver=" +
        FW_VERSION_STRING + "|packed=" + FW_VERSION_PACKED_STRING + "|END";
    std::memcpy(bytes.data() + 512, tag.data(), tag.size());
    identity.size = static_cast<uint32_t>(size);
    std::strcpy(identity.arch, "n16r8");
    std::strcpy(identity.version, FW_VERSION_STRING);
    std::strcpy(identity.transferId, "test-transfer-012345");
    mbedtls_sha256_context hash;
    mbedtls_sha256_init(&hash);
    mbedtls_sha256_update(&hash, bytes.data(), bytes.size());
    uint8_t digest[32];
    mbedtls_sha256_finish(&hash, digest);
    sha256Hex(digest, identity.sha256);
  }
};

struct Stream {
  const std::vector<uint8_t> &bytes;
  size_t position = 0;
  size_t readLimit = 4096;
  size_t failAt = SIZE_MAX;
  bool safe = true;
  bool unsafeAfterRead = false;
  static int read(void *context, uint8_t *out, size_t capacity) {
    auto &s = *static_cast<Stream *>(context);
    if (s.position >= s.failAt) return 0;
    const size_t length = std::min({capacity, s.readLimit,
        s.bytes.size() - s.position, s.failAt - s.position});
    std::memcpy(out, s.bytes.data() + s.position, length);
    s.position += length;
    if (s.unsafeAfterRead) s.safe = false;
    return static_cast<int>(length);
  }
  static bool stillSafe(void *context) { return static_cast<Stream *>(context)->safe; }
  OtaStreamIo io() { return {read, stillSafe, nullptr, this}; }
};

void testFragmentedHeaderAndTail() {
  reset();
  auto ota = OtaHostTestAccess::fresh();
  Image image(65536 + 31);
  CHECK(ota->createSession(image.identity, hostMillis) == OtaResult::OK);
  Stream stream{image.bytes};
  stream.readLimit = 100;
  CHECK(ota->writeRange(0, 65536, stream.io(), hostMillis) == OtaResult::OK);
  CHECK(ota_host::writes == 16);
  CHECK(ota->writeRange(65536, 31, stream.io(), hostMillis) == OtaResult::OK);
  CHECK(ota->snapshot().state == OtaState::STAGED);
  CHECK(ota_host::ends == 1 && ota_host::aborts == 0);
  CHECK(std::equal(image.bytes.begin(), image.bytes.end(), ota_host::flash.begin()));
  CHECK(ota->commit() == OtaResult::OK);
  CHECK(ota->snapshot().state == OtaState::COMMITTED);
}

void testInterruptedRangeAndAlignment() {
  reset();
  auto ota = OtaHostTestAccess::fresh();
  Image image;
  CHECK(ota->createSession(image.identity, hostMillis) == OtaResult::OK);
  Stream stream{image.bytes};
  CHECK(ota->writeRange(0, 5000, stream.io(), hostMillis) == OtaResult::INVALID_RANGE);
  CHECK(ota_host::begins == 0 && stream.position == 0);
  stream.failAt = 4096 + 100;
  CHECK(ota->writeRange(0, image.identity.size, stream.io(), hostMillis) == OtaResult::RECEIVE_FAILED);
  auto status = ota->snapshot();
  CHECK(status.sessionActive && status.nextOffset == 4096 && status.busy);
  CHECK(status.lastResult == OtaResult::RECEIVE_FAILED && ota_host::aborts == 0);
  CHECK(ota_host::flash[4096] == 0xFF);
  stream.position = status.nextOffset;
  stream.failAt = SIZE_MAX;
  CHECK(ota->writeRange(4096, image.identity.size - 4096, stream.io(), hostMillis) == OtaResult::OK);
  CHECK(ota->snapshot().stagedValid);
}

void testSafetyAndFatalFailures() {
  for (int failure = 0; failure < 5; ++failure) {
    reset();
    auto ota = OtaHostTestAccess::fresh();
    Image image;
    if (failure == 3) image.identity.sha256[0] = image.identity.sha256[0] == 'a' ? 'b' : 'a';
    CHECK(ota->createSession(image.identity, hostMillis) == OtaResult::OK);
    Stream stream{image.bytes};
    if (failure == 0) stream.unsafeAfterRead = true;
    if (failure == 1) ota_host::writeError = ESP_FAIL;
    if (failure == 2) ota_host::endError = ESP_FAIL;
    if (failure == 4) image.bytes[0] = 0;
    const OtaResult expected[] = {OtaResult::SAFETY_LOST, OtaResult::WRITE_FAILED,
        OtaResult::VERIFY_FAILED, OtaResult::HASH_MISMATCH, OtaResult::BAD_IMAGE};
    CHECK(ota->writeRange(0, image.identity.size, stream.io(), hostMillis) == expected[failure]);
    CHECK(!ota->snapshot().sessionActive && !ota->snapshot().stagedValid);
    CHECK(!ota_host::active && ota_host::invalidHandles == 0);
    if (failure == 0 || failure == 4) CHECK(ota_host::begins == 0 && ota_host::writes == 0);
    if (failure == 1) CHECK(ota_host::aborts == 1);
    if (failure == 2 || failure == 3) CHECK(ota_host::ends == 1 && ota_host::aborts == 0);
  }
}

void testCheckpointRecovery() {
  for (bool corrupt : {false, true}) {
    reset();
    auto ota = OtaHostTestAccess::fresh();
    Image image(OTA_JOURNAL_CHECKPOINT_BYTES + 2 * OTA_TRANSFER_CHUNK_BYTES);
    CHECK(ota->createSession(image.identity, hostMillis) == OtaResult::OK);
    Stream stream{image.bytes};
    for (uint32_t offset = 0; offset < OTA_JOURNAL_CHECKPOINT_BYTES + OTA_TRANSFER_CHUNK_BYTES;
         offset += OTA_TRANSFER_CHUNK_BYTES) {
      CHECK(ota->writeRange(offset, OTA_TRANSFER_CHUNK_BYTES, stream.io(), hostMillis) == OtaResult::OK);
    }
    ota.reset();  // Volatile state dies; flash and journal survive.
    if (corrupt) ota_host::flash[8000] ^= 1;
    else std::fill(ota_host::flash.begin() + OTA_JOURNAL_CHECKPOINT_BYTES,
                  ota_host::flash.begin() + image.bytes.size(), 0);
    ota = OtaHostTestAccess::fresh();
    if (corrupt) {
      CHECK(!ota->snapshot().sessionActive && ota_host::resumes == 0);
      CHECK(persistence_host::records.empty());
      continue;
    }
    CHECK(ota->snapshot().nextOffset == OTA_JOURNAL_CHECKPOINT_BYTES);
    CHECK(ota_host::resumes == 1 && ota_host::resumeMode == OTA_WITH_SEQUENTIAL_WRITES);
    stream.position = OTA_JOURNAL_CHECKPOINT_BYTES;
    for (uint32_t offset = OTA_JOURNAL_CHECKPOINT_BYTES; offset < image.bytes.size();
         offset += OTA_TRANSFER_CHUNK_BYTES) {
      CHECK(ota->writeRange(offset, OTA_TRANSFER_CHUNK_BYTES, stream.io(), hostMillis) == OtaResult::OK);
    }
    CHECK(ota->snapshot().stagedValid && ota_host::invalidHandles == 0);
    CHECK(std::equal(image.bytes.begin(), image.bytes.end(), ota_host::flash.begin()));
    CHECK(persistence_host::records.empty());
  }
}

OtaBootStatus service(ShotStopperOta &ota, uint32_t now, bool http = true,
                      bool cycle = false, bool relay = false, bool ble = false) {
  return ota.serviceBoot(now, http, cycle, relay, ble, 15000, 180000);
}

void testConfirmationGatesAndDiagnostics() {
  reset(ESP_OTA_IMG_PENDING_VERIFY);
  auto ota = OtaHostTestAccess::fresh();
  CHECK(ota->snapshot().pendingVerify && !ota->snapshot().confirmed);
  Image image;
  CHECK(ota->createSession(image.identity, hostMillis) == OtaResult::PENDING_VERIFY);
  std::strcpy(image.identity.transferId, "different-transfer-012345");
  image.identity.sha256[0] = image.identity.sha256[0] == 'a' ? 'b' : 'a';
  CHECK(ota->createSession(image.identity, hostMillis) == OtaResult::PENDING_VERIFY);
  CHECK(std::string(service(*ota, 14999).reason) == "WAIT_UPTIME");
  CHECK(std::string(service(*ota, 15000, false).reason) == "WAIT_HTTP");
  CHECK(std::string(service(*ota, 15000, true, true).reason) == "WAIT_CYCLE");
  CHECK(std::string(service(*ota, 15000, true, false, true).reason) == "WAIT_RELAY");
  CHECK(std::string(service(*ota, 179999, true, false, false, true).reason) == "WAIT_BLE");
  CHECK(ota_host::confirms == 0);
  CHECK(service(*ota, 180000, true, true, false, true).attempts == 0);
  CHECK(service(*ota, 180000, true, false, false, true).state == ESP_OTA_IMG_VALID);
  const auto status = ota->snapshot();
  CHECK(status.confirmed && !status.pendingVerify && status.confirmAttempts == 1);
  CHECK(status.confirmLastError == ESP_OK && std::string(status.confirmBlockReason) == "CONFIRMED");
  CHECK(std::strlen(status.runningImageSha256) == 64);
  CHECK(service(*ota, 200000).attempts == 1 && ota_host::confirms == 1);
  reset(ESP_OTA_IMG_PENDING_VERIFY);
  ota = OtaHostTestAccess::fresh();
  CHECK(service(*ota, 15000).state == ESP_OTA_IMG_VALID);
}

void testConfirmationRetryAndStateRecovery() {
  for (bool flashBusy : {false, true}) {
    reset(ESP_OTA_IMG_PENDING_VERIFY);
    auto ota = OtaHostTestAccess::fresh();
    if (flashBusy) g_hostFlashIoMutexAvailable = false;
    else ota_host::confirmError = ESP_FAIL;
    const auto first = service(*ota, 15000);
    CHECK(first.attempts == 1 && first.lastError == (flashBusy ? ESP_ERR_TIMEOUT : ESP_FAIL));
    CHECK(!ota->snapshot().confirmed && ota->snapshot().pendingVerify);
    CHECK(std::string(first.reason) == (flashBusy ? "FLASH_BUSY" : "CONFIRM_ERROR"));
    g_hostFlashIoMutexAvailable = true;
    ota_host::confirmError = ESP_OK;
    CHECK(service(*ota, 15999).attempts == 1);
    CHECK(service(*ota, 16000).attempts == 2);
    CHECK(ota->snapshot().confirmed && ota->snapshot().confirmLastError == ESP_OK);
  }
  reset(ESP_OTA_IMG_PENDING_VERIFY);
  ota_host::stateError = ESP_FAIL;
  auto ota = OtaHostTestAccess::fresh();
  CHECK(ota->snapshot().bootState == -2 && ota->snapshot().pendingVerify);
  CHECK(std::string(service(*ota, 15000).reason) == "STATE_ERROR");
  ota_host::stateError = ESP_OK;
  CHECK(!ota->snapshot().confirmed && service(*ota, 15999).state == -2);
  CHECK(service(*ota, 16000).state == ESP_OTA_IMG_VALID);
}

void testRollbackDoesNotConfirmOnFailure() {
  for (bool flashBusy : {false, true}) {
    reset(ESP_OTA_IMG_PENDING_VERIFY);
    auto ota = OtaHostTestAccess::fresh();
    if (flashBusy) g_hostFlashIoMutexAvailable = false;
    else ota_host::rollbackError = ESP_FAIL;
    const auto status = service(*ota, 180000, false);
    CHECK(std::string(status.reason) == (flashBusy ? "FLASH_BUSY" : "ROLLBACK_ERROR"));
    CHECK(ota_host::confirms == 0 && ota->snapshot().pendingVerify);
    g_hostFlashIoMutexAvailable = true;
    ota_host::rollbackError = ESP_OK;
    CHECK(service(*ota, 180999, false).attempts == 1);
    CHECK(service(*ota, 181000, false).state == ESP_OTA_IMG_INVALID);
    CHECK(ota->snapshot().rejected && !ota->confirmRunningImage());
    CHECK(ota_host::confirms == 0);
  }
  reset(ESP_OTA_IMG_PENDING_VERIFY);
  ota_host::rollbackPossible = false;
  auto ota = OtaHostTestAccess::fresh();
  CHECK(std::string(service(*ota, 180000, false).reason) == "NO_ROLLBACK");
  CHECK(ota_host::rollbacks == 0 && ota_host::confirms == 1);
  CHECK(ota->snapshot().confirmed);
}

int main() {
  testFragmentedHeaderAndTail();
  testInterruptedRangeAndAlignment();
  testSafetyAndFatalFailures();
  testCheckpointRecovery();
  testConfirmationGatesAndDiagnostics();
  testConfirmationRetryAndStateRecovery();
  testRollbackDoesNotConfirmOnFailure();
  std::cout << "OTA functional transfer, checkpoint and boot service tests passed\n";
}
