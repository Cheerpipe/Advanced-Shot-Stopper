#pragma once

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperNvsDualSlot.h"
#include "ShotStopperPersistedNetwork.h"
#include "ShotStopperPersistedSettings.h"
#include "ShotStopperPreferences.h"
#include "ShotStopperSettingsWriteAdmission.h"

#include <stdint.h>

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_task_wdt.h>
#include <esp_ota_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace shotstopper {

inline void initializeSettingsSchemaWriteAdmission() {
#if !defined(SHOT_STOPPER_HOST_TEST) && !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  if (!ensureFlashIoMutex() || !lockFlashIo()) return;
  const esp_err_t error = esp_ota_get_state_partition(esp_ota_get_running_partition(), &state);
  unlockFlashIo();
  settingsSchemaWritesAdmitted.store(error == ESP_ERR_NOT_FOUND ||
      (error == ESP_OK && (state == ESP_OTA_IMG_VALID || state == ESP_OTA_IMG_UNDEFINED)),
      std::memory_order_release);
#endif
}

inline void ensurePersistedPresetBank(PersistedSettings &settings) {
  ensureShotPresetBank(settings.presets, settings.runtime.retareWindowMs,
                       settings.runtime.autoRetare);
}

// Set when the most recently ADOPTED settings record aliased blobs that still
// stored the removed regression mode (bbwAlgorithm==0); boot consumes it to
// invalidate BBW learning so post-migration guards fail safe for one shot.
// bbwLegacyAliasedPresetIds scopes that invalidation to the presets that
// actually aliased — never-aliased presets keep their learning evidence even
// while a migrated slot waits for its first post-migration persist.
// Only loadPersistedSettings sets these, from the slot that actually won — a
// stale losing slot or a probe read must not re-trigger invalidation.
inline bool bbwLegacyAliasesApplied = false;
inline uint8_t bbwLegacyAliasedPresetIds[MAX_SHOT_PRESETS] = {};
inline uint8_t bbwLegacyAliasedPresetCount = 0;

// Repair-before-validate: settings slots persisted by pre-removal firmware
// must never surface as invalid. The regression algorithm id becomes EWMA,
// and the EWMA offset seeds from the regression offset the user actually
// brewed with — but only while the EWMA field still holds its untouched
// default, so a user who already ran EWMA keeps that learning. Returns
// whether anything changed so the caller can re-sign the checksum; aliased
// preset ids are reported for the scoped boot invalidation.
inline bool repairRemovedBbwLegacyAliases(
    PersistedSettings &settings, uint8_t *aliasedPresetIds = nullptr,
    uint8_t *aliasedPresetCount = nullptr) {
  if (aliasedPresetCount != nullptr) {
    *aliasedPresetCount = 0;
  }
  bool repaired = false;
  if (settings.runtime.bbwAlgorithm == 0) {
    settings.runtime.bbwAlgorithm =
        static_cast<uint8_t>(BbwAlgorithm::LINEAR_EWMA);
    repaired = true;
  }
  for (uint8_t i = 0; i < settings.presets.count && i < MAX_SHOT_PRESETS; ++i) {
    ShotPreset &preset = settings.presets.presets[i];
    if (preset.bbwAlgorithm != 0) {
      continue;
    }
    preset.bbwAlgorithm = static_cast<uint8_t>(BbwAlgorithm::LINEAR_EWMA);
    if (preset.bbwEwmaOffsetG == DEFAULT_WEIGHT_OFFSET_G) {
      preset.bbwEwmaOffsetG = preset.weightOffsetG;
    }
    preset.bbwProfileVersion = BBW_PROFILE_VERSION;
    repaired = true;
    if (aliasedPresetIds != nullptr && aliasedPresetCount != nullptr &&
        *aliasedPresetCount < MAX_SHOT_PRESETS) {
      aliasedPresetIds[(*aliasedPresetCount)++] = preset.id;
    }
  }
  return repaired;
}

