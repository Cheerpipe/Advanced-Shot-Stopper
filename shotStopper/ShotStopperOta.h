#pragma once

// Wi-Fi firmware updates for the Shot Stopper controller.
//
// The design goal is that no reachable failure can leave the machine without a
// bootable application:
//
//  * A transfer only ever writes the OTA slot that is NOT running, so the
//    working firmware survives a mid-upload power cut, reset or abort.
//  * The boot selection is changed only by an explicit, separate commit, after
//    the whole image passed header, identity and SHA-256 checks.
//  * A committed image boots as ESP_OTA_IMG_PENDING_VERIFY. It becomes
//    permanent once the new firmware proves it can serve its Web UI (HTTP up
//    for the confirm uptime) and the scale radio is idle so the otadata write
//    does not drop GATT. A firmware that crashes, hangs or cannot bring
//    up HTTP is rolled back to the previous slot by the bootloader — except
//    when no other slot is bootable, in which case this image is confirmed so
//    the machine is not left without an application. USB recovery remains.

#include "ShotStopperOtaImage.h"
#include "ShotStopperResourceOwner.h"
#include "ShotStopperTaskMutex.h"

#include <stddef.h>
#include <stdint.h>
#include <atomic>

#ifndef SHOT_STOPPER_HOST_TEST
#include <sdkconfig.h>
// The last line of defence is the bootloader, not this firmware: it is what
// still recovers an image too broken to run any of the code below. Losing that
// silently, because a future core version ships a different configuration, is
// exactly the failure this whole module exists to prevent.
#if !defined(CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE)
#error "Wi-Fi updates require CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE"
#endif
#if !defined(CONFIG_APP_ROLLBACK_ENABLE)
#error "Wi-Fi updates require CONFIG_APP_ROLLBACK_ENABLE"
#endif
// An image that boots into an infinite loop before the task watchdog is armed
// is only ever recovered by the bootloader's own timer.
#if !defined(CONFIG_BOOTLOADER_WDT_ENABLE)
#error "Wi-Fi updates require CONFIG_BOOTLOADER_WDT_ENABLE"
#endif
#endif

