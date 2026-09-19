#pragma once

#include "ShotStopperFlashIoScratch.h"
#include "ShotStopperNvsDualSlot.h"
#include "ShotStopperPersistedNetwork.h"
#include "ShotStopperPersistedSettings.h"
#include "ShotStopperPreferences.h"
#include "ShotStopperSettingsMigrate.h"

#include <stdint.h>

#if !defined(SHOT_STOPPER_HOST_TEST) &&                                        \
    !defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#endif

namespace shotstopper {

inline bool validPersistedSettings(const PersistedSettings &settings) {
  if (settings.magic != PERSISTED_SETTINGS_MAGIC ||
      settings.schemaVersion != CONFIG_SCHEMA_VERSION ||
      settings.structureSize != sizeof(PersistedSettings) ||
      settings.checksum != persistedSettingsChecksum(settings) ||
      validateRuntimeConfig(settings.runtime) != ConfigValidationError::NONE ||
      !validBullseyeMelodyConfig(settings.bullseyeMelody) ||
      !validateShotPresetBank(settings.presets, settings.runtime.retareWindowMs,
                              settings.runtime.autoRetare) ||
      !validDevicePassword(settings.devicePassword) ||
      !validDeviceName(settings.deviceName) ||
      !validMachineIntegrationSettings(settings.machineIntegration) ||
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

// Dual-slot staging inside the shared flash-I/O scratch (see FlashIoScratch).
// Allocated from internal SRAM (heap, not BSS): source/destination of
// Preferences putBytes/getBytes while flash cache is disabled.
inline PersistedSettings &persistedSettingsScratch(uint8_t index) {
  static_assert(2 * sizeof(PersistedSettings) <= FLASH_IO_SCRATCH_BYTES,
                "PersistedSettings dual-slot scratch exceeds flash I/O buffer");
  auto *slots =
      reinterpret_cast<PersistedSettings *>(flashIoScratchBytes());
  return slots[index & 1U];
}

// Legacy migration runs during setup() on the 8 KiB Arduino loop task.
// Legacy blobs stage in this temporary internal allocation — taken only
// while a legacy V1–V13 blob is actually being read (boot migration or a
// post-boot probe while a legacy blob is still stored) and released by the
// callers of readSettingsSlot once the read path finishes. It keeps the
// record off the NVS call chain without permanently reserving scratch.
inline uint8_t *&persistedSettingsMigrationBlock() {
  static uint8_t *block = nullptr;
  return block;
}

inline uint8_t *persistedSettingsMigrationScratch() {
  uint8_t *&block = persistedSettingsMigrationBlock();
  if (block == nullptr) {
    block = static_cast<uint8_t *>(
        allocInternal(sizeof(PersistedSettingsV13), AllocationOwner::FLASH_IO));
  }
  return block;
}

inline void releasePersistedSettingsMigrationScratch() {
  heapCapsFree(persistedSettingsMigrationBlock());
  persistedSettingsMigrationBlock() = nullptr;
}

// A null staging block (allocation failure) reads the legacy blob as
// invalid, failing the migration closed.
inline PersistedSettingsV4 *persistedSettingsV4MigrationScratch() {
  return reinterpret_cast<PersistedSettingsV4 *>(
      persistedSettingsMigrationScratch());
}
inline PersistedSettingsV3 *persistedSettingsV3MigrationScratch() {
  return reinterpret_cast<PersistedSettingsV3 *>(
      persistedSettingsMigrationScratch());
}
inline PersistedSettingsV2 *persistedSettingsV2MigrationScratch() {
  return reinterpret_cast<PersistedSettingsV2 *>(
      persistedSettingsMigrationScratch());
}
inline PersistedSettingsV1 *persistedSettingsV1MigrationScratch() {
  return reinterpret_cast<PersistedSettingsV1 *>(
      persistedSettingsMigrationScratch());
}
// V6–V13 blobs share the 2,616-byte legacy layout. Reading them into this
// scratch avoids stacking the record on the NVS call chain of the 8 KiB
// loop task; migrations write the current-size result into `settings`.
inline PersistedSettingsV13 *persistedSettingsV13MigrationScratch() {
  return reinterpret_cast<PersistedSettingsV13 *>(
      persistedSettingsMigrationScratch());
}
inline PersistedSettingsV14 *persistedSettingsV14MigrationScratch() {
  return reinterpret_cast<PersistedSettingsV14 *>(
      persistedSettingsMigrationScratch());
}

inline bool readSettingsSlot(ShotStopperPreferences &preferences, const char *key,
                             PersistedSettings &settings) {
  if (!preferences.isKey(key)) {
    return false;
  }
  const size_t storedLength = preferences.getBytesLength(key);
  // Staging may contain old data; each path requires a complete read/copy
  // before inspecting it. Default initialization would be overwritten.
  if (storedLength == sizeof(PersistedSettingsV4)) {
    PersistedSettingsV4 *legacy = persistedSettingsV4MigrationScratch();
    if (legacy == nullptr ||
        preferences.getBytes(key, legacy, sizeof(*legacy)) != sizeof(*legacy)) {
      return false;
    }
    if (legacy->schemaVersion == 4) {
      return migratePersistedSettingsFromV4(*legacy, settings) &&
             validPersistedSettings(settings);
    }
    if (legacy->schemaVersion == 5) {
      return migratePersistedSettingsFromV5(*legacy, settings) &&
             validPersistedSettings(settings);
    }
  }
  if (storedLength == sizeof(PersistedSettingsV3)) {
    PersistedSettingsV3 *legacy = persistedSettingsV3MigrationScratch();
    if (legacy == nullptr ||
        preferences.getBytes(key, legacy, sizeof(*legacy)) != sizeof(*legacy)) {
      return false;
    }
    return migratePersistedSettingsFromV3(*legacy, settings) &&
           validPersistedSettings(settings);
  }
  if (storedLength == sizeof(PersistedSettings)) {
    if (preferences.getBytes(key, &settings, sizeof(settings)) !=
        sizeof(settings)) {
      return false;
    }
    return validPersistedSettings(settings);
  }
  if (storedLength == PERSISTED_SETTINGS_V14_SIZE) {
    PersistedSettingsV14 *legacy = persistedSettingsV14MigrationScratch();
    if (legacy == nullptr ||
        preferences.getBytes(key, legacy, sizeof(*legacy)) != sizeof(*legacy)) {
      return false;
    }
    return migratePersistedSettingsFromV14(*legacy, settings) &&
           validPersistedSettings(settings);
  }
  if (storedLength == PERSISTED_SETTINGS_V13_SIZE) {
    PersistedSettingsV13 *legacy = persistedSettingsV13MigrationScratch();
    if (legacy == nullptr ||
        preferences.getBytes(key, legacy, sizeof(*legacy)) != sizeof(*legacy)) {
      return false;
    }
    bool migrated = false;
    switch (legacy->schemaVersion) {
      case 6:
        migrated = migratePersistedSettingsFromV6(*legacy, settings);
        break;
      case 7:
        migrated = migratePersistedSettingsFromV7(*legacy, settings);
        break;
      case 8:
        migrated = migratePersistedSettingsFromV8(*legacy, settings);
        break;
      case 9:
        migrated = migratePersistedSettingsFromV9(*legacy, settings);
        break;
      case 10:
        migrated = migratePersistedSettingsFromV10(*legacy, settings);
        break;
      case 11:
        migrated = migratePersistedSettingsFromV11(*legacy, settings);
        break;
      case 12:
        migrated = migratePersistedSettingsFromV12(*legacy, settings);
        break;
      case 13:
        migrated = migratePersistedSettingsFromV13(*legacy, settings);
        break;
      default:
        return false;
    }
    return migrated && validPersistedSettings(settings);
  }
  if (storedLength == sizeof(PersistedSettingsV2)) {
    PersistedSettingsV2 *legacy = persistedSettingsV2MigrationScratch();
    if (legacy == nullptr ||
        preferences.getBytes(key, legacy, sizeof(*legacy)) != sizeof(*legacy)) {
      return false;
    }
    if (legacy->schemaVersion == 2) {
      return migratePersistedSettingsFromV2(*legacy, settings) &&
             validPersistedSettings(settings);
    }
    if (legacy->schemaVersion == 1) {
      return migratePersistedSettingsFromV1(
                 *reinterpret_cast<PersistedSettingsV1 *>(legacy), settings) &&
             validPersistedSettings(settings);
    }
  }
  return false;
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
  PersistedSettings &first = persistedSettingsScratch(0);
  PersistedSettings &second = persistedSettingsScratch(1);
  first.storageRevision = second.storageRevision = 0;
  bool firstValid = readSettingsSlot(preferences, SETTINGS_SLOT_A, first);
  bool secondValid = readSettingsSlot(preferences, SETTINGS_SLOT_B, second);
  preferences.end();
  releasePersistedSettingsMigrationScratch();

  const DualSlotChoice choice = chooseNewerRevision(
      firstValid, first.storageRevision, secondValid, second.storageRevision);
  bool loaded = false;
  if (choice == DualSlotChoice::SECOND) {
    settings = second;
    loaded = true;
  } else if (choice == DualSlotChoice::FIRST) {
    settings = first;
    loaded = true;
  }
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
  unlockSettingsNvs();
}

inline bool savePersistedSettings(PersistedSettings &settings) {
  // Slots 0 and 1 only: candidate in [1], scratch [0] for revision probe /
  // verify. Never call loadPersistedSettings here — it needs both slots.
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  if (!lockSettingsNvs()) {
    feedSettingsNvsWatchdog();
    return false;
  }
  PersistedSettings &candidate = persistedSettingsScratch(1);
  PersistedSettings &scratch = persistedSettingsScratch(0);
  candidate = settings;
  if (durableStorageRevisionValid()) {
    candidate.storageRevision = durableStorageRevision();
  } else if (candidate.storageRevision == 0) {
    uint32_t revision = 0;
    bool haveRevision = false;
    ShotStopperPreferences probe(NvsSubsystem::SETTINGS);
    if (probe.begin(SETTINGS_NAMESPACE, true)) {
      if (readSettingsSlot(probe, SETTINGS_SLOT_A, scratch)) {
        revision = scratch.storageRevision;
        haveRevision = true;
      }
      if (readSettingsSlot(probe, SETTINGS_SLOT_B, scratch)) {
        if (!haveRevision ||
            secondRevisionIsNewer(revision, scratch.storageRevision)) {
          revision = scratch.storageRevision;
        }
        haveRevision = true;
      }
      probe.end();
    }
    releasePersistedSettingsMigrationScratch();
    if (haveRevision) {
      candidate.storageRevision = revision;
    }
  }
  ++candidate.storageRevision;
  if (candidate.storageRevision == 0) {
    candidate.storageRevision = 1;
  }
  finalizePersistedSettings(candidate);

  ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    feedSettingsNvsWatchdog();
    return false;
  }
  const char *target =
      (candidate.storageRevision & 1U) ? SETTINGS_SLOT_A : SETTINGS_SLOT_B;
  const bool written =
      preferences.putBytes(target, &candidate, sizeof(candidate)) ==
      sizeof(candidate);
  const bool saved = written &&
                     readSettingsSlot(preferences, target, scratch) &&
                     scratch.storageRevision == candidate.storageRevision &&
                     memcmp(&scratch, &candidate, sizeof(candidate)) == 0;
  preferences.end();
  if (saved) {
    durableStorageRevision() = candidate.storageRevision;
    durableStorageRevisionValid() = true;
    settings = candidate;
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
  if (!lockSettingsNvs()) {
    return false;
  }
  ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }

  uint32_t existingMax = 0;
  PersistedSettings &probe = persistedSettingsScratch(0);
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
  releasePersistedSettingsMigrationScratch();
  if (existingMax > UINT32_MAX - 2U) {
    existingMax = UINT32_MAX - 2U;
  }

  PersistedSettings &first = persistedSettingsScratch(0);
  if (!initializeDefaultSettings(first)) {
    preferences.end();
    unlockSettingsNvs();
    return false;
  }
  first.storageRevision = existingMax + 1U;
  finalizePersistedSettings(first);
  PersistedSettings &second = persistedSettingsScratch(1);
  second = first;
  second.storageRevision = existingMax + 2U;
  finalizePersistedSettings(second);

  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  const bool firstSaved =
      preferences.putBytes(SETTINGS_SLOT_A, &first, sizeof(first)) ==
      sizeof(first);
  yieldSettingsNvs();
  feedSettingsNvsWatchdog();
  const bool secondSaved =
      preferences.putBytes(SETTINGS_SLOT_B, &second, sizeof(second)) ==
      sizeof(second);
  const bool firstVerified =
      firstSaved && readSettingsSlot(preferences, SETTINGS_SLOT_A, first);
  const bool secondVerified =
      secondSaved && readSettingsSlot(preferences, SETTINGS_SLOT_B, second);
  preferences.end();
  unlockSettingsNvs();

  if (!firstVerified && !secondVerified) {
    return false;
  }
  settings = secondVerified ? second : first;
  noteDurableStorageRevision(settings.storageRevision);
  return true;
}

}  // namespace shotstopper