inline bool validPersistedSettings(const PersistedSettings &settings) {
  if (settings.magic != PERSISTED_SETTINGS_MAGIC ||
      settings.schemaVersion != CONFIG_SCHEMA_VERSION ||
      settings.structureSize != sizeof(PersistedSettings) ||
      settings.checksum != persistedSettingsChecksum(settings) ||
      validateRuntimeConfig(settings.runtime) != ConfigValidationError::NONE ||
      settings.runtime.timezoneSource >= 3 ||
      settings.runtime.timezoneAutomatic > 1 ||
      !validBullseyeMelodyConfig(settings.bullseyeMelody) ||
      !validateShotPresetBank(settings.presets, settings.runtime.retareWindowMs,
                              settings.runtime.autoRetare) ||
      !validDevicePassword(settings.devicePassword) ||
      !validDeviceName(settings.deviceName) ||
      !validLineaMicraSettings(settings.lineaMicra) ||
      !validPreferredScaleMac(settings.preferredScaleMac) ||
      !validPreferredScaleName(settings.preferredScaleName) ||
      !validScaleHistoryEntries(settings.scaleHistory) ||
      !validWebhookConfig(settings.webhook) ||
      !validPersistedStaNetwork(settings)) {
    return false;
  }
  return true;
}

inline void finalizePersistedSettings(PersistedSettings &settings) {
  ensurePersistedPresetBank(settings);
  uint32_t seedSeq = 0;
  for (auto & i : settings.scaleHistory) {
    if (i.lastSeenSeq > seedSeq) {
      seedSeq = i.lastSeenSeq;
    }
  }
  seedScaleHistoryFromPreferred(settings.scaleHistory, seedSeq,
                                settings.preferredScaleMac,
                                settings.preferredScaleName);
  settings.magic = PERSISTED_SETTINGS_MAGIC;
  settings.schemaVersion = CONFIG_SCHEMA_VERSION;
  settings.structureSize = sizeof(PersistedSettings);
  settings.checksum = 0;
  settings.checksum = persistedSettingsChecksum(settings);
}

// Single-record staging inside the shared flash-I/O scratch (see FlashIoScratch).
// Allocated from internal SRAM (heap, not BSS): source/destination of
// Preferences putBytes/getBytes if a flash path disables cache despite XIP.
inline PersistedSettings &persistedSettingsScratch() {
  static_assert(sizeof(PersistedSettings) <= FLASH_IO_SCRATCH_BYTES,
                "PersistedSettings exceeds flash I/O buffer");
  return *reinterpret_cast<PersistedSettings *>(flashIoScratchBytes());
}

// Settings v3→v4 cutover: preset and runtime goals widen from whole grams
// (uint8) to decigrams (uint16). v1/v2/v3 blobs share the 3312-byte layout;
// after the in-place v1/v2 semantic tweaks the blob is v3, and this staged
// widening upgrades it in the read buffer before validation. Byte offsets
// are pinned by the ShotPreset/RuntimeConfig static asserts.
constexpr size_t kSettingsV3Bytes = 3312;
constexpr size_t kSettingsV3ChecksumOffset = 3304;
constexpr size_t kSettingsV3PresetBytes = 104;
// The bank header keeps one alignment pad byte before the preset array.
constexpr size_t kSettingsV3BankBytes =
    offsetof(ShotPresetBank, presets) + MAX_SHOT_PRESETS * kSettingsV3PresetBytes;
static_assert(kSettingsV3BankBytes == 836);
static_assert(sizeof(ShotPreset) == 108 && offsetof(ShotPreset, goalWeightDg) == 28);
static_assert(offsetof(RuntimeConfig, goalWeightDg) == 4);
static_assert(offsetof(PersistedSettings, runtime) == 16);

inline uint32_t settingsV3Checksum(const uint8_t *blob) {
  return crc32(blob, kSettingsV3ChecksumOffset);
}