namespace shotstopper {

struct OtaHandleAborter {
  void operator()(uint32_t handle) const;
};

struct OtaSha256Deleter {
  void operator()(void *context) const;
};

enum class OtaState : uint8_t {
  UNAVAILABLE = 0,  // partition table has no usable second app slot
  IDLE,
  RECEIVING,
  STAGED,
  COMMITTED,  // boot partition switched; waiting for the restart
};

enum class OtaResult : uint8_t {
  OK = 0,
  UNAVAILABLE,
  BUSY,
  PENDING_VERIFY,  // this boot has not proven itself yet; esp_ota_begin refuses
  BAD_LENGTH,
  TOO_LARGE,
  RECEIVE_FAILED,
  BAD_IMAGE,
  NO_TAG,
  ARCH_MISMATCH,
  DOWNGRADE,
  WRITE_FAILED,
  VERIFY_FAILED,
  SAFETY_LOST,
  NOTHING_STAGED,
  COMMIT_FAILED,
  NO_MEMORY,
  // This build never declared which board it is for, so no image can be proven
  // compatible with it. Reachable on firmware not produced by ./scripts/build-idf.
  NO_IDENTITY,
  SESSION_REQUIRED,
  SESSION_CONFLICT,
  SESSION_IDENTITY_MISMATCH,
  OFFSET_MISMATCH,
  INVALID_RANGE,
  HASH_MISMATCH,
  SESSION_EXPIRED,
  INTERNAL,
};

constexpr size_t OTA_TRANSFER_ID_CAPACITY = 65;
constexpr size_t OTA_SHA256_HEX_CAPACITY = 65;
constexpr uint32_t OTA_TRANSFER_CHUNK_BYTES = 64U * 1024U;
// Journal at most twice per MiB. A 3 MiB image therefore performs at most
// six durable writes (the empty record plus five progress checkpoints), not
// one write and a full-prefix rehash for every HTTP range.
constexpr uint32_t OTA_JOURNAL_CHECKPOINT_BYTES = 512U * 1024U;
// Largest OTA app slot in the two supported partition tables (n8r4). Keep the
// endurance budget coupled to that upper bound: adding a larger supported
// slot must deliberately update the budget and its tests.
constexpr uint32_t OTA_SUPPORTED_MAX_SLOT_BYTES = 0x330000U;
constexpr uint32_t OTA_JOURNAL_ERASE_KEYS_PER_SESSION = 2U;

// One initial record plus one record for every full checkpoint strictly before
// image completion. Completion is intentionally not journaled: esp_ota_end()
// owns final validation, after which both alternating keys are removed.
constexpr uint32_t otaJournalMaxDurableWrites(uint32_t imageBytes) {
  return imageBytes == 0
             ? 0
             : 1U + (imageBytes - 1U) / OTA_JOURNAL_CHECKPOINT_BYTES;
}

constexpr uint32_t OTA_JOURNAL_MAX_WRITES_PER_SESSION =
    otaJournalMaxDurableWrites(OTA_SUPPORTED_MAX_SLOT_BYTES);

static_assert(OTA_JOURNAL_MAX_WRITES_PER_SESSION == 7U,
              "OTA NVS endurance budget changed; review P2 qualification");

inline bool otaJournalCheckpointDue(uint32_t received, uint32_t persisted,
                                    uint32_t expected) {
  return received >= persisted &&
         received < expected &&
         received - persisted >= OTA_JOURNAL_CHECKPOINT_BYTES;
}

// Supplied by the client before any flash operation.  The SHA-256 makes the
// session identity independent of a human version string: a rebuild with the
// same version cannot inherit another image's prefix.
struct OtaSessionIdentity {
  uint32_t size = 0;
  char sha256[OTA_SHA256_HEX_CAPACITY] = {};
  char arch[OTA_ARCH_CAPACITY] = {};
  char version[OTA_VERSION_CAPACITY] = {};
  char transferId[OTA_TRANSFER_ID_CAPACITY] = {};
};

// PENDING_VERIFY policy. Pure: no I/O, no heap. `rollbackPossible` is ignored
// until the confirm deadline with HTTP still down.
// `flashWriteSafe` is false while a scale GATT session is connecting or
// linked: esp_ota_mark_app_valid disables flash cache and drops BLE. At the
// deadline the image must still settle, even if that costs the radio.
enum class OtaPendingVerifyAction : uint8_t {
  NONE = 0,     // not pending, already settled, or busy
  WAIT,         // still inside the confirm window
  CONFIRM,      // HTTP has been serving long enough
  REJECT,       // deadline; a previous slot can take over
  KEEP_RUNNING  // deadline; rollback would leave no bootable app
};

inline OtaPendingVerifyAction decideOtaPendingVerify(
    bool pendingVerify, bool alreadySettled, bool httpReady, uint32_t uptimeMs,
    uint32_t confirmMinUptimeMs, uint32_t confirmDeadlineMs,
    bool rollbackPossible, bool flashWriteSafe = true) {
  if (alreadySettled || !pendingVerify) {
    return OtaPendingVerifyAction::NONE;
  }
  if (httpReady && uptimeMs >= confirmMinUptimeMs) {
    if (!flashWriteSafe && uptimeMs < confirmDeadlineMs) {
      return OtaPendingVerifyAction::WAIT;
    }
    return OtaPendingVerifyAction::CONFIRM;
  }
  if (uptimeMs < confirmDeadlineMs) {
    return OtaPendingVerifyAction::WAIT;
  }
  return rollbackPossible ? OtaPendingVerifyAction::REJECT
                          : OtaPendingVerifyAction::KEEP_RUNNING;
}

struct OtaStatusSnapshot {
  bool available = false;
  bool busy = false;
  OtaState state = OtaState::UNAVAILABLE;
  uint32_t slotBytes = 0;
  uint32_t receivedBytes = 0;
  uint32_t expectedBytes = 0;
  // Failure diagnostics are retained after the active counters are reset, so
  // a client can distinguish a cut transport from a validation failure.
  OtaResult lastResult = OtaResult::OK;
  uint32_t lastReceivedBytes = 0;
  uint32_t lastExpectedBytes = 0;
  bool stagedValid = false;
  OtaImageTag staged = {};
  // The running image was booted by an OTA commit and has not been confirmed.
  bool pendingVerify = false;
  bool confirmed = false;
  bool rejected = false;
  OtaImageTag running = {};
  bool sessionActive = false;
  uint32_t nextOffset = 0;
  uint32_t chunkBytes = OTA_TRANSFER_CHUNK_BYTES;
  uint32_t sessionExpiresInMs = 0;
  OtaSessionIdentity session = {};
  char lastChunkSha256[OTA_SHA256_HEX_CAPACITY] = {};
};

// Lock-free publication consumed by network_manager. Long HTTP socket waits
// remain under the OTA transition mutex, but must never make the watchdog-
// supervised manager wait behind an upload.
struct OtaPublishedState {
  bool available = false;
  bool busy = false;
  bool pendingVerify = false;
  bool confirmed = false;
  bool rejected = false;
};

// Transport hooks supplied by the HTTP layer. Keeping them as plain function
// pointers keeps this module free of any dependency on esp_http_server.
struct OtaStreamIo {
  // Returns the number of bytes read, 0 if the peer closed early, or a
  // negative value on a transport error.
  int (*read)(void *context, uint8_t *buffer, size_t capacity) = nullptr;
  // Polled on every chunk. Returning false aborts immediately: a shot (or
  // even just the paddle) always wins over an in-flight firmware transfer.
  bool (*stillSafe)(void *context) = nullptr;
  void (*progress)(void *context, uint32_t received, uint32_t expected) =
      nullptr;
  void *context = nullptr;
};

class ShotStopperOta {
  public:
  static ShotStopperOta &instance();

