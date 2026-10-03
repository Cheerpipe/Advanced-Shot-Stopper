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

inline bool readSettingsSlot(ShotStopperPreferences &preferences, const char *key,
                             PersistedSettings &settings) {
  if (!preferences.isKey(key) ||
      preferences.getBytesLength(key) != sizeof(PersistedSettings)) {
    return false;
  }
  if (preferences.getBytes(key, &settings, sizeof(settings)) !=
      sizeof(settings)) {
    return false;
  }
  // Both old schemas have the same layout; authenticate before naming padding.
  if ((settings.schemaVersion == 1 || settings.schemaVersion == 2) &&
      settings.magic == PERSISTED_SETTINGS_MAGIC &&
      settings.structureSize == sizeof(settings) &&
      settings.checksum == persistedSettingsChecksum(settings)) {
    if (settings.schemaVersion == 1) {
      settings.runtime.touchStopFallbackEnabled = true;
      for (auto &preset : settings.presets.presets)
        preset.touchStopFallbackEnabled = true;
    }
    settings.lineaMicra.connectionType = static_cast<uint8_t>(MicraConnectionType::WEBSOCKET);
    settings.schemaVersion = CONFIG_SCHEMA_VERSION;
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
  bool loaded = readSettingsSlot(preferences, SETTINGS_SLOT_A, scratch);
  uint32_t loadedRevision = 0;
  if (loaded) {
    settings = scratch;
    loadedRevision = scratch.storageRevision;
  }
  if (readSettingsSlot(preferences, SETTINGS_SLOT_B, scratch) &&
      (!loaded || secondRevisionIsNewer(loadedRevision,
                                        scratch.storageRevision))) {
    settings = scratch;
    loadedRevision = scratch.storageRevision;
    loaded = true;
  }
  preferences.end();
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