// Widens an authenticated v3-layout blob in place; the caller's buffer must
// be sizeof(PersistedSettings) so the +32 preset growth fits.
inline void widenSettingsV3ToV4(uint8_t *blob) {
  const size_t bank = offsetof(PersistedSettings, presets);
  // Move the post-bank tail (including the old checksum slot) past the
  // widened bank, then expand presets back-to-front so overlaps stay safe.
  memmove(blob + bank + sizeof(ShotPresetBank),
          blob + bank + kSettingsV3BankBytes,
          kSettingsV3Bytes - bank - kSettingsV3BankBytes);
  const size_t slots = bank + offsetof(ShotPresetBank, presets);
  for (size_t index = MAX_SHOT_PRESETS; index-- > 0;) {
    const uint8_t *src = blob + slots + index * kSettingsV3PresetBytes;
    uint8_t *dst = blob + slots + index * sizeof(ShotPreset);
    // Capture the goal before any move: preset 0 widens in place, so the
    // memset below would otherwise erase its own source byte.
    const uint16_t goalDg = static_cast<uint16_t>(src[27]) * 10;
    // Tail first: the widened head would otherwise overwrite the tail source.
    memmove(dst + 32, src + 28, kSettingsV3PresetBytes - 28);
    memmove(dst, src, 27);
    memset(dst + 27, 0, 5);
    dst[28] = static_cast<uint8_t>(goalDg);
    dst[29] = static_cast<uint8_t>(goalDg >> 8);
  }
  // RuntimeConfig: the u16 goal absorbs the v3 pad byte at offset 7, so
  // everything from offset 8 on is byte-identical.
  const size_t runtime = offsetof(PersistedSettings, runtime);
  const uint16_t runtimeGoalDg = static_cast<uint16_t>(blob[runtime + 4]) * 10;
  blob[runtime + 7] = blob[runtime + 6];
  blob[runtime + 6] = blob[runtime + 5];
  blob[runtime + 4] = static_cast<uint8_t>(runtimeGoalDg);
  blob[runtime + 5] = static_cast<uint8_t>(runtimeGoalDg >> 8);
  // The shifted v3 tail leaves old checksum bytes in the v4 tail padding;
  // zero it so byte-exact round-trips stay deterministic.
  memset(blob + offsetof(PersistedSettings, checksum) + sizeof(uint32_t), 0,
         sizeof(PersistedSettings) - offsetof(PersistedSettings, checksum) -
             sizeof(uint32_t));
  auto *upgraded = reinterpret_cast<PersistedSettings *>(blob);
  upgraded->schemaVersion = CONFIG_SCHEMA_VERSION;
  upgraded->structureSize = sizeof(PersistedSettings);
  upgraded->checksum = 0;
  upgraded->checksum = persistedSettingsChecksum(*upgraded);
}

inline bool readSettingsSlot(ShotStopperPreferences &preferences, const char *key,
                             PersistedSettings &settings,
                             uint8_t *aliasedPresetIds = nullptr,
                             uint8_t *aliasedPresetCount = nullptr) {
  const size_t stored = preferences.isKey(key)
                            ? preferences.getBytesLength(key)
                            : 0;
  if (stored != sizeof(PersistedSettings) && stored != kSettingsV3Bytes) {
    return false;
  }
  if (preferences.getBytes(key, &settings, stored) != stored) {
    return false;
  }
  // All old schemas share the v3 byte layout: authenticate against the v3
  // checksum slot, widen to v4 bytewise, then apply the historical v1/v2
  // semantic tweaks against the widened struct.
  if (stored == kSettingsV3Bytes) {
    uint8_t *blob = reinterpret_cast<uint8_t *>(&settings);
    const uint32_t schema = settings.schemaVersion;
    if (schema >= 1 && schema <= 3 &&
        settings.magic == PERSISTED_SETTINGS_MAGIC &&
        settings.structureSize == kSettingsV3Bytes &&
        reinterpret_cast<const uint32_t *>(blob + kSettingsV3ChecksumOffset)[0] ==
            settingsV3Checksum(blob)) {
      widenSettingsV3ToV4(blob);
      if (schema == 1) {
        settings.runtime.touchStopFallbackEnabled = true;
        for (auto &preset : settings.presets.presets)
          preset.touchStopFallbackEnabled = true;
      }
      if (schema < 3) {
        settings.lineaMicra.connectionType =
            static_cast<uint8_t>(MicraConnectionType::WEBSOCKET);
        settings.checksum = 0;
        settings.checksum = persistedSettingsChecksum(settings);
      }
    }
  }
  if (repairRemovedBbwLegacyAliases(settings, aliasedPresetIds,
                                    aliasedPresetCount)) {
    settings.checksum = persistedSettingsChecksum(settings);
  }
  if (!validPersistedSettings(settings)) return false;
  return true;
}

inline bool lockSettingsNvs() { return lockFlashIo(); }
inline void unlockSettingsNvs() { unlockFlashIo(); }
inline void yieldSettingsNvs() { yieldFlashIo(); }
inline void feedSettingsNvsWatchdog() { feedFlashIoWatchdog(); }

inline bool savePersistedSettings(PersistedSettings &settings);