  // Discovers the slot pair and records whether this boot is awaiting
  // confirmation. Safe to call more than once.
  void begin();

  bool available() const;
  bool busy() const;
  uint32_t slotBytes() const;
  OtaStatusSnapshot snapshot() const;
  OtaPublishedState publishedState() const;

  // Starts (or reconnects to) a resumable transfer.  This does not erase or
  // write flash; the first range validates the image header before begin().
  OtaResult createSession(const OtaSessionIdentity &identity, uint32_t now);
  // Writes exactly one contiguous range. `offset` must be nextOffset(). A
  // completed duplicate is reported as OFFSET_MISMATCH to the HTTP layer,
  // which answers 208 without touching flash.
  OtaResult writeRange(uint32_t offset, uint32_t contentLength,
                       const OtaStreamIo &io, uint32_t now);
  void expireSession(uint32_t now);
  bool isExactSession(const OtaSessionIdentity &identity) const;
  bool isDuplicateRange(uint32_t offset, uint32_t contentLength) const;
  // Points the bootloader at the staged image. The caller performs the
  // restart, so machine circuit can be opened first.
  OtaResult commit();
  void discard();

  bool bootPendingVerify() const;
  bool runningImageConfirmed() const;
  bool runningImageRejected() const;
  // Cancels the pending rollback: the running image becomes permanent.
  bool confirmRunningImage();
  // Selects the previous slot without rebooting, so the caller can restart
  // through the normal machine circuit-safe path. Returns false when no other slot holds a
  // bootable application, in which case staying on this image is the only
  // option that keeps the machine usable. After a successful reject, both
  // confirmRunningImage() and a second reject are no-ops, so a later tick
  // cannot silently cancel the armed rollback.
  bool rejectRunningImage();

  OtaImageTag runningTag() const;

  static const char *resultName(OtaResult result);
  static const char *stateName(OtaState state);

  private:
  ShotStopperOta() = default;

  OtaResult finishFailure(OtaResult result);
  bool reconfirmStagedTag();
  bool verifySessionSha256();
  bool startSessionSha256();
  bool updateSessionSha256(const uint8_t *bytes, size_t length);
  void clearSessionSha256();
  void clearSession(bool abortHandle);
  bool persistSession();
  void removeSessionJournal();
  void restoreSessionJournal();
  void publishState();

  bool started_ = false;
  bool available_ = false;
  bool busy_ = false;
  bool pendingVerify_ = false;
  bool confirmed_ = false;
  bool rejected_ = false;
  bool stagedValid_ = false;
  OtaState state_ = OtaState::UNAVAILABLE;
  uint32_t slotBytes_ = 0;
  uint32_t receivedBytes_ = 0;
  uint32_t expectedBytes_ = 0;
  OtaResult lastResult_ = OtaResult::OK;
  uint32_t lastReceivedBytes_ = 0;
  uint32_t lastExpectedBytes_ = 0;
  uint32_t stagedTagOffset_ = 0;
  OtaImageTag stagedTag_ = {};
  OtaImageTag runningTag_ = {};
  const void *targetPartition_ = nullptr;
  bool sessionActive_ = false;
  uint32_t sessionLastActivityMs_ = 0;
  uint32_t sessionGeneration_ = 0;
  OtaSessionIdentity session_ = {};
  char lastChunkSha256_[OTA_SHA256_HEX_CAPACITY] = {};
  uint32_t lastChunkOffset_ = 0;
  uint32_t lastChunkLength_ = 0;
  UniqueResource<uint32_t, OtaHandleAborter> otaHandle_;
  OtaImageTagScanner scanner_ = {};
  // Owned mbedtls_sha256_context. Kept opaque here so this public header does
  // not force every consumer to include mbedTLS internals.
  UniqueResource<void *, OtaSha256Deleter> sessionSha256_;
  uint32_t journaledBytes_ = 0;
  std::atomic<uint32_t> publishedFlags_{0};
  mutable TaskMutex mutex_;
};

}  // namespace shotstopper