inline bool loadPersistedSettings(PersistedSettings &settings) {
  if (!lockSettingsNvs()) {
    return false;
  }
  ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
  if (!preferences.begin(SETTINGS_NAMESPACE, true)) {
    unlockSettingsNvs();
    return false;
  }
  PersistedSettings &scratch = persistedSettingsScratch();
  uint8_t slotAIds[MAX_SHOT_PRESETS];
  uint8_t slotACount = 0;
  bool loaded = readSettingsSlot(preferences, SETTINGS_SLOT_A, scratch,
                                 slotAIds, &slotACount);
  uint32_t loadedRevision = 0;
  const uint8_t *adoptedIds = slotAIds;
  uint8_t adoptedCount = slotACount;
  if (loaded) {
    settings = scratch;
    loadedRevision = scratch.storageRevision;
  }
  uint8_t slotBIds[MAX_SHOT_PRESETS];
  uint8_t slotBCount = 0;
  if (readSettingsSlot(preferences, SETTINGS_SLOT_B, scratch, slotBIds,
                       &slotBCount) &&
      (!loaded || secondRevisionIsNewer(loadedRevision,
                                        scratch.storageRevision))) {
    settings = scratch;
    loadedRevision = scratch.storageRevision;
    loaded = true;
    adoptedIds = slotBIds;
    adoptedCount = slotBCount;
  }
  preferences.end();
  // Only the adopted record may re-arm the one-shot migration invalidation;
  // a stale losing slot or later probe read must not. The id list scopes the
  // invalidation to presets that actually aliased.
  bbwLegacyAliasesApplied = loaded && adoptedCount > 0;
  bbwLegacyAliasedPresetCount = bbwLegacyAliasesApplied ? adoptedCount : 0;
  if (bbwLegacyAliasesApplied) {
    for (uint8_t i = 0; i < adoptedCount; ++i) {
      bbwLegacyAliasedPresetIds[i] = adoptedIds[i];
    }
  }
  if (loaded) durableTimezoneSaved().store(settings.runtime.timezoneId[0] != '\0');
  unlockSettingsNvs();
  return loaded;
}

inline void overlayLivePersistedSettings(PersistedSettings &settings,
                                         const RuntimeConfig &runtime,
                                         const ShotPresetBank &presets) {
  settings.runtime = runtime;
  settings.presets = presets;
}

inline uint32_t &durableStorageRevision() {
  static uint32_t revision = 0;
  return revision;
}

inline bool &durableStorageRevisionValid() {
  static bool valid = false;
  return valid;
}

inline void noteDurableStorageRevision(uint32_t revision) {
  if (!lockSettingsNvs()) {
    return;
  }
  durableStorageRevision() = revision;
  durableStorageRevisionValid() = revision != 0;
  unlockSettingsNvs();
}

inline void resetDurableStorageRevision() {
  if (!lockSettingsNvs()) {
    return;
  }
  durableStorageRevision() = 0;
  durableStorageRevisionValid() = false;
  durableTimezoneSaved().store(false);
  unlockSettingsNvs();
}

inline bool savePersistedSettings(PersistedSettings &settings) {
  if (!settingsSchemaWritesAdmitted.load(std::memory_order_acquire)) return false;
  // One internal record is reused for revision probes, candidate write, and
  // read-back verification. Never call loadPersistedSettings while locked.
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  if (!lockSettingsNvs()) {
    feedSettingsNvsWatchdog();
    return false;
  }
  PersistedSettings &scratch = persistedSettingsScratch();
  uint32_t revision = settings.storageRevision;
  if (durableStorageRevisionValid()) {
    revision = durableStorageRevision();
  } else if (revision == 0) {
    bool haveRevision = false;
    ShotStopperPreferences probe(NvsSubsystem::SETTINGS);
    if (probe.begin(SETTINGS_NAMESPACE, true)) {
      if (readSettingsSlot(probe, SETTINGS_SLOT_A, scratch)) {
        revision = scratch.storageRevision;
        haveRevision = true;
      }
      if (readSettingsSlot(probe, SETTINGS_SLOT_B, scratch)) {
        if (!haveRevision || secondRevisionIsNewer(
                                 revision, scratch.storageRevision)) {
          revision = scratch.storageRevision;
        }
        haveRevision = true;
      }
      probe.end();
    }
  }
  const uint32_t originalRevision = settings.storageRevision;
  const uint32_t originalChecksum = settings.checksum;
  // Zero first: the shared scratch carries stale bytes from earlier reads,
  // and memberwise assignment leaves padding indeterminate — stored blobs
  // must stay byte-reproducible for the read-back verification.
  memset(&scratch, 0, sizeof(scratch));
  scratch = settings;
  scratch.storageRevision = revision + 1U;
  if (scratch.storageRevision == 0) {
    scratch.storageRevision = 1;
  }
  finalizePersistedSettings(scratch);

  ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    feedSettingsNvsWatchdog();
    return false;
  }
  const char *target =
      (scratch.storageRevision & 1U) ? SETTINGS_SLOT_A : SETTINGS_SLOT_B;
  const uint32_t candidateRevision = scratch.storageRevision;
  const bool written =
      preferences.putBytes(target, &scratch, sizeof(scratch)) == sizeof(scratch);
  if (written) {
    // Bitwise copy: the read-back verification below compares every stored
    // byte, including tail padding that struct assignment need not preserve.
    memcpy(&settings, &scratch, sizeof(settings));
  }
  const bool saved = written && readSettingsSlot(preferences, target, scratch) &&
                     memcmp(&settings, &scratch, sizeof(scratch)) == 0;
  preferences.end();
  if (saved) {
    durableStorageRevision() = candidateRevision;
    durableStorageRevisionValid() = true;
    durableTimezoneSaved().store(settings.runtime.timezoneId[0] != '\0');
  } else {
    settings.storageRevision = originalRevision;
    settings.checksum = originalChecksum;
  }
  unlockSettingsNvs();
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  return saved;
}

inline bool initializeDefaultSettings(PersistedSettings &settings) {
  settings = PersistedSettings{};
  if (!initializeDefaultDevicePassword(settings) ||
      !initializeDefaultDeviceName(settings)) {
    return false;
  }
  finalizePersistedSettings(settings);
  return true;
}

inline bool resetPersistedSettingsToFactory(PersistedSettings &settings) {
  if (!settingsSchemaWritesAdmitted.load(std::memory_order_acquire)) return false;
  if (!lockSettingsNvs()) {
    return false;
  }
  ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }

  uint32_t existingMax = 0;
  PersistedSettings &probe = persistedSettingsScratch();
  if (readSettingsSlot(preferences, SETTINGS_SLOT_A, probe) &&
      probe.storageRevision > existingMax) {
    existingMax = probe.storageRevision;
  }
  if (readSettingsSlot(preferences, SETTINGS_SLOT_B, probe) &&
      probe.storageRevision > existingMax) {
    existingMax = probe.storageRevision;
  }
  if (durableStorageRevisionValid() &&
      durableStorageRevision() > existingMax) {
    existingMax = durableStorageRevision();
  }
  if (existingMax > UINT32_MAX - 2U) {
    existingMax = UINT32_MAX - 2U;
  }

  PersistedSettings &scratch = persistedSettingsScratch();
  if (!initializeDefaultSettings(scratch)) {
    preferences.end();
    unlockSettingsNvs();
    return false;
  }
  scratch.storageRevision = existingMax + 1U;
  finalizePersistedSettings(scratch);

  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  const bool firstSaved =
      preferences.putBytes(SETTINGS_SLOT_A, &scratch, sizeof(scratch)) ==
      sizeof(scratch);
  const bool firstVerified =
      firstSaved && readSettingsSlot(preferences, SETTINGS_SLOT_A, scratch);
  if (firstVerified) {
    settings = scratch;
  } else if (!initializeDefaultSettings(scratch)) {
    preferences.end();
    unlockSettingsNvs();
    return false;
  }
  scratch.storageRevision = existingMax + 2U;
  finalizePersistedSettings(scratch);
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  const bool secondSaved =
      preferences.putBytes(SETTINGS_SLOT_B, &scratch, sizeof(scratch)) ==
      sizeof(scratch);
  if (secondSaved) {
    // Same invariant as savePersistedSettings: bitwise copy into the caller
    // record before the byte-exact verification memcmp below.
    memcpy(&settings, &scratch, sizeof(settings));
  }
  const bool secondVerified = secondSaved &&
      readSettingsSlot(preferences, SETTINGS_SLOT_B, scratch) &&
      memcmp(&scratch, &settings, sizeof(scratch)) == 0;
  preferences.end();
  unlockSettingsNvs();

  if (!firstVerified && !secondVerified) {
    return false;
  }
  if (secondVerified) {
    settings = scratch;
  } else {
    settings.storageRevision = existingMax + 1U;
    finalizePersistedSettings(settings);
  }
  noteDurableStorageRevision(settings.storageRevision);
  durableTimezoneSaved().store(false);
  return true;
}

}  // namespace shotstopper
