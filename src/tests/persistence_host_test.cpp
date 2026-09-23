#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#include "../ShotStopperPersistence.h"
#include "../ShotStopperBleScanPersistence.h"
#include "../ShotStopperDurableStores.h"
#include "../ShotStopperRecovery.h"
#include "../ShotStopperRecoveryGesture.h"
#include "../ShotStopperShotLog.h"
#include "../ShotStopperShotCurve.h"
#include "../ShotStopperHistory.h"
#include "../ShotStopperLastShot.h"
#include "../machine/ShotStopperLineaMicraSettings.h"

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>

using namespace shotstopper;

namespace {

int failures = 0;
int testsRun = 0;

#define CHECK(expression)                                                     \
  do {                                                                        \
    if (!(expression)) {                                                      \
      std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: "          \
                << #expression << '\n';                                       \
      ++failures;                                                             \
    }                                                                         \
  } while (false)

void resetHostPersistence() {
  persistence_host::reset();
  resetNvsDiagnosticsForHostTest();
  resetDurableStorageRevision();
  ShotLog::resetHostStorage();
  ShotCurveLog::resetHostStorage();
  HistoryLog::resetHostStorage();
  g_hostFlashIoMutexAvailable = true;
  // Reused flash scratch must never supply defaults or stale record fields.
  memset(flashIoScratchBytes(), 0xA5, FLASH_IO_SCRATCH_BYTES);
}

void p01_defaults_are_valid() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(settings.staIpMode == static_cast<uint8_t>(StaIpMode::DHCP));
  CHECK(settings.staConfigState ==
        static_cast<uint8_t>(StaConfigState::CONFIRMED));
  CHECK(!settings.lkgValid);
  CHECK(settings.staWifiSleep);
  CHECK(settings.runtime.showDiagnosticPage);
  CHECK(settings.runtime.autoTareOutsideBrew);
  CHECK(settings.runtime.powerManagementEnabled);
  CHECK(!settings.webhook.deferDuringShot);
  CHECK(settings.runtime.fastExtractionGuardEnabled);
  CHECK(std::fabs(settings.runtime.maxRecoveryWeightG -
                  DEFAULT_MAX_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(settings.runtime.minBbwBrewTimeMs == DEFAULT_MIN_BBW_BREW_TIME_MS);
  CHECK(settings.runtime.slowExtractionGuardEnabled);
  CHECK(std::fabs(settings.runtime.minRecoveryWeightG -
                  DEFAULT_MIN_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(settings.runtime.maxBbwBrewTimeMs == DEFAULT_MAX_BBW_BREW_TIME_MS);
  CHECK(settings.runtime.autoToManualGuardEnabled);
  CHECK(settings.runtime.autoToManualGuardLimitMode ==
        static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO));
  CHECK(settings.runtime.autoToManualGuardManualLimitMs ==
        DEFAULT_AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT_MS);
  CHECK(settings.runtime.autoToManualGuardBaselineMs ==
        DEFAULT_AUTO_TO_MANUAL_GUARD_BASELINE_MS);
  CHECK(std::fabs(settings.runtime.weightOffsetBaselineG -
                  DEFAULT_WEIGHT_OFFSET_G) < 0.001f);
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    CHECK(settings.runtime.autoToManualGuardSamplesDs[i] ==
          AUTO_TO_MANUAL_GUARD_DEFAULT_SAMPLE_DS);
  }
  CHECK(validPersistedSettings(settings));
  CHECK(passwordIsFactoryDefault(settings));
  CHECK(passwordIsFactoryDefault(settings));
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.preferredScaleName[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::ONLY));
  CHECK(settings.runtime.paddleMode ==
        static_cast<uint8_t>(PaddleMode::NATURAL));
  CHECK(runtimeStopPulseMs(settings.runtime) == COMPILED_STOP_PULSE_MS);
  CHECK(runtimeMaxSinglePressMs(settings.runtime) ==
        COMPILED_MAX_SINGLE_PRESS_MS);
  CHECK(settings.runtime.momentaryStartOnPress);
  CHECK(settings.runtime.reedConfirmTimeoutHundredMs == 0);
  CHECK(runtimeReedConfirmTimeoutMs(settings.runtime) ==
        COMPILED_REED_CONFIRM_TIMEOUT_MS);
  CHECK(settings.runtime.alertOutputChannel ==
        static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL));
  CHECK(!settings.runtime.soundAlertsMuted);
  CHECK(DEFAULT_ALERT_OUTPUT_CHANNEL ==
        (BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY
                                : AlertOutputChannel::SCALE_ONLY));
  CHECK(settings.runtime.bookooMuteOnBuzzerOnly);
  CHECK(settings.runtime.bookooConnectBeepLevel ==
        DEFAULT_BOOKOO_CONNECT_BEEP_LEVEL);
  CHECK(settings.runtime.buzzerExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(settings.runtime.buzzerSlowExtendedPulseRate ==
        static_cast<uint8_t>(DEFAULT_EXTENDED_PULSE_RATE));
  CHECK(settings.runtime.noScaleBbwMode ==
        static_cast<uint8_t>(NoScaleBbwMode::WARN_ONCE));
  CHECK(!noScaleAllowRinseWhileArmed(settings.runtime.noScaleBbwMode));
  CHECK(settings.runtime.cupProtectionEnabled);
  CHECK(settings.runtime.stopIfCupRemoved);
  CHECK(!settings.runtime.requireCupToStart);
  CHECK(settings.runtime.avoidAccidentalTouchEnabled);
  CHECK(std::fabs(settings.runtime.cupPresentWeightG -
                  DEFAULT_CUP_PRESENT_WEIGHT_G) < 0.001f);
  CHECK(std::fabs(settings.runtime.cupRemovedWeightG -
                  DEFAULT_CUP_REMOVED_WEIGHT_G) < 0.001f);
  CHECK(settings.runtime.lastShotCooldownMs == DEFAULT_LAST_SHOT_COOLDOWN_MS);
  CHECK(settings.runtime.dripDelayMs == DEFAULT_DRIP_DELAY_MS);
  CHECK(settings.runtime.postTareBaselineGraceMs ==
        DEFAULT_POST_TARE_BASELINE_GRACE_MS);
  CHECK(serialLogLevelFromRuntime(settings.runtime) == LogLevel::NONE);
  CHECK(settings.runtime.ringRetainLogLevel ==
        static_cast<uint8_t>(LogLevel::NONE));
  CHECK(settings.runtime.buzzerScaleConnectedBeep);
  CHECK(settings.runtime.scaleConnectedLed);
  CHECK(validPreferredScaleMac(settings.preferredScaleMac));
  CHECK(validPreferredScaleName(settings.preferredScaleName));
  CHECK(validPreferredScaleName("Pearl-S"));
  CHECK(!validPreferredScaleName("bad\"name"));
  CHECK(validPreferredScaleMac("AA:BB:CC:DD:EE:FF"));
  CHECK(validPreferredScaleMac("aa:bb:cc:dd:ee:ff"));
  CHECK(!validPreferredScaleMac("AA:BB:CC:DD:EE"));
  CHECK(!validPreferredScaleMac("GG:BB:CC:DD:EE:FF"));
}

void p02_newest_valid_slot_is_loaded() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(savePersistedSettings(settings));
  const uint32_t firstRevision = settings.storageRevision;
  settings.runtime.goalWeightG = 47;
  settings.runtime.maxRecoveryWeightG = 55.0f;
  settings.runtime.soundAlertsMuted = true;
  settings.runtime.cupProtectionEnabled = false;
  settings.runtime.stopIfCupRemoved = false;
  settings.runtime.requireCupToStart = true;
  settings.runtime.avoidAccidentalTouchEnabled = false;
  settings.runtime.cupPresentWeightG = 4.5f;
  settings.runtime.cupRemovedWeightG = -6.0f;
  settings.presets.presets[0].cupProtectionEnabled = false;
  settings.presets.presets[0].stopIfCupRemoved = false;
  settings.presets.presets[0].requireCupToStart = true;
  settings.presets.presets[0].avoidAccidentalTouchEnabled = false;
  settings.presets.presets[0].bbwEwmaOffsetG = 4.5f;
  settings.presets.presets[0].bbwEwmaAlpha = 50;
  settings.presets.presets[0].bbwAlphaLearned = 1;
  CHECK(savePersistedSettings(settings));
  CHECK(settings.storageRevision == firstRevision + 1);

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 47);
  CHECK(loaded.runtime.soundAlertsMuted);
  CHECK(!loaded.runtime.cupProtectionEnabled);
  CHECK(!loaded.runtime.stopIfCupRemoved);
  CHECK(loaded.runtime.requireCupToStart);
  CHECK(!loaded.runtime.avoidAccidentalTouchEnabled);
  CHECK(std::fabs(loaded.runtime.cupPresentWeightG - 4.5f) < 0.001f);
  CHECK(std::fabs(loaded.runtime.cupRemovedWeightG - (-6.0f)) < 0.001f);
  CHECK(!loaded.presets.presets[0].cupProtectionEnabled);
  CHECK(!loaded.presets.presets[0].stopIfCupRemoved);
  CHECK(loaded.presets.presets[0].requireCupToStart);
  CHECK(!loaded.presets.presets[0].avoidAccidentalTouchEnabled);
  CHECK(std::fabs(loaded.presets.presets[0].bbwEwmaOffsetG - 4.5f) < 0.001f);
  CHECK(loaded.presets.presets[0].bbwEwmaAlpha == 50);
  CHECK(loaded.presets.presets[0].bbwAlphaLearned == 1);
}

void p02b_save_uses_ram_revision_when_slots_unreadable() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(savePersistedSettings(settings));
  CHECK(settings.storageRevision != 0);
  persistence_host::records.clear();
  settings.runtime.goalWeightG = 41;
  settings.runtime.maxRecoveryWeightG = 50.0f;
  CHECK(savePersistedSettings(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 41);
  CHECK(loaded.storageRevision == settings.storageRevision);
  const uint32_t revision = settings.storageRevision;
  persistence_host::corruptNextWrite = true;
  CHECK(!savePersistedSettings(settings));
  CHECK(settings.storageRevision == revision);
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.storageRevision == revision);
}

void p02c_overlay_live_runtime_is_saved_not_stale_blob() {
  resetHostPersistence();
  PersistedSettings stale;
  CHECK(initializeDefaultSettings(stale));
  stale.runtime.goalWeightG = 36;
  CHECK(savePersistedSettings(stale));

  RuntimeConfig live = stale.runtime;
  live.goalWeightG = 44;
  live.maxRecoveryWeightG = 53.0f;
  ShotPresetBank livePresets = stale.presets;
  overlayLivePersistedSettings(stale, live, livePresets);
  CHECK(stale.runtime.goalWeightG == 44);
  CHECK(savePersistedSettings(stale));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 44);
  CHECK(loaded.storageRevision == stale.storageRevision);
}

void p02d_single_scratch_handles_wrap_and_external_destination() {
  resetHostPersistence();
  PersistedSettings beforeWrap;
  CHECK(initializeDefaultSettings(beforeWrap));
  beforeWrap.storageRevision = UINT32_MAX;
  beforeWrap.runtime.goalWeightG = 40;
  finalizePersistedSettings(beforeWrap);
  PersistedSettings afterWrap = beforeWrap;
  afterWrap.storageRevision = 1;
  afterWrap.runtime.goalWeightG = 48;
  afterWrap.runtime.maxRecoveryWeightG = 55.0f;
  finalizePersistedSettings(afterWrap);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &beforeWrap,
                           sizeof(beforeWrap));
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_B, &afterWrap,
                           sizeof(afterWrap));

  auto *loaded = static_cast<PersistedSettings *>(
      allocExternal(sizeof(PersistedSettings)));
  CHECK(loaded != nullptr);
  CHECK(loadPersistedSettings(*loaded));
  CHECK(loaded->storageRevision == 1);
  CHECK(loaded->runtime.goalWeightG == 48);
  heapCapsFree(loaded);
}

void p03_corrupt_newest_slot_falls_back() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 40;
  CHECK(savePersistedSettings(settings));
  settings.runtime.goalWeightG = 48;
  settings.runtime.maxRecoveryWeightG = 55.0f;
  CHECK(savePersistedSettings(settings));
  CHECK(persistence_host::corrupt(SETTINGS_NAMESPACE, SETTINGS_SLOT_B,
                                  offsetof(PersistedSettings, runtime)));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 40);
}

void p04_crc_and_semantic_validation_reject_corruption() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 50;
  settings.runtime.maxRecoveryWeightG = 58.0f;
  CHECK(!validPersistedSettings(settings));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
}

void p05_password_change_updates_hash() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(!setDevicePassword(settings, DEFAULT_DEVICE_PASSWORD));
  CHECK(setDevicePassword(settings, "NuevaClaveSegura"));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(strcmp(settings.devicePassword, "NuevaClaveSegura") == 0);
  CHECK(!passwordIsFactoryDefault(settings));
}

void p18_shot_log_keeps_history_when_inactive_slot_write_fails() {
  resetHostPersistence();
  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightG = 36;
  record.actualWeightCg = 3600;
  CHECK(log.append(record));
  CHECK(log.count() == 1);

  ShotLog::setHostSaveSucceeds(false);
  ShotLogRecord second = {};
  second.durationDs = 260;
  second.goalWeightG = 36;
  CHECK(!log.append(second));
  ShotLog::setHostSaveSucceeds(true);
  CHECK(log.count() == 1);

  ShotLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 1);
  ShotLogRecord out[1] = {};
  CHECK(reloaded.copyNewestFirst(out, 1) == 1);
  CHECK(out[0].durationDs == 250);
}

void p19_shot_log_weight_sentinel_allows_int16_max() {
  CHECK(shotLogWeightToCentigrams(327.67f) == INT16_MAX);
  CHECK(shotLogWeightIsMissing(SHOT_LOG_WEIGHT_MISSING));
  CHECK(shotLogWeightIsMissing(SHOT_LOG_WEIGHT_MISSING_LEGACY));
  CHECK(!shotLogWeightIsMissing(3600));
  CHECK(shotLogWeightToCentigrams(400.0f) == SHOT_LOG_WEIGHT_MISSING);
}

void p29_last_shot_persists_and_clears() {
  resetHostPersistence();
  LastShotStore store;
  CHECK(store.load());
  CHECK(!store.get().valid);
  CHECK(!publishableLastShot(store.getGood()));

  PersistedLastShot shot = {};
  shot.valid = true;
  shot.cycleId = 42;
  shot.durationMs = 28500;
  shot.goalWeightG = 36;
  shot.weightValid = true;
  shot.currentWeightG = 36.2f;
  shot.shotType = static_cast<uint8_t>(LastShotType::AUTO);
  shot.noScaleShotGuardEnabled = true;
  shot.noScaleShotGuardArmed = true;
  shot.rating = 4;
  shot.shotLogId = 9;
  shot.endedAtUptimeMs = 123456;
  shot.presetId = 2;
  shot.averageFlowGps = 1.57f;
  shot.averageFlowValid = true;
  strcpy(shot.presetName, "Double");
  strcpy(shot.scaleProtocol, "acaia");
  CHECK(store.persist(shot));
  CHECK(persistence_host::records.count("lastshot/record") == 1);

  LastShotStore reloaded;
  CHECK(reloaded.load());
  CHECK(!reloaded.loadedLegacy());
  CHECK(reloaded.get().valid);
  CHECK(reloaded.get().cycleId == 42);
  CHECK(reloaded.get().goalWeightG == 36);
  CHECK(fabs(reloaded.get().currentWeightG - 36.2f) < 0.001f);
  CHECK(reloaded.get().noScaleShotGuardEnabled);
  CHECK(reloaded.get().noScaleShotGuardArmed);
  CHECK(reloaded.get().rating == 4);
  CHECK(reloaded.get().shotLogId == 9);
  CHECK(reloaded.get().endedAtUptimeMs == 123456);
  CHECK(reloaded.get().presetId == 2);
  CHECK(strcmp(reloaded.get().presetName, "Double") == 0);
  CHECK(strcmp(reloaded.get().scaleProtocol, "acaia") == 0);
  CHECK(reloaded.getGood().cycleId == 42);
  CHECK(reloaded.getGood().averageFlowValid);
  CHECK(fabs(reloaded.getGood().averageFlowGps - 1.57f) < 0.001f);

  g_hostFlashIoMutexAvailable = false;
  CHECK(!reloaded.clearLast());
  CHECK(reloaded.get().cycleId == 42);
  g_hostFlashIoMutexAvailable = true;
  LastShotStore afterFailedClear;
  CHECK(afterFailedClear.load());
  CHECK(afterFailedClear.get().cycleId == 42);

  CHECK(reloaded.clearLast());
  CHECK(!reloaded.get().valid);
  CHECK(reloaded.getGood().cycleId == 42);

  CHECK(reloaded.clear());
  LastShotStore emptied;
  CHECK(emptied.load());
  CHECK(!emptied.get().valid);
  CHECK(!emptied.getGood().valid);

  resetHostPersistence();
  LastShotBlobV3 v3 = {};
  v3.magic = LAST_SHOT_MAGIC;
  v3.schemaVersion = 3;
  v3.structureSize = sizeof(v3);
  memcpy(v3.shot, &shot, sizeof(v3.shot));
  v3.checksum = lastShotV3Checksum(v3);
  persistence_host::putRaw("lastshot", "record", &v3, sizeof(v3));
  LastShotStore migratedV3;
  CHECK(migratedV3.load());
  CHECK(migratedV3.loadedLegacy());
  CHECK(migratedV3.get().cycleId == 42);
  CHECK(migratedV3.getGood().cycleId == 42);
  CHECK(!migratedV3.get().averageFlowValid);
  CHECK(publishableLastShot(migratedV3.getGood()));
  CHECK(migratedV3.save());
  LastShotStore migratedV3Reboot;
  CHECK(migratedV3Reboot.load());
  CHECK(!migratedV3Reboot.loadedLegacy());
  CHECK(migratedV3Reboot.getGood().cycleId == 42);

  resetHostPersistence();
  LastShotBlobV2 legacy = {};
  legacy.magic = LAST_SHOT_MAGIC;
  legacy.schemaVersion = 2;
  legacy.structureSize = sizeof(legacy);
  memcpy(legacy.shot, &shot, sizeof(legacy.shot));
  legacy.checksum = lastShotV2Checksum(legacy);
  persistence_host::putRaw("lastshot", "record", &legacy, sizeof(legacy));
  LastShotStore migrated;
  CHECK(migrated.load());
  CHECK(migrated.loadedLegacy());
  CHECK(migrated.get().cycleId == 42);
  CHECK(migrated.get().endedAtUptimeMs == 0);
  CHECK(migrated.get().presetId == 0);
  CHECK(migrated.getGood().cycleId == 42);
  CHECK(!publishableLastShot(migrated.getGood()));
  CHECK(migrated.save());
  LastShotStore migratedReboot;
  CHECK(migratedReboot.load());
  CHECK(!migratedReboot.loadedLegacy());
  CHECK(migratedReboot.get().presetId == 0);
}

void p80_boot_id_remains_dirty_until_durable() {
  resetHostPersistence();
  ShotLog log;
  CHECK(log.load());
  log.onBoot();
  CHECK(log.dirty());
  CHECK(log.save());
  const uint32_t durableBoot = log.bootId();

  ShotLog rebooted;
  CHECK(rebooted.load());
  rebooted.onBoot();
  CHECK(rebooted.bootId() == durableBoot + 1);
  g_hostFlashIoMutexAvailable = false;
  CHECK(!rebooted.save());
  CHECK(rebooted.dirty());
  g_hostFlashIoMutexAvailable = true;

  ShotLog stillDurable;
  CHECK(stillDurable.load());
  CHECK(stillDurable.bootId() == durableBoot);
  CHECK(rebooted.flush());
  ShotLog afterRetry;
  CHECK(afterRetry.load());
  CHECK(afterRetry.bootId() == durableBoot + 1);
}

void p07_invalid_schema_uses_factory_on_missing_slots() {
  resetHostPersistence();
  PersistedSettings loaded;
  CHECK(!loadPersistedSettings(loaded));
}

void p08_factory_reset_rebuilds_defaults() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 63;
  settings.runtime.maxRecoveryWeightG = 70.0f;
  strcpy(settings.preferredScaleMac, "AA:BB:CC:DD:EE:FF");
  strcpy(settings.preferredScaleName, "Lunar");
  settings.runtime.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(settings.staWifiSleep);
  CHECK(settings.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
  CHECK(settings.runtime.fastExtractionGuardEnabled);
  CHECK(settings.runtime.autoToManualGuardEnabled);
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.preferredScaleName[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::ONLY));
  for (const ScaleHistoryEntry &entry : settings.scaleHistory) {
    CHECK(entry.mac[0] == '\0');
    CHECK(entry.name[0] == '\0');
    CHECK(entry.lastSeenSeq == 0);
  }
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(verifyFactorySettings(loaded));
  CHECK(loaded.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
}

void p08b_scale_preference_without_mac_round_trips() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::ONLY));
  CHECK(savePersistedSettings(settings));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.preferredScaleMac[0] == '\0');
  CHECK(loaded.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::ONLY));

  loaded.runtime.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::FIRST);
  CHECK(savePersistedSettings(loaded));
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::FIRST));
}

void p64_factory_settings_overwrite_does_not_clear_ble_namespace() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 63;
  settings.runtime.maxRecoveryWeightG = 70.0f;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  BleScanPersistedSettings ble;
  ble.scanIntensity = static_cast<uint8_t>(BleScanIntensity::RELAXED);
  CHECK(saveBleScanSettings(ble));
  CHECK(persistence_host::records.count("shotstopper/bleCfgA") +
            persistence_host::records.count("shotstopper/bleCfgB") >=
        1);
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(verifyFactorySettings(settings));
  CHECK(persistence_host::records.count("shotstopper/bleCfgA") +
            persistence_host::records.count("shotstopper/bleCfgB") >=
        1);
  BleScanPersistedSettings reloadedBle;
  CHECK(loadBleScanSettings(reloadedBle));
  CHECK(reloadedBle.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::RELAXED));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(verifyFactorySettings(loaded));
}

void p82_ble_scan_backoff_boost_migration_and_roundtrip() {
  resetHostPersistence();
  // A V2 blob kept the backoff and boost bytes reserved (always 0); loading it
  // must adopt the default backoff and boost (now OFF) without losing the
  // saved intensity.
  BleScanPersistedSettings v2;
  v2.version = BLE_SCAN_SETTINGS_V2_VERSION;
  v2.revision = 7;
  v2.scanIntensity = static_cast<uint8_t>(BleScanIntensity::RELAXED);
  v2.checksum = bleScanSettingsChecksum(v2);
  persistence_host::putRaw(SETTINGS_NAMESPACE, BLE_SCAN_SLOT_A, &v2,
                           sizeof(v2));
  BleScanPersistedSettings loaded;
  CHECK(loadBleScanSettings(loaded));
  CHECK(loaded.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(loaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::RELAXED));
  CHECK(loaded.scanBackoffMin == SCALE_SCAN_QUIET_BACKOFF_DEFAULT_MIN);
  CHECK(loaded.scanBoostMin == SCALE_SCAN_BOOST_DEFAULT_MIN);

  // A V3 blob stored the backoff but kept the boost byte reserved: the stored
  // backoff choice survives while the boost adopts OFF.
  BleScanPersistedSettings v3;
  v3.version = BLE_SCAN_SETTINGS_V3_VERSION;
  v3.revision = 8;
  v3.scanIntensity = static_cast<uint8_t>(BleScanIntensity::BALANCED);
  v3.scanBackoffMin = 30;
  v3.checksum = bleScanSettingsChecksum(v3);
  persistence_host::putRaw(SETTINGS_NAMESPACE, BLE_SCAN_SLOT_A, &v3,
                           sizeof(v3));
  CHECK(loadBleScanSettings(loaded));
  CHECK(loaded.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(loaded.scanBackoffMin == 30);
  CHECK(loaded.scanBoostMin == SCALE_SCAN_BOOST_DEFAULT_MIN);

  CHECK(persistBleScanSettings(loaded, loaded.scanIntensity, 30, 15));
  CHECK(loaded.scanBoostMin == 15);
  BleScanPersistedSettings reloaded;
  CHECK(loadBleScanSettings(reloaded));
  CHECK(reloaded.scanBackoffMin == 30);
  CHECK(reloaded.scanBoostMin == 15);
  CHECK(reloaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::BALANCED));

  // Zero is a stored choice ("off"), never an unset marker.
  CHECK(persistBleScanSettings(reloaded, reloaded.scanIntensity, 0, 0));
  CHECK(loadBleScanSettings(loaded));
  CHECK(loaded.scanBackoffMin == 0);
  CHECK(loaded.scanBoostMin == 0);

  // All three fields in one call cost a single revision bump.
  const uint32_t revisionBefore = loaded.revision;
  CHECK(persistBleScanSettings(loaded,
                               static_cast<uint8_t>(BleScanIntensity::RELAXED),
                               60, 45));
  CHECK(loadBleScanSettings(reloaded));
  CHECK(reloaded.revision == revisionBefore + 1);
  CHECK(reloaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::RELAXED));
  CHECK(reloaded.scanBackoffMin == 60);
  CHECK(reloaded.scanBoostMin == 45);

  BleScanPersistedSettings factory;
  finalizeBleScanSettings(factory);
  CHECK(verifyFactoryBleScanSettings(factory));
}

void p65_factory_settings_survives_second_slot_write_fail() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 63;
  settings.runtime.maxRecoveryWeightG = 70.0f;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  persistence_host::failNextWriteForKey = SETTINGS_SLOT_B;
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(verifyFactorySettings(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(verifyFactorySettings(loaded));
  CHECK(loaded.runtime.goalWeightG == DEFAULT_GOAL_WEIGHT_G);
}

void p66_shot_log_dual_slot_generation_flip() {
  resetHostPersistence();
  CHECK(sizeof(ShotLogStore) == 7256);
  CHECK(sizeof(ShotLogStore) % 4 == 0);
  CHECK(sizeof(ShotLogStore) <= SHOT_LOG_FLASH_SLOT_BYTES);
  CHECK(SHOT_LOG_FLASH_SLOT_COUNT * SHOT_LOG_FLASH_SLOT_BYTES <= 0x8000);
  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightG = 36;
  record.actualWeightCg = 3600;
  CHECK(log.append(record));
  CHECK(log.count() == 1);

  // A failed inactive-slot write leaves the last-good slot active and the
  // durable history intact.
  ShotLog::setHostSaveSucceeds(false);
  ShotLogRecord second = {};
  second.durationDs = 260;
  second.goalWeightG = 36;
  CHECK(!log.append(second));
  ShotLog::setHostSaveSucceeds(true);
  CHECK(log.count() == 1);

  // Every successful save flips to the other slot and bumps the generation;
  // load() keeps picking the newer generation across reboots.
  CHECK(log.append(second));
  CHECK(log.count() == 2);
  ShotLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 2);
  ShotLogRecord out[2] = {};
  CHECK(reloaded.copyNewestFirst(out, 2) == 2);
  CHECK(out[0].durationDs == 260);
  CHECK(out[1].durationDs == 250);
}

void p09_fast_extraction_guard_validation() {
  RuntimeConfig config = {};
  config.goalWeightG = 36;
  config.maxRecoveryWeightG = 42.5f;
  config.minBbwBrewTimeMs = 26000;
  config.fastExtractionGuardEnabled = true;
  config.bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  config.operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.maxRecoveryWeightG = 36.0f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::FAST_EXTRACTION_GUARD_RELATION);

  config = {};
  config.goalWeightG = 36;
  config.minRecoveryWeightG = 30.0f;
  config.maxBbwBrewTimeMs = 44000;
  config.slowExtractionGuardEnabled = true;
  config.bbwProtectionMs = DEFAULT_BBW_PROTECTION_MS;
  config.operationalWallMs = DEFAULT_OPERATIONAL_WALL_MS;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.minRecoveryWeightG = 36.0f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION);
  config.minRecoveryWeightG = 30.0f;
  config.fastExtractionGuardEnabled = true;
  config.maxRecoveryWeightG = 42.5f;
  config.minBbwBrewTimeMs = 28000;
  config.maxBbwBrewTimeMs = 28000;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::SLOW_EXTRACTION_GUARD_RELATION);
}

void p10_auto_to_manual_guard_trend_and_validation() {
  uint16_t samples[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {300, 300, 300, 300,
                                                         300};
  CHECK(autoToManualGuardTrendMs(samples, 60000) == 30000);
  samples[0] = 200;
  samples[1] = 220;
  samples[2] = 240;
  samples[3] = 260;
  samples[4] = 280;
  const uint32_t rising = autoToManualGuardTrendMs(samples, 60000);
  CHECK(rising == 30000);

  RuntimeConfig config = {};
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.autoToManualGuardManualLimitMs = 5000;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::AUTO_TO_MANUAL_GUARD_MANUAL_LIMIT);
  config.autoToManualGuardManualLimitMs = 30000;
  config.autoToManualGuardBaselineMs = 5000;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::AUTO_TO_MANUAL_GUARD_BASELINE);
  config.autoToManualGuardBaselineMs = 30000;
  config.autoToManualGuardLimitMode = 9;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::AUTO_TO_MANUAL_GUARD_MODE);
  config.autoToManualGuardLimitMode =
      static_cast<uint8_t>(AutoToManualGuardLimitMode::AUTO);
  config.weightOffsetBaselineG = -0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::WEIGHT_OFFSET_BASELINE);
  config.weightOffsetBaselineG = MAX_OFFSET_G + 0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::WEIGHT_OFFSET_BASELINE);
  config.weightOffsetBaselineG = 2.25f;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);

  uint16_t seeded[AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT] = {};
  resetAutoToManualGuardSamples(seeded, 28000);
  for (size_t i = 0; i < AUTO_TO_MANUAL_GUARD_SAMPLE_COUNT; ++i) {
    CHECK(seeded[i] == 280);
  }
}

void p12_shot_log_persists_compact_blob() {
  resetHostPersistence();
  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightG = 36;
  record.actualWeightCg = 3600;
  record.actualWeightSource =
      static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  strcpy(record.presetName, "Double");
  CHECK(log.append(record));
  CHECK(log.count() == 1);

  ShotLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 1);
  ShotLogRecord out[1] = {};
  CHECK(reloaded.copyNewestFirst(out, 1) == 1);
  CHECK(out[0].goalWeightG == 36);
  CHECK(out[0].actualWeightSource ==
        static_cast<uint8_t>(ActualWeightSource::POST_DRIP));
  CHECK(strcmp(out[0].presetName, "Double") == 0);
  CHECK(shotLogPackGuardFlags(true, true) ==
        (SHOT_LOG_FAST_GUARD_BIT | SHOT_LOG_SLOW_GUARD_BIT));
  CHECK(shotLogSlowGuardEnabled(shotLogPackGuardFlags(false, true)));
  CHECK(shotLogSlowExtended(shotLogPackExtendedFlags(false, true)));
  CHECK(strcmp(shotLogStopDetailName(ShotLogStopDetail::SLOW_MAX_TIME),
               "slow_max_time") == 0);
  CHECK(strcmp(shotLogStopDetailName(ShotLogStopDetail::SLOW_MIN_WEIGHT),
               "slow_min_weight") == 0);
}

void p47_rejects_non_current_schema_blob() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  finalizePersistedSettings(settings);
  settings.schemaVersion = 31;
  settings.checksum = 0;
  settings.checksum = persistedSettingsChecksum(settings);
  // Wrong schemaVersion must fail validPersistedSettings even with matching CRC.
  CHECK(!validPersistedSettings(settings));
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &settings,
                           sizeof(settings));
  PersistedSettings loaded;
  CHECK(!loadPersistedSettings(loaded));
}

void p47f_bullseye_melody_persists_as_fixed_record() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.bullseyeMelody.enabled = true;
  copyCString(settings.bullseyeMelody.rtttl,
              sizeof(settings.bullseyeMelody.rtttl),
              "bullseye:d=8,o=5,b=180:c,e,g,c6");
  CHECK(savePersistedSettings(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.bullseyeMelody.enabled);
  CHECK(strcmp(loaded.bullseyeMelody.rtttl,
               "bullseye:d=8,o=5,b=180:c,e,g,c6") == 0);
}

void p47h_webhook_url_validation_is_http_only() {
  CHECK(validWebhookUrl("http://192.168.1.10/hook"));
  CHECK(validWebhookUrl("http://server.local:8123/api/webhook?x=1"));
  CHECK(validWebhookUrl("http://[fd00::1]:8123/hook"));
  CHECK(!validWebhookUrl("https://server.local/hook"));
  CHECK(!validWebhookUrl("HTTP://server.local/hook"));
  CHECK(!validWebhookUrl("http://user@server.local/hook"));
  CHECK(!validWebhookUrl("http://server.local/hook#fragment"));
  CHECK(!validWebhookUrl("http://server.local/bad path"));
  CHECK(!validWebhookUrl("http://server.local/\"bad\""));
  CHECK(!validWebhookUrl("http://:80/hook"));
  CHECK(!validWebhookUrl("http://server.local:/hook"));
  CHECK(!validWebhookUrl("http://server.local:0/hook"));
  CHECK(!validWebhookUrl("http://server.local:65536/hook"));
  CHECK(!validWebhookUrl("http://server.local:abc/hook"));
  WebhookConfig disabled;
  CHECK(validWebhookConfig(disabled));
  copyCString(disabled.url, sizeof(disabled.url), "https://server.local/hook");
  CHECK(!validWebhookConfig(disabled));
  disabled.url[0] = '\0';
  disabled.enabled = true;
  CHECK(!validWebhookConfig(disabled));
}

void p47c_desired_wifi_power_save_policy() {
  using M = WifiPowerSaveMode;
  CHECK(desiredWifiPowerSave(false, false, true, false) == M::NONE);
  CHECK(desiredWifiPowerSave(false, true, false, false) == M::NONE);
  CHECK(desiredWifiPowerSave(true, false, true, false) == M::MIN_MODEM);
  CHECK(desiredWifiPowerSave(true, true, true, false) == M::NONE);
  CHECK(desiredWifiPowerSave(true, false, false, false) == M::NONE);
  CHECK(desiredWifiPowerSave(true, true, true, true) == M::NONE);
  CHECK(desiredWifiPowerSave(true, false, true, true) == M::NONE);
  CHECK(strcmp(wifiPsLiveName(WifiPsLive::NONE), "NONE") == 0);
  CHECK(strcmp(wifiPsLiveName(WifiPsLive::MIN_MODEM), "MIN_MODEM") == 0);
  CHECK(strcmp(wifiPsLiveName(WifiPsLive::MAX_MODEM), "MAX_MODEM") == 0);
  CHECK(strcmp(wifiPsLiveName(WifiPsLive::UNKNOWN), "UNKNOWN") == 0);
}

void p47d_durable_flash_write_gate() {
  CHECK(durableFlashWriteAllowed(false, false));
  CHECK(!durableFlashWriteAllowed(true, false));
  CHECK(!durableFlashWriteAllowed(false, true));
  CHECK(!durableFlashWriteAllowed(true, true));
}

void p46_ring_retain_log_level_persists_round_trip() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.runtime.ringRetainLogLevel ==
        static_cast<uint8_t>(LogLevel::NONE));
  settings.runtime.ringRetainLogLevel = static_cast<uint8_t>(LogLevel::INFO);
  CHECK(savePersistedSettings(settings));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.ringRetainLogLevel ==
        static_cast<uint8_t>(LogLevel::INFO));
}

void p43_scale_history_upsert_and_lru() {
  ScaleHistoryEntry entries[SCALE_HISTORY_CAPACITY] = {};
  uint32_t seq = 0;
  CHECK(upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:01", "One"));
  CHECK(upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:02", "Two"));
  CHECK(!upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:01", "One"));
  // Case-insensitive: lowercase must not create a duplicate slot.
  CHECK(!upsertScaleHistory(entries, seq, "aa:bb:cc:dd:ee:01", "One"));
  CHECK(scaleHistoryOccupiedCount(entries) == 2);
  CHECK(strcmp(entries[0].mac, "AA:BB:CC:DD:EE:01") == 0);
  for (uint8_t i = 3; i <= 9; ++i) {
    char mac[PREFERRED_SCALE_MAC_CAPACITY];
    snprintf(mac, sizeof(mac), "AA:BB:CC:DD:EE:%02X", i);
    CHECK(upsertScaleHistory(entries, seq, mac, "X"));
  }
  CHECK(scaleHistoryOccupiedCount(entries) == SCALE_HISTORY_CAPACITY);
  bool foundOne = false;
  bool foundTwo = false;
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (strcmp(entries[i].mac, "AA:BB:CC:DD:EE:01") == 0) {
      foundOne = true;
    }
    if (strcmp(entries[i].mac, "AA:BB:CC:DD:EE:02") == 0) {
      foundTwo = true;
    }
  }
  // Refreshing :01 made it newest; :02 is the LRU victim when the table fills.
  CHECK(foundOne);
  CHECK(!foundTwo);
}

void p70_scale_history_session_consumption_is_bounded_and_reopenable() {
  ScaleHistoryEntry entries[SCALE_HISTORY_CAPACITY] = {};
  uint32_t seq = 0;
  for (uint8_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    char mac[PREFERRED_SCALE_MAC_CAPACITY];
    snprintf(mac, sizeof(mac), "AA:BB:CC:DD:EE:%02X", i);
    CHECK(consumeScaleHistorySessionConnection(entries, seq, mac, "Bookoo"));
    CHECK(!consumeScaleHistorySessionConnection(entries, seq, mac, "Bookoo"));
  }
  CHECK(scaleHistoryOccupiedCount(entries) == SCALE_HISTORY_CAPACITY);
  CHECK(!consumeScaleHistorySessionConnection(
      entries, seq, "AA:BB:CC:DD:EE:99", "Overflow"));
  CHECK(scaleHistoryOccupiedCount(entries) == SCALE_HISTORY_CAPACITY);

  reopenScaleHistorySessionConnection(entries, "aa:bb:cc:dd:ee:03");
  CHECK(consumeScaleHistorySessionConnection(
      entries, seq, "AA:BB:CC:DD:EE:03", "Bookoo"));
  CHECK(!consumeScaleHistorySessionConnection(
      entries, seq, "AA:BB:CC:DD:EE:03", "Bookoo"));

  for (ScaleHistoryEntry &entry : entries) {
    clearScaleHistorySessionMarker(entry);
  }
  CHECK(consumeScaleHistorySessionConnection(
      entries, seq, "AA:BB:CC:DD:EE:03", "Bookoo"));
}

void p45_scale_mac_nvs_ignores_seq_and_defers_while_linked() {
  ScaleHistoryEntry left[SCALE_HISTORY_CAPACITY] = {};
  ScaleHistoryEntry right[SCALE_HISTORY_CAPACITY] = {};
  uint32_t seq = 0;
  CHECK(upsertScaleHistory(left, seq, "AA:BB:CC:DD:EE:01", "One"));
  memcpy(right, left, sizeof(left));
  CHECK(scaleHistoryIdentityEqual(left, right));
  CHECK(!upsertScaleHistory(left, seq, "AA:BB:CC:DD:EE:01", "One"));
  CHECK(left[0].lastSeenSeq != right[0].lastSeenSeq);
  CHECK(scaleHistoryIdentityEqual(left, right));
  CHECK(memcmp(left, right, sizeof(left)) != 0);

  CHECK(decideScaleMacNvsAction(true, false, false) ==
        ScaleMacNvsAction::CLEAR_DIRTY);
  CHECK(decideScaleMacNvsAction(true, true, true) ==
        ScaleMacNvsAction::CLEAR_DIRTY);
  CHECK(decideScaleMacNvsAction(false, true, false) == ScaleMacNvsAction::DEFER);
  CHECK(decideScaleMacNvsAction(false, false, true) == ScaleMacNvsAction::DEFER);
  CHECK(decideScaleMacNvsAction(false, false, false) == ScaleMacNvsAction::QUEUE);
  CHECK(!scaleMacNvsWriteAllowed(true, false));
  CHECK(!scaleMacNvsWriteAllowed(false, true));
  CHECK(scaleMacNvsWriteAllowed(false, false));

  CHECK(upsertScaleHistory(left, seq, "AA:BB:CC:DD:EE:02", "Two"));
  CHECK(!scaleHistoryIdentityEqual(left, right));
}

void p44_scale_history_canonicalizes_mac_case() {
  ScaleHistoryEntry entries[SCALE_HISTORY_CAPACITY] = {};
  uint32_t seq = 0;
  CHECK(upsertScaleHistory(entries, seq, "aa:bb:cc:dd:ee:10", "Pearl"));
  CHECK(strcmp(entries[0].mac, "AA:BB:CC:DD:EE:10") == 0);
  CHECK(preferredScaleMacEqual(entries[0].mac, "Aa:Bb:Cc:Dd:Ee:10"));
  char name[PREFERRED_SCALE_NAME_CAPACITY] = {};
  CHECK(findScaleHistoryName(entries, "AA:BB:CC:DD:EE:10", name, sizeof(name)));
  CHECK(strcmp(name, "Pearl") == 0);
  CHECK(findScaleHistoryName(entries, "aa:bb:cc:dd:ee:10", name, sizeof(name)));
  CHECK(strcmp(name, "Pearl") == 0);
}

void p24_preset_bank_size_and_crud_budgets() {
  CHECK(sizeof(ShotPreset) <= 136);
  CHECK(sizeof(ShotPresetBank) <= 1100);
  CHECK(sizeof(PersistedSettings) <= PERSISTED_SETTINGS_NVS_BUDGET);
  CHECK(sizeof(PersistedSettings) == 2960);
  CHECK(FLASH_IO_SCRATCH_BYTES == sizeof(PersistedSettings));
  CHECK(sizeof(RuntimeConfig) == 252);
  CHECK(sizeof(SettingsPersistRequest) <= PERSISTED_SETTINGS_NVS_BUDGET + 16);
  CHECK(sizeof(ControlStatusSnapshot) <= 4096);
  CHECK(sizeof(ControlGateSnapshot) <= 32);
  CHECK(sizeof(WebCommand) <= 328);
  WebCommand command;
  command.type = WebCommandType::PRESET_OP;
  command.config.goalWeightG = 42;
  command.presetId = 3;
  command.wipePassword();
  CHECK(command.config.goalWeightG == 42);
  WebCommand copied = command;
  CHECK(copied.config.goalWeightG == 42 && copied.presetId == 3);
  command.setNetworkType(WebCommandType::SAVE_NETWORK);
  copyCString(command.network.ssid, sizeof(command.network.ssid), "test-network");
  copyCString(command.network.password, sizeof(command.network.password), "test-password");
  memcpy(&copied, &command, sizeof(command));
  CHECK(strcmp(copied.network.ssid, "test-network") == 0);
  command.wipePassword();
  CHECK(command.network.password[0] == '\0');
  CHECK(strcmp(copied.network.password, "test-password") == 0);
  command = WebCommand{};
  CHECK(command.config.goalWeightG == RuntimeConfig{}.goalWeightG);
  CHECK(weightToCentigrams(INFINITY) == 0);
  CHECK(weightToCentigrams(NAN) == 0);
  CHECK(weightToCentigrams(1e30f) == INT32_MAX);
  CHECK(weightToCentigrams(-1e30f) == INT32_MIN);
  CHECK(weightToCentigrams(-1.25f) == -125);
  char formatted[32] = {};
  formatWeightCentigrams(-25, formatted, sizeof(formatted));
  CHECK(strcmp(formatted, "-0.25g") == 0);
  formatWeightCentigrams(INT32_MIN, formatted, sizeof(formatted));
  CHECK(strcmp(formatted, "-21474836.48g") == 0);
  formatWeightCentigrams(INT32_MAX, formatted, sizeof(formatted));
  CHECK(strcmp(formatted, "21474836.47g") == 0);
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  CHECK(bank.count == 2);
  CHECK(bank.activeId == FACTORY_PRESET_ID_DOUBLE);
  CHECK(bank.presets[0].id == FACTORY_PRESET_ID_DOUBLE);
  CHECK(bank.presets[1].id == FACTORY_PRESET_ID_SINGLE);
  CHECK(bank.presets[0].slowExtractionGuardEnabled);
  CHECK(std::fabs(bank.presets[0].minRecoveryWeightG -
                  DEFAULT_MIN_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(bank.presets[0].maxBbwBrewTimeMs == DEFAULT_MAX_BBW_BREW_TIME_MS);
  CHECK(bank.presets[0].cupProtectionEnabled);
  CHECK(bank.presets[0].stopIfCupRemoved);
  CHECK(!bank.presets[0].requireCupToStart);
  CHECK(bank.presets[0].avoidAccidentalTouchEnabled);
  CHECK(bank.presets[0].bbwEwmaAlpha == DEFAULT_BBW_EWMA_ALPHA);
  CHECK(bank.presets[0].bbwAlgorithm == 1);
  CHECK(bank.presets[1].fastExtractionGuardEnabled);
  CHECK(bank.presets[1].slowExtractionGuardEnabled);
  CHECK(bank.presets[1].minBbwBrewTimeMs == FACTORY_SINGLE_MIN_BBW_BREW_TIME_MS);
  CHECK(std::fabs(bank.presets[1].minRecoveryWeightG -
                  FACTORY_SINGLE_MIN_RECOVERY_WEIGHT_G) < 0.001f);
  CHECK(bank.presets[1].maxBbwBrewTimeMs == FACTORY_SINGLE_MAX_BBW_BREW_TIME_MS);

  {
    ShotPreset recipe{};
    RuntimeConfig cfg{};
    cfg.cupProtectionEnabled = false;
    cfg.stopIfCupRemoved = false;
    cfg.requireCupToStart = true;
    cfg.avoidAccidentalTouchEnabled = false;
    cfg.cupPresentWeightG = 5.0f;
    cfg.cupRemovedWeightG = -8.0f;
    copyUserRecipeFromConfig(cfg, recipe);
    CHECK(!recipe.cupProtectionEnabled);
    CHECK(!recipe.stopIfCupRemoved);
    CHECK(recipe.requireCupToStart);
    CHECK(!recipe.avoidAccidentalTouchEnabled);
    CHECK(recipe.bbwEwmaOffsetG == DEFAULT_WEIGHT_OFFSET_G);
    CHECK(recipe.bbwEwmaAlpha == DEFAULT_BBW_EWMA_ALPHA);
    applyShotPresetToConfig(recipe, cfg, false);
    CHECK(!cfg.cupProtectionEnabled);
    CHECK(!cfg.stopIfCupRemoved);
    CHECK(cfg.requireCupToStart);
    CHECK(!cfg.avoidAccidentalTouchEnabled);
    CHECK(std::fabs(cfg.cupPresentWeightG - 5.0f) < 0.001f);
    CHECK(std::fabs(cfg.cupRemovedWeightG - (-8.0f)) < 0.001f);
  }

  ShotPresetBank resetBank = bank;
  ShotPreset *single = mutableShotPreset(resetBank, FACTORY_PRESET_ID_SINGLE);
  CHECK(single != nullptr);
  single->fastExtractionGuardEnabled = false;
  single->slowExtractionGuardEnabled = false;
  CHECK(restoreFactoryShotPresetValues(resetBank, FACTORY_PRESET_ID_SINGLE));
  CHECK(findShotPreset(resetBank, FACTORY_PRESET_ID_SINGLE)->fastExtractionGuardEnabled);
  CHECK(findShotPreset(resetBank, FACTORY_PRESET_ID_SINGLE)->slowExtractionGuardEnabled);

  // Legacy Single-then-Double banks reorder on ensure.
  ShotPresetBank legacy{};
  legacy.count = 2;
  legacy.activeId = FACTORY_PRESET_ID_DOUBLE;
  legacy.nextId = 3;
  fillFactorySinglePreset(legacy.presets[0]);
  fillFactoryDoublePreset(legacy.presets[1]);
  ensureShotPresetBank(legacy, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(legacy.presets[0].id == FACTORY_PRESET_ID_DOUBLE);
  CHECK(legacy.presets[1].id == FACTORY_PRESET_ID_SINGLE);

  CHECK(!deleteShotPreset(bank, FACTORY_PRESET_ID_DOUBLE));
  CHECK(!deleteShotPreset(bank, FACTORY_PRESET_ID_SINGLE));
  CHECK(bank.count == 2);

  ShotPreset *dbl = mutableShotPreset(bank, FACTORY_PRESET_ID_DOUBLE);
  CHECK(dbl != nullptr);
  dbl->goalWeightG = 40;
  CHECK(restoreFactoryShotPresetValues(bank, FACTORY_PRESET_ID_DOUBLE));
  CHECK(findShotPreset(bank, FACTORY_PRESET_ID_DOUBLE)->goalWeightG ==
        DEFAULT_GOAL_WEIGHT_G);

  uint8_t newId = 0;
  CHECK(createUntitledShotPreset(bank, newId));
  CHECK(newId != 0);
  CHECK(bank.activeId == newId);
  const ShotPreset *created = findShotPreset(bank, newId);
  CHECK(created != nullptr);
  CHECK(created->goalWeightG == 36);
  CHECK(std::fabs(created->weightOffsetBaselineG - 1.5f) < 0.001f);
  CHECK(std::fabs(created->weightOffsetG - 1.5f) < 0.001f);

  uint8_t copyId = 0;
  CHECK(duplicateShotPreset(bank, FACTORY_PRESET_ID_DOUBLE, copyId));
  const ShotPreset *copy = findShotPreset(bank, copyId);
  CHECK(copy != nullptr);
  CHECK(strcmp(copy->name, "Double copy") == 0);
  CHECK(!copy->isFactory);

  uint8_t copy2 = 0;
  CHECK(duplicateShotPreset(bank, FACTORY_PRESET_ID_DOUBLE, copy2));
  const ShotPreset *copyB = findShotPreset(bank, copy2);
  CHECK(copyB != nullptr);
  CHECK(strcmp(copyB->name, "Double copy 2") == 0);

  CHECK(renameShotPreset(bank, copyId, "Double light"));
  CHECK(strcmp(findShotPreset(bank, copyId)->name, "Double light") == 0);
  CHECK(!renameShotPreset(bank, copy2, "Double light"));

  RuntimeConfig machine = {};
  machine.retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  machine.autoRetare = true;
  machine.timerOnly = true;  // session Manual
  RuntimeConfig composed = composeEffectiveConfig(machine, bank);
  CHECK(composed.timerOnly);  // Manual preserved
  CHECK(composed.goalWeightG == activeShotPreset(bank).goalWeightG);

  while (bank.count < MAX_SHOT_PRESETS) {
    uint8_t id = 0;
    CHECK(createUntitledShotPreset(bank, id));
  }
  uint8_t overflow = 0;
  CHECK(!createUntitledShotPreset(bank, overflow));
  CHECK(!duplicateShotPreset(bank, FACTORY_PRESET_ID_SINGLE, overflow));
}


void p25_invalid_active_id_keeps_customs() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == 3);
  bank.activeId = 99;  // missing
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == 3);
  CHECK(findShotPresetIndex(bank, customId) >= 0);
  CHECK(findShotPresetIndex(bank, bank.activeId) >= 0);

  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(createUntitledShotPreset(settings.presets, customId));
  settings.presets.activeId = 99;
  settings.runtime.timerOnly = true;
  ensurePersistedPresetBank(settings);
  CHECK(settings.presets.count >= 3);
  CHECK(findShotPresetIndex(settings.presets, customId) >= 0);
  const ShotPreset *dbl =
      findShotPreset(settings.presets, FACTORY_PRESET_ID_DOUBLE);
  CHECK(dbl != nullptr);
  CHECK(dbl->brewByWeight);  // must not inherit session Manual
}

void p26_save_candidate_validation_does_not_require_live_mutation() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  ShotPreset *preset = mutableShotPreset(bank, FACTORY_PRESET_ID_DOUBLE);
  CHECK(preset != nullptr);
  const uint8_t originalGoal = preset->goalWeightG;
  ShotPreset candidate = *preset;
  candidate.goalWeightG = 5;  // invalid
  CHECK(!validateShotPresetRecipe(candidate, DEFAULT_RETARE_WINDOW_MS, true));
  CHECK(preset->goalWeightG == originalGoal);
}

void p35_invalid_fast_extraction_recipe_keeps_custom() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == 3);
  ShotPreset *custom = mutableShotPreset(bank, customId);
  CHECK(custom != nullptr);
  custom->fastExtractionGuardEnabled = true;
  custom->goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  custom->maxRecoveryWeightG = 30.0f;
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == 3);
  const ShotPreset *kept = findShotPreset(bank, customId);
  CHECK(kept != nullptr);
  CHECK(!kept->fastExtractionGuardEnabled ||
        kept->maxRecoveryWeightG > static_cast<float>(kept->goalWeightG));
}

void p38_invalid_slow_extraction_recipe_keeps_custom() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == 3);
  ShotPreset *custom = mutableShotPreset(bank, customId);
  CHECK(custom != nullptr);
  custom->slowExtractionGuardEnabled = true;
  custom->goalWeightG = DEFAULT_GOAL_WEIGHT_G;
  custom->minRecoveryWeightG = 36.0f;
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == 3);
  const ShotPreset *kept = findShotPreset(bank, customId);
  CHECK(kept != nullptr);
  CHECK(!kept->slowExtractionGuardEnabled ||
        kept->minRecoveryWeightG < static_cast<float>(kept->goalWeightG));
}

void p16_static_ip_address_validation() {
  uint8_t ip[4] = {192, 168, 1, 50};
  uint8_t mask[4] = {255, 255, 255, 0};
  uint8_t gateway[4] = {192, 168, 1, 1};
  uint8_t dns1[4] = {1, 1, 1, 1};
  uint8_t dns2[4] = {0, 0, 0, 0};
  CHECK(validStaAddressConfig(static_cast<uint8_t>(StaIpMode::STATIC), ip, mask,
                              gateway, dns1, dns2));
  uint8_t softAp[4] = {192, 168, 4, 10};
  CHECK(!validStaAddressConfig(static_cast<uint8_t>(StaIpMode::STATIC), softAp,
                               mask, gateway, dns1, dns2));
  uint8_t zero[4] = {0, 0, 0, 0};
  CHECK(validStaAddressConfig(static_cast<uint8_t>(StaIpMode::DHCP), zero, zero,
                              zero, zero, zero));
  CHECK(!validStaAddressConfig(static_cast<uint8_t>(StaIpMode::DHCP), ip, mask,
                               gateway, dns1, dns2));

  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.staConfigured = true;
  strcpy(settings.staSsid, "CafeLAN");
  strcpy(settings.staPassword, "CafePass1");
  settings.staIpMode = static_cast<uint8_t>(StaIpMode::STATIC);
  memcpy(settings.staIp, ip, 4);
  memcpy(settings.staNetmask, mask, 4);
  memcpy(settings.staGateway, gateway, 4);
  memcpy(settings.staDns1, dns1, 4);
  settings.staConfigState = static_cast<uint8_t>(StaConfigState::PENDING);
  copyActiveStaToLkg(settings);
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  CHECK(restoreLkgToActive(settings));
  CHECK(settings.staConfigState ==
        static_cast<uint8_t>(StaConfigState::CONFIRMED));
}

void p16b_usb_set_wifi_commits_confirmed_lkg() {
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.staConfigured = true;
  strcpy(settings.staSsid, "CafeLAN");
  strcpy(settings.staPassword, "CafePass1");
  finalizeSavedStaCredentials(settings, true);
  CHECK(settings.staConfigState ==
        static_cast<uint8_t>(StaConfigState::CONFIRMED));
  CHECK(settings.lkgValid);
  CHECK(strcmp(settings.lkgSsid, "CafeLAN") == 0);
  CHECK(strcmp(settings.lkgPassword, "CafePass1") == 0);
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));

  PersistedSettings pending;
  CHECK(initializeDefaultSettings(pending));
  pending.staConfigured = true;
  strcpy(pending.staSsid, "CafeLAN");
  strcpy(pending.staPassword, "CafePass1");
  finalizeSavedStaCredentials(pending, false);
  CHECK(pending.staConfigState ==
        static_cast<uint8_t>(StaConfigState::PENDING));
  CHECK(!pending.lkgValid);
  finalizePersistedSettings(pending);
  CHECK(validPersistedSettings(pending));
}

void p48_ble_scan_defaults_and_dual_slot_round_trip() {
  resetHostPersistence();
  CHECK(sizeof(BleScanPersistedSettings) == 20);
  BleScanPersistedSettings settings;
  CHECK(settings.reservedEnabled == 0);
  CHECK(settings.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::BALANCED));
  settings.scanIntensity = 9;
  finalizeBleScanSettings(settings);
  CHECK(settings.scanIntensity == 0);
  CHECK(settings.reservedEnabled == 0);
  CHECK(settings.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(saveBleScanSettings(settings));
  CHECK(settings.revision == 1);
  CHECK(verifyFactoryBleScanSettings(settings));
  settings.scanIntensity =
      static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE);
  CHECK(saveBleScanSettings(settings));
  CHECK(settings.revision == 2);
  BleScanPersistedSettings loaded;
  CHECK(loadBleScanSettings(loaded));
  CHECK(loaded.reservedEnabled == 0);
  CHECK(loaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE));
  CHECK(loaded.revision == 2);
  CHECK(loaded.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(validBleScanSettingsBlob(loaded));
  BleScanPersistedSettings onDisk;
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(!verifyFactoryBleScanSettings(onDisk));

  BleScanPersistedSettings v1 = {};
  v1.magic = BLE_SCAN_SETTINGS_MAGIC;
  v1.version = BLE_SCAN_SETTINGS_V1_VERSION;
  v1.structureSize = sizeof(BleScanPersistedSettings);
  v1.revision = 3;
  v1.reservedEnabled = 1;
  v1.scanIntensity = static_cast<uint8_t>(BleScanIntensity::RELAXED);
  v1.checksum = bleScanSettingsChecksum(v1);
  CHECK(validBleScanSettingsBlob(v1));
  CHECK(!verifyFactoryBleScanSettings(v1));
  v1.reservedEnabled = 0;
  v1.scanIntensity = static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE);
  v1.checksum = bleScanSettingsChecksum(v1);
  CHECK(validBleScanSettingsBlob(v1));
  CHECK(!verifyFactoryBleScanSettings(v1));
  v1.reservedEnabled = 1;
  v1.scanIntensity = static_cast<uint8_t>(BleScanIntensity::RELAXED);
  v1.checksum = bleScanSettingsChecksum(v1);
  CHECK(lockSettingsNvs());
  ShotStopperPreferences preferences(NvsSubsystem::BLE_SCAN);
  CHECK(preferences.begin(SETTINGS_NAMESPACE, false));
  CHECK(preferences.putBytes(BLE_SCAN_SLOT_A, &v1, sizeof(v1)) == sizeof(v1));
  preferences.end();
  unlockSettingsNvs();
  BleScanPersistedSettings upgraded;
  CHECK(loadBleScanSettings(upgraded));
  CHECK(upgraded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::RELAXED));
  CHECK(upgraded.reservedEnabled == 0);
  CHECK(upgraded.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(upgraded.scanBoostMin == SCALE_SCAN_BOOST_DEFAULT_MIN);
  CHECK(validBleScanSettingsBlob(upgraded));
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(onDisk.version == BLE_SCAN_SETTINGS_V1_VERSION);
  CHECK(onDisk.reservedEnabled == 1);
  CHECK(!verifyFactoryBleScanSettings(onDisk));
  CHECK(persistBleScanSettings(
      upgraded, static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE),
      upgraded.scanBackoffMin, upgraded.scanBoostMin));
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(onDisk.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(onDisk.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE));
  CHECK(validBleScanSettingsBlob(onDisk));
  CHECK(!verifyFactoryBleScanSettings(onDisk));
}

void p49_ble_scan_corruption_falls_back_to_factory_default() {
  resetHostPersistence();
  BleScanPersistedSettings settings;
  CHECK(saveBleScanSettings(settings));
  settings.scanIntensity = static_cast<uint8_t>(BleScanIntensity::RELAXED);
  CHECK(saveBleScanSettings(settings));
  CHECK(persistence_host::corrupt(SETTINGS_NAMESPACE, BLE_SCAN_SLOT_B,
                                  offsetof(BleScanPersistedSettings,
                                           checksum)));
  BleScanPersistedSettings loaded;
  CHECK(loadBleScanSettings(loaded));
  CHECK(loaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::BALANCED));
  CHECK(loaded.revision == 1);
  CHECK(validBleScanSettingsBlob(loaded));
  CHECK(resetBleScanSettings(loaded));
  CHECK(loaded.reservedEnabled == 0);
  CHECK(loaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::BALANCED));
  BleScanPersistedSettings onDisk;
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(verifyFactoryBleScanSettings(onDisk));
}

RecoveryGestureResult recoveryEdge(RecoveryGestureRecognizer &recognizer,
                                   uint32_t atMs, bool activatorOn) {
  return recognizer.update(atMs, activatorOn, activatorOn, !activatorOn);
}

void p50_recovery_three_cycles_confirm_network_reset() {
  CHECK(recoveryGestureEntryAllowed(true, true));
  CHECK(!recoveryGestureEntryAllowed(false, true));
  CHECK(!recoveryGestureEntryAllowed(true, false));
  RecoveryGestureRecognizer recognizer;
  recognizer.begin(0);
  CHECK(recoveryEdge(recognizer, 100, false) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 200, true) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 300, false) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 400, true) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 500, false) == RecoveryGestureResult::NONE);
  CHECK(recoveryEdge(recognizer, 600, true) == RecoveryGestureResult::NONE);
  CHECK(recognizer.update(3599, true, false, false) ==
        RecoveryGestureResult::NONE);
  CHECK(recognizer.update(3600, true, false, false) ==
        RecoveryGestureResult::NETWORK_ACCESS_RESET);
}

void p51_recovery_five_cycles_upgrade_factory_candidate() {
  RecoveryGestureRecognizer recognizer;
  recognizer.begin(0);
  for (uint32_t cycle = 0; cycle < 5; ++cycle) {
    CHECK(recoveryEdge(recognizer, 100 + cycle * 200, false) ==
          RecoveryGestureResult::NONE);
    CHECK(recoveryEdge(recognizer, 200 + cycle * 200, true) ==
          RecoveryGestureResult::NONE);
  }
  CHECK(recognizer.completedCycles == 5);
  CHECK(recognizer.update(3999, true, false, false) ==
        RecoveryGestureResult::NONE);
  CHECK(recognizer.update(4000, true, false, false) ==
        RecoveryGestureResult::FACTORY_RESET);
}

void p52_recovery_rejects_four_slow_and_late_confirmation() {
  RecoveryGestureRecognizer recognizer;
  recognizer.begin(0);
  for (uint32_t cycle = 0; cycle < 4; ++cycle) {
    (void)recoveryEdge(recognizer, 100 + cycle * 200, false);
    (void)recoveryEdge(recognizer, 200 + cycle * 200, true);
  }
  CHECK(recognizer.update(5101, true, false, false) ==
        RecoveryGestureResult::NONE);
  CHECK(!recognizer.attemptActive);

  recognizer.begin(0);
  for (uint32_t cycle = 0; cycle < 6; ++cycle) {
    (void)recoveryEdge(recognizer, 100 + cycle * 200, false);
    (void)recoveryEdge(recognizer, 200 + cycle * 200, true);
  }
  CHECK(!recognizer.attemptActive);
  CHECK(recognizer.update(5000, true, false, false) ==
        RecoveryGestureResult::NONE);

  (void)recoveryEdge(recognizer, 10000, false);
  (void)recoveryEdge(recognizer, 16000, true);
  CHECK(recognizer.completedCycles == 0);
  CHECK(!recognizer.attemptActive);

  recognizer.begin(0);
  (void)recoveryEdge(recognizer, 58000, false);
  (void)recoveryEdge(recognizer, 58200, true);
  (void)recoveryEdge(recognizer, 58400, false);
  (void)recoveryEdge(recognizer, 58600, true);
  (void)recoveryEdge(recognizer, 58800, false);
  (void)recoveryEdge(recognizer, 59000, true);
  CHECK(recognizer.update(59999, true, false, false) ==
        RecoveryGestureResult::NONE);
  CHECK(recognizer.update(60000, true, false, false) ==
        RecoveryGestureResult::TIMED_OUT);
}

void p53_recovery_boundaries_and_millis_wraparound() {
  RecoveryGestureRecognizer recognizer;
  recognizer.begin(0);
  (void)recoveryEdge(recognizer, 0, false);
  (void)recoveryEdge(recognizer, 1000, true);
  (void)recoveryEdge(recognizer, 2000, false);
  (void)recoveryEdge(recognizer, 3000, true);
  (void)recoveryEdge(recognizer, 4000, false);
  (void)recoveryEdge(recognizer, 5000, true);
  CHECK(recognizer.update(8000, true, false, false) ==
        RecoveryGestureResult::NETWORK_ACCESS_RESET);

  constexpr uint32_t base = UINT32_MAX - 1000U;
  recognizer.begin(base);
  (void)recoveryEdge(recognizer, base + 100U, false);
  (void)recoveryEdge(recognizer, base + 200U, true);
  (void)recoveryEdge(recognizer, base + 300U, false);
  (void)recoveryEdge(recognizer, base + 400U, true);
  (void)recoveryEdge(recognizer, base + 500U, false);
  (void)recoveryEdge(recognizer, base + 600U, true);
  CHECK(recognizer.update(base + 3600U, true, false, false) ==
        RecoveryGestureResult::NETWORK_ACCESS_RESET);
}

void p54_recovery_intent_round_trip_corruption_and_clear() {
  resetHostPersistence();
  CHECK(saveRecoveryIntent(RecoveryOperation::FACTORY_RESET));
  CHECK(recoveryIntentRecordPresent());
  RecoveryIntent intent;
  CHECK(loadRecoveryIntent(intent));
  CHECK(intent.operation ==
        static_cast<uint8_t>(RecoveryOperation::FACTORY_RESET));
  CHECK(persistence_host::corrupt(RECOVERY_NAMESPACE, RECOVERY_INTENT_KEY,
                                  offsetof(RecoveryIntent, checksum)));
  CHECK(!loadRecoveryIntent(intent));
  CHECK(recoveryIntentRecordPresent());
  CHECK(clearRecoveryIntent());
  CHECK(!recoveryIntentRecordPresent());

  persistence_host::failNextWrite = true;
  CHECK(!saveRecoveryIntent(RecoveryOperation::NETWORK_ACCESS_RESET));
}

void p55_network_access_reset_preserves_non_network_settings() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 41;
  strcpy(settings.preferredScaleMac, "AA:BB:CC:DD:EE:FF");
  strcpy(settings.preferredScaleName, "Lunar");
  settings.staConfigured = true;
  settings.staOpen = false;
  settings.staWifiSleep = true;
  strcpy(settings.staSsid, "CafeLAN");
  strcpy(settings.staPassword, "CafePass1");
  settings.staIpMode = static_cast<uint8_t>(StaIpMode::STATIC);
  settings.staIp[0] = 192;
  settings.staIp[1] = 168;
  settings.staIp[2] = 10;
  settings.staIp[3] = 50;
  settings.staNetmask[0] = 255;
  settings.staNetmask[1] = 255;
  settings.staNetmask[2] = 255;
  settings.staGateway[0] = 192;
  settings.staGateway[1] = 168;
  settings.staGateway[2] = 10;
  settings.staGateway[3] = 1;
  settings.staDns1[0] = 1;
  settings.staDns1[1] = 1;
  settings.staDns1[2] = 1;
  settings.staDns1[3] = 1;
  settings.lkgValid = true;
  settings.lkgOpen = false;
  strcpy(settings.lkgSsid, "OldCafeLAN");
  strcpy(settings.lkgPassword, "OldCafe1");
  settings.lkgIpMode = static_cast<uint8_t>(StaIpMode::DHCP);
  CHECK(setDevicePassword(settings, "NewAccess1"));
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));

  PersistedSettings reset;
  CHECK(resetPersistedNetworkAccess(reset));
  CHECK(!reset.staConfigured);
  CHECK(!reset.lkgValid);
  CHECK(reset.staWifiSleep);
  CHECK(reset.staIpMode == static_cast<uint8_t>(StaIpMode::DHCP));
  CHECK(passwordIsFactoryDefault(reset));
  CHECK(reset.runtime.goalWeightG == 41);
  CHECK(strcmp(reset.preferredScaleMac, "AA:BB:CC:DD:EE:FF") == 0);
  CHECK(strcmp(reset.preferredScaleName, "Lunar") == 0);
}

void p56_shot_log_stale_slot_and_foreign_schema_rejected() {
  ShotLogStore current = {};
  resetShotLogStore(current, 4);
  current.header.count = 1;
  current.header.writeIndex = 1;
  current.header.nextRecordId = 2;
  current.records[0].id = 1;
  current.records[0].bootId = 4;
  current.records[0].goalWeightG = 36;
  current.records[0].actualWeightCg = 3600;
  current.records[0].actualWeightSource =
      static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  strcpy(current.records[0].presetName, "Double");
  finalizeShotLogStore(current);
  CHECK(current.header.schemaVersion == SHOT_LOG_SCHEMA_VERSION);
  CHECK(current.records[0].goalWeightG == 36);
  CHECK(strcmp(current.records[0].presetName, "Double") == 0);

  // A corrupted checksum makes the slot invalid even with intact records.
  ShotLogStore corrupt = current;
  corrupt.header.checksum ^= 1;
  CHECK(!validShotLogStore(corrupt));

  // Unknown schemas are rejected even with an intact record CRC.
  ShotLogStore foreign = current;
  foreign.header.schemaVersion = 31;
  foreign.header.checksum = 0;
  foreign.header.checksum = shotLogChecksum(foreign);
  CHECK(!validShotLogStore(foreign));

  // The stale generation loses to the newer slot.
  ShotLogStore stale = current;
  resetShotLogStore(stale, 4);
  stale.header.generation = current.header.generation - 1;
  CHECK(secondRevisionIsNewer(stale.header.generation,
                              current.header.generation));
}

void p58_reset_all_durable_stores_and_mid_fail_keeps_settings() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 41;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));

  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightG = 36;
  record.actualWeightCg = 3600;
  CHECK(log.append(record));

  HistoryLog history;
  CHECK(history.load());
  HistoryRecord activation = {};
  activation.durationDs = 250;
  activation.type = static_cast<uint8_t>(HistoryType::SHOT);
  activation.flags = HISTORY_FLAG_WALL_TIME;
  activation.endedAtUnixSec = 1700000000;
  activation.endedAtLocalSec = 1700000000;
  CHECK(history.append(activation));

  ShotCurveLog curves;
  CHECK(curves.load());
  ShotCurveRecord curve = {};
  curve.shotId = 1;
  curve.count = 2;
  curve.intervalDs = SHOT_CURVE_INTERVAL_DS;
  curve.weightCg[0] = 0;
  curve.weightCg[1] = 1800;
  CHECK(curves.append(curve));

  LastShotStore lastShot;
  CHECK(lastShot.load());
  PersistedLastShot shot = {};
  shot.valid = true;
  shot.cycleId = 3;
  CHECK(lastShot.persist(shot));

  BleScanPersistedSettings ble;
  ble.scanIntensity = static_cast<uint8_t>(BleScanIntensity::RELAXED);
  CHECK(saveBleScanSettings(ble));

  CHECK(resetAllDurableStores(settings, ble, log, history, lastShot,
                              curves));
  CHECK(verifyFactorySettings(settings));
  CHECK(log.count() == 0);
  CHECK(history.count() == 0);
  CHECK(curves.count() == 0);
  CHECK(!lastShot.get().valid);
  CHECK(ble.reservedEnabled == 0);
  CHECK(ble.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::BALANCED));
  BleScanPersistedSettings onDisk;
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(verifyFactoryBleScanSettings(onDisk));

  settings.runtime.goalWeightG = 40;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  CHECK(log.append(record));
  ShotLog::setHostSaveSucceeds(false);
  CHECK(!resetAllDurableStores(settings, ble, log, history, lastShot,
                               curves));
  ShotLog::setHostSaveSucceeds(true);
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 40);
  ShotLog reloaded;
  CHECK(reloaded.load());
  // A failed clear keeps the last-good slot: the dual-slot flip never erases
  // the only durable copy.
  CHECK(reloaded.count() == 1);
}

void p59_deferred_shot_log_append_writes_only_on_flush() {
  resetHostPersistence();
  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightG = 36;
  CHECK(log.append(record, false));
  CHECK(log.dirty());
  CHECK(log.count() == 1);
  // Nothing reached the durable slots yet: a fresh reader still sees empty.
  ShotLog reader;
  CHECK(reader.load());
  CHECK(reader.count() == 0);
  CHECK(log.flush());
  CHECK(!log.dirty());
  CHECK(reader.load());
  CHECK(reader.count() == 1);
}

void p60_factory_intent_survives_failed_store_reset() {
  resetHostPersistence();
  CHECK(saveRecoveryIntent(RecoveryOperation::FACTORY_RESET));
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 41;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  ShotLog log;
  CHECK(log.load());
  HistoryLog history;
  CHECK(history.load());
  LastShotStore lastShot;
  CHECK(lastShot.load());
  BleScanPersistedSettings ble;
  ShotCurveLog curves;
  CHECK(curves.load());
  ShotLog::setHostSaveSucceeds(false);
  CHECK(!resetAllDurableStores(settings, ble, log, history, lastShot,
                               curves));
  ShotLog::setHostSaveSucceeds(true);
  RecoveryIntent intent;
  CHECK(loadRecoveryIntent(intent));
  CHECK(intent.operation ==
        static_cast<uint8_t>(RecoveryOperation::FACTORY_RESET));
}

void p61_shot_curve_dual_slot_round_trip_and_delete() {
  resetHostPersistence();
  CHECK(SHOT_CURVE_INTERVAL_MS == 500);
  CHECK(SHOT_CURVE_MAX_POINTS == 121);
  CHECK(sizeof(ShotCurveRecord) == 268);
  CHECK(sizeof(ShotCurveStore) == 26820);
  CHECK(sizeof(ShotCurveStore) % 4 == 0);
  CHECK(sizeof(ShotCurveStore) <= SHOT_CURVE_FLASH_SLOT_BYTES);
  CHECK(SHOT_CURVE_FLASH_SLOT_BYTES == 28 * 1024);
  ShotCurveRecord full = emptyShotCurveRecord();
  full.count = SHOT_CURVE_MAX_POINTS;
  for (size_t i = 0; i < SHOT_CURVE_MAX_POINTS; ++i) {
    full.weightCg[i] = static_cast<int16_t>(i * 50);
  }
  char curveJson[SHOT_CURVE_JSON_CAPACITY] = {};
  CHECK(formatShotCurveJsonBody(curveJson, sizeof(curveJson), full));
  CHECK(strstr(curveJson, "\"wDtS\":0.5") != nullptr);
  CHECK(strstr(curveJson, "6000]") != nullptr);
  ShotCurveLog curves;
  CHECK(curves.load());
  CHECK(curves.count() == 0);
  ShotCurveRecord first = emptyShotCurveRecord();
  first.shotId = 7;
  first.count = 3;
  first.intervalDs = SHOT_CURVE_INTERVAL_DS;
  first.firstDrop.atDs = 45;
  first.firstDrop.weightCg = 50;
  first.extended.atDs = 180;
  first.extended.weightCg = 1800;
  first.atm.atDs = 200;
  first.atm.weightCg = 1900;
  first.ended.atDs = 220;
  first.ended.weightCg = 1900;
  first.weightCg[0] = 0;
  first.weightCg[1] = 900;
  first.weightCg[2] = 1800;
  CHECK(curves.append(first));
  ShotCurveRecord second = emptyShotCurveRecord();
  second.shotId = 8;
  second.count = 2;
  second.intervalDs = SHOT_CURVE_INTERVAL_DS;
  second.weightCg[0] = 10;
  second.weightCg[1] = 400;
  CHECK(curves.append(second));
  ShotCurveLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 2);
  ShotCurveRecord newest[2] = {};
  CHECK(reloaded.copyNewestFirst(newest, 2) == 2);
  CHECK(newest[0].shotId == 8);
  CHECK(newest[1].shotId == 7);
  CHECK(newest[1].firstDrop.atDs == 45);
  CHECK(newest[1].extended.atDs == 180);
  CHECK(newest[1].atm.atDs == 200);
  CHECK(newest[1].ended.atDs == 220);
  CHECK(newest[1].weightCg[2] == 1800);
  ShotCurveRecord exact = {};
  CHECK(reloaded.copyByShotId(7, exact));
  CHECK(exact.shotId == 7);
  CHECK(exact.weightCg[2] == 1800);
  CHECK(!reloaded.copyByShotId(99, exact));
  CHECK(reloaded.removeById(7));
  CHECK(!reloaded.copyByShotId(7, exact));
  CHECK(reloaded.count() == 1);
  CHECK(reloaded.copyNewestFirst(newest, 2) == 1);
  CHECK(newest[0].shotId == 8);
  CHECK(reloaded.clear());
  CHECK(reloaded.count() == 0);
  for (uint32_t id = 1; id <= SHOT_CURVE_CAPACITY + 1; ++id) {
    first.shotId = id;
    CHECK(reloaded.append(first, false));
  }
  CHECK(reloaded.dirty());
  CHECK(reloaded.count() == SHOT_CURVE_CAPACITY);
  CHECK(reloaded.flush());
  ShotCurveRecord ring[SHOT_CURVE_CAPACITY] = {};
  CHECK(reloaded.copyNewestFirst(ring, SHOT_CURVE_CAPACITY) ==
        SHOT_CURVE_CAPACITY);
  CHECK(ring[0].shotId == SHOT_CURVE_CAPACITY + 1);
  CHECK(ring[SHOT_CURVE_CAPACITY - 1].shotId == 2);
}

void p61b_shot_curve_failed_compaction_restores_durable_ring() {
  resetHostPersistence();
  ShotCurveLog curves;
  CHECK(curves.load());
  ShotCurveRecord record = emptyShotCurveRecord();
  record.count = 1;
  for (uint32_t id = 1; id <= SHOT_CURVE_CAPACITY; ++id) {
    record.shotId = id;
    record.weightCg[0] = static_cast<int16_t>(id);
    CHECK(curves.append(record, false));
  }
  CHECK(curves.flush());
  ShotCurveRecord durable[SHOT_CURVE_CAPACITY] = {};
  CHECK(curves.copyNewestFirst(durable, SHOT_CURVE_CAPACITY) ==
        SHOT_CURVE_CAPACITY);

  record.shotId = SHOT_CURVE_CAPACITY + 1;
  ShotCurveLog::setHostSaveSucceeds(false);
  CHECK(!curves.append(record, true));
  ShotCurveLog::setHostSaveSucceeds(true);
  ShotCurveRecord restored[SHOT_CURVE_CAPACITY] = {};
  CHECK(!curves.dirty());
  CHECK(curves.copyNewestFirst(restored, SHOT_CURVE_CAPACITY) ==
        SHOT_CURVE_CAPACITY);
  for (size_t i = 0; i < SHOT_CURVE_CAPACITY; ++i) {
    CHECK(restored[i].shotId == durable[i].shotId);
    CHECK(restored[i].weightCg[0] == durable[i].weightCg[0]);
  }
}

void p62_shot_curve_foreign_schema_is_rejected() {
  ShotCurveStore store;
  resetShotCurveStore(store);
  CHECK(validShotCurveStore(store));
  CHECK(store.header.schemaVersion == SHOT_CURVE_SCHEMA_VERSION);
  store.header.schemaVersion = SHOT_CURVE_SCHEMA_VERSION - 1;
  store.header.checksum = 0;
  store.header.checksum = shotCurveChecksum(store);
  CHECK(!validShotCurveStore(store));
}

void p67_ensure_recovery_intent_skips_rewrite_when_valid() {
  resetHostPersistence();
  CHECK(saveRecoveryIntent(RecoveryOperation::FACTORY_RESET));
  persistence_host::failNextWrite = true;
  CHECK(ensureRecoveryIntent(RecoveryOperation::FACTORY_RESET));
  CHECK(recoveryIntentMatches(RecoveryOperation::FACTORY_RESET));
  CHECK(!ensureRecoveryIntent(RecoveryOperation::NETWORK_ACCESS_RESET));
  RecoveryIntent intent;
  CHECK(inspectPendingRecovery(intent) == PendingRecoveryKind::VALID);
  CHECK(intent.operation ==
        static_cast<uint8_t>(RecoveryOperation::FACTORY_RESET));
}

void p68_malformed_recovery_intent_is_abandoned() {
  resetHostPersistence();
  CHECK(saveRecoveryIntent(RecoveryOperation::FACTORY_RESET));
  CHECK(persistence_host::corrupt(RECOVERY_NAMESPACE, RECOVERY_INTENT_KEY,
                                  offsetof(RecoveryIntent, checksum)));
  RecoveryIntent intent;
  CHECK(inspectPendingRecovery(intent) == PendingRecoveryKind::MALFORMED);
  CHECK(abandonRecoveryIntent());
  CHECK(inspectPendingRecovery(intent) == PendingRecoveryKind::NONE);
  CHECK(!recoveryIntentRecordPresent());
}

void p69_history_log_round_trip_eviction_and_paging() {
  resetHostPersistence();
  CHECK(sizeof(HistoryRecord) == 16);
  CHECK(sizeof(HistoryStore) == 16024);
  CHECK(sizeof(HistoryStore) % 4 == 0);
  CHECK(sizeof(HistoryStore) <= HISTORY_FLASH_SLOT_BYTES);
  CHECK(HISTORY_FLASH_SLOT_COUNT * HISTORY_FLASH_SLOT_BYTES == 0x8000);
  CHECK(historyClampPageLimit(0) == 1);
  CHECK(historyClampPageLimit(1000) == HISTORY_PAGE_MAX);
  CHECK(historyTypeFromCycle(true, 30000, DEFAULT_BBW_PROTECTION_MS) ==
        HistoryType::RINSE);
  CHECK(historyTypeFromCycle(false, DEFAULT_BBW_PROTECTION_MS,
                            DEFAULT_BBW_PROTECTION_MS) == HistoryType::OTHER);
  CHECK(historyTypeFromCycle(false, DEFAULT_BBW_PROTECTION_MS + 1,
                            DEFAULT_BBW_PROTECTION_MS) == HistoryType::SHOT);
  CHECK(strcmp(historyTypeName(HistoryType::SHOT), "shot") == 0);
  CHECK(strcmp(historyTypeName(HistoryType::RINSE), "rinse") == 0);
  CHECK(strcmp(historyTypeName(HistoryType::OTHER), "other") == 0);
  CHECK(strcmp(historyTypeName(HistoryType::POWER_ON), "power_on") == 0);

  HistoryLog log;
  CHECK(log.load());
  CHECK(log.count() == 0);
  HistoryRecord record = {};
  record.type = static_cast<uint8_t>(HistoryType::SHOT);
  record.flags = HISTORY_FLAG_WALL_TIME;
  record.endedAtUnixSec = 1700000000;
  record.endedAtLocalSec = 1700003600;
  record.durationDs = 281;
  CHECK(log.append(record, false));
  record.durationDs = 62;
  record.type = static_cast<uint8_t>(HistoryType::OTHER);
  CHECK(log.append(record, false));
  record.durationDs = 40;
  record.type = static_cast<uint8_t>(HistoryType::RINSE);
  CHECK(log.append(record, false));
  CHECK(log.dirty());
  CHECK(log.flush());
  CHECK(!log.dirty());

  HistoryLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 3);
  HistoryPage page;
  reloaded.copyPage(page, 0, HISTORY_PAGE_DEFAULT, ShotLogSortDir::Desc);
  CHECK(page.total == 3 && page.count == 3 && !page.hasMore);
  CHECK(page.records[0].type == static_cast<uint8_t>(HistoryType::RINSE));
  CHECK(page.records[2].durationDs == 281);
  CHECK(page.records[0].id == 3 && page.records[2].id == 1);
  reloaded.copyPage(page, 0, HISTORY_PAGE_DEFAULT, ShotLogSortDir::Asc);
  CHECK(page.records[0].id == 1 && page.records[2].id == 3);
  reloaded.copyPage(page, 1, 1, ShotLogSortDir::Desc);
  CHECK(page.total == 3 && page.count == 1 && page.hasMore);
  CHECK(page.records[0].id == 2);

  CHECK(reloaded.removeById(2));
  CHECK(!reloaded.containsId(2));
  CHECK(reloaded.count() == 2);
  HistoryLog afterDelete;
  CHECK(afterDelete.load());
  CHECK(afterDelete.count() == 2);
  CHECK(!afterDelete.removeById(2));

  CHECK(reloaded.clear());
  HistoryLog emptied;
  CHECK(emptied.load());
  CHECK(emptied.count() == 0);

  // Ring eviction: capacity keeps the newest HISTORY_CAPACITY activations.
  record.type = static_cast<uint8_t>(HistoryType::SHOT);
  for (uint32_t i = 0; i < HISTORY_CAPACITY + 3; ++i) {
    record.durationDs = static_cast<uint16_t>(100 + i);
    CHECK(emptied.append(record, false));
  }
  CHECK(emptied.count() == HISTORY_CAPACITY);
  CHECK(emptied.flush());
  HistoryLog evicted;
  CHECK(evicted.load());
  CHECK(evicted.count() == HISTORY_CAPACITY);
  CHECK(evicted.nextRecordId() == HISTORY_CAPACITY + 4);
  evicted.copyPage(page, 0, 1, ShotLogSortDir::Desc);
  CHECK(page.records[0].durationDs == 100 + HISTORY_CAPACITY + 2);
  evicted.copyPage(page, 0, 1, ShotLogSortDir::Asc);
  CHECK(page.records[0].durationDs == 103);
}

void p71_nvs_capacity_budget_keeps_compaction_margin() {
  constexpr size_t pageEntries = 126;
  constexpr size_t conservativeEntries =
      (EXPECTED_NVS_PARTITION_BYTES / 4096U - 2U) * pageEntries;
  constexpr size_t settingsEntries =
      2U * nvsBlobRequiredEntries(sizeof(PersistedSettings));
  // The shot log and activation history moved to dedicated flash partitions;
  // the last-shot record is the remaining history blob in NVS.
  constexpr size_t lastShotEntries = nvsBlobRequiredEntries(sizeof(LastShotBlob));
  constexpr size_t remainingRecords = lastShotEntries + 6U + 3U + 24U + 32U;
  constexpr size_t applicationEntries = settingsEntries + remainingRecords;
  CHECK(EXPECTED_NVS_PARTITION_BYTES == 0x15000U);
  CHECK(sizeof(PersistedSettings) == 2960U);
  CHECK(settingsEntries == 190U);
  CHECK(lastShotEntries == 11U);
  CHECK(applicationEntries == 266U);
  CHECK(conservativeEntries == 2394U);
  CHECK(conservativeEntries - applicationEntries == 2128U);
}

void p72_factory_intent_recovers_only_from_nvs_no_space() {
  resetHostPersistence();
  ShotLog log;
  LastShotStore lastShot;
  CHECK(log.load());
  CHECK(lastShot.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightG = 36;
  CHECK(log.append(record));
  PersistedLastShot shot = {};
  shot.valid = true;
  shot.cycleId = 9;
  CHECK(lastShot.persist(shot));

  persistence_host::failNextWrite = true;
  persistence_host::failNextWriteError = HOST_NVS_NOT_ENOUGH_SPACE;
  unsigned releases = 0;
  CHECK(ensureFactoryResetIntent([&]() {
    ++releases;
    return releaseNvsSpaceForFactoryReset(lastShot);
  }));
  CHECK(releases == 1U);
  CHECK(recoveryIntentMatches(RecoveryOperation::FACTORY_RESET));
  CHECK(persistence_host::records.count("lastshot/record") == 0);

  const NvsDiagnosticSnapshot nvs = captureNvsDiagnostics();
  CHECK(nvs.failureCount == 1U);
  CHECK(nvs.lastFailure.present);
  CHECK(nvs.lastFailure.subsystem == NvsSubsystem::RECOVERY_INTENT);
  CHECK(nvs.lastFailure.operation == NvsOperation::WRITE_BLOB);
  CHECK(nvs.lastFailure.errorCode == HOST_NVS_NOT_ENOUGH_SPACE);
  CHECK(nvs.lastFailure.requiredEntries ==
        nvsBlobRequiredEntries(sizeof(RecoveryIntent)));
}

void p73_factory_intent_does_not_free_data_for_other_failures() {
  resetHostPersistence();
  unsigned releases = 0;
  persistence_host::failNextWrite = true;
  CHECK(!ensureFactoryResetIntent([&]() {
    ++releases;
    return true;
  }));
  CHECK(releases == 0U);

  resetHostPersistence();
  releases = 0;
  persistence_host::corruptNextWrite = true;
  CHECK(!ensureFactoryResetIntent([&]() {
    ++releases;
    return true;
  }));
  CHECK(releases == 0U);

  resetHostPersistence();
  releases = 0;
  g_hostFlashIoMutexAvailable = false;
  CHECK(!ensureFactoryResetIntent([&]() {
    ++releases;
    return true;
  }));
  CHECK(releases == 0U);
  g_hostFlashIoMutexAvailable = true;
}

void p74_factory_intent_second_failure_keeps_settings() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightG = 40;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));

  persistence_host::failNextWrite = true;
  persistence_host::failNextWriteError = HOST_NVS_NOT_ENOUGH_SPACE;
  unsigned releases = 0;
  CHECK(!ensureFactoryResetIntent([&]() {
    ++releases;
    persistence_host::failNextWrite = true;
    persistence_host::failNextWriteError = HOST_NVS_ERROR;
    return true;
  }));
  CHECK(releases == 1U);
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightG == 40U);
  RecoveryIntent intent;
  CHECK(inspectPendingRecovery(intent) == PendingRecoveryKind::NONE);
}

void p63_flash_io_lock_fails_closed_without_mutex() {
  resetHostPersistence();
  const uint32_t before = flashIoLockTimeouts();
  g_hostFlashIoMutexAvailable = false;
  CHECK(!tryLockFlashIo());
  CHECK(!lockFlashIo());
  CHECK(flashIoLockTimeouts() == before + 2);
  g_hostFlashIoMutexAvailable = true;
  CHECK(tryLockFlashIo());
  unlockFlashIo();
  CHECK(FLASH_IO_LOCK_TIMEOUT_MS == 3000);
}

void p85_schema1_is_strict_and_micra_defaults_round_trip() {
  CHECK(validDeviceName(DEFAULT_DEVICE_NAME));
  CHECK(validDeviceName("Cafe Bar 2"));
  CHECK(!validDeviceName(""));
  CHECK(!validDeviceName(" leading"));
  CHECK(!validDeviceName("trailing-"));
  CHECK(!validDeviceName("bad_underscore"));
  char host[DEVICE_NAME_CAPACITY];
  CHECK(deviceNameToMdnsHost(host, sizeof(host), "Cafe Bar 2") == 10);
  CHECK(strcmp(host, "cafe-bar-2") == 0);
  CHECK(deviceNameToMdnsHost(host, sizeof(host), "A--  B") == 3);
  CHECK(strcmp(host, "a-b") == 0);
  CHECK(deviceNameToInstanceLabel(host, sizeof(host), "open brew by weight") == 19);
  CHECK(strcmp(host, "Open Brew By Weight") == 0);
  CHECK(deviceNameToInstanceLabel(host, sizeof(host), "cafe bar 2") == 10);
  CHECK(strcmp(host, "Cafe Bar 2") == 0);
  CHECK(deviceNameToInstanceLabel(host, sizeof(host), "down-stairs-espresso") == 20);
  CHECK(strcmp(host, "Down Stairs Espresso") == 0);
  CHECK(deviceNameToInstanceLabel(host, sizeof(host), "A--  B") == 3);
  CHECK(strcmp(host, "A B") == 0);

  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.schemaVersion == 1);
  CHECK(settings.lineaMicra.options == LINEA_MICRA_DEFAULT_OPTIONS);

  uint8_t oldBlob[sizeof(PersistedSettings) - 1] = {};
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, oldBlob,
                           sizeof(oldBlob));
  PersistedSettings rejected;
  CHECK(!loadPersistedSettings(rejected));

  strcpy(settings.lineaMicra.username, "barista@example.com");
  strcpy(settings.lineaMicra.password, "correct horse battery staple");
  memset(settings.lineaMicra.installationPrivateKey, 0x5a,
         sizeof(settings.lineaMicra.installationPrivateKey));
  strcpy(settings.lineaMicra.selectedSerial, "MR123456");
  strcpy(settings.lineaMicra.selectedName, "Kitchen Micra");
  settings.lineaMicra.accountConfigured = true;
  setLineaMicraOptions(settings.lineaMicra, true, true, true, false, 0);
  strcpy(settings.deviceName, "Cafe Bar 2");
  settings.presets.presets[0].lineaMicraBrewTargetDeciC = 935;
  CHECK(savePersistedSettings(settings));
  CHECK(loadPersistedSettings(settings));
  CHECK(settings.lineaMicra.options == LINEA_MICRA_DEFAULT_OPTIONS);
  CHECK(settings.lineaMicra.accountConfigured);
  CHECK(strcmp(settings.lineaMicra.username, "barista@example.com") == 0);
  CHECK(strcmp(settings.lineaMicra.selectedSerial, "MR123456") == 0);
  CHECK(settings.presets.presets[0].lineaMicraBrewTargetDeciC == 935);
  CHECK(strcmp(settings.deviceName, "Cafe Bar 2") == 0);

  setLineaMicraOptions(settings.lineaMicra, false, false, false, true, 3);
  CHECK(savePersistedSettings(settings));
  CHECK(loadPersistedSettings(settings));
  CHECK(settings.lineaMicra.options ==
        (LINEA_MICRA_SHUTDOWN_WITH_SCALE |
         (3U << LINEA_MICRA_SHUTDOWN_GRACE_SHIFT)));
  disconnectLineaMicra(settings.lineaMicra);
  CHECK(validLineaMicraSettings(settings.lineaMicra));
  CHECK(settings.lineaMicra.options ==
        (LINEA_MICRA_SHUTDOWN_WITH_SCALE |
         (3U << LINEA_MICRA_SHUTDOWN_GRACE_SHIFT)));
  CHECK(resetPersistedNetworkAccess(settings));
  CHECK(strcmp(settings.deviceName, "Cafe Bar 2") == 0);

  ShotPresetBank bank = settings.presets;
  CHECK(setShotPresetLineaMicraBrewTarget(bank, bank.activeId, 947));
  uint8_t duplicateId = 0;
  CHECK(duplicateShotPreset(bank, bank.activeId, duplicateId));
  CHECK(findShotPreset(bank, duplicateId)->lineaMicraBrewTargetDeciC == 947);
  uint8_t createdId = 0;
  CHECK(createUntitledShotPreset(bank, createdId));
  CHECK(findShotPreset(bank, createdId)->lineaMicraBrewTargetDeciC == 930);
  CHECK(!setShotPresetLineaMicraBrewTarget(bank, createdId, 799));
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(settings.lineaMicra.options == LINEA_MICRA_DEFAULT_OPTIONS);
  CHECK(strcmp(settings.deviceName, DEFAULT_DEVICE_NAME) == 0);
  CHECK(settings.presets.presets[0].lineaMicraBrewTargetDeciC == 930);
}

struct TestCase {
  const char *id;
  void (*function)();
};

const TestCase tests[] = {
    {"P85", p85_schema1_is_strict_and_micra_defaults_round_trip},
    {"P82", p82_ble_scan_backoff_boost_migration_and_roundtrip},
    {"P80", p80_boot_id_remains_dirty_until_durable},
    {"P01", p01_defaults_are_valid},
    {"P02", p02_newest_valid_slot_is_loaded},
    {"P02B", p02b_save_uses_ram_revision_when_slots_unreadable},
    {"P02C", p02c_overlay_live_runtime_is_saved_not_stale_blob},
    {"P02D", p02d_single_scratch_handles_wrap_and_external_destination},
    {"P03", p03_corrupt_newest_slot_falls_back},
    {"P04", p04_crc_and_semantic_validation_reject_corruption},
    {"P05", p05_password_change_updates_hash},
    {"P07", p07_invalid_schema_uses_factory_on_missing_slots},
    {"P08", p08_factory_reset_rebuilds_defaults},
    {"P08B", p08b_scale_preference_without_mac_round_trips},
    {"P64", p64_factory_settings_overwrite_does_not_clear_ble_namespace},
    {"P65", p65_factory_settings_survives_second_slot_write_fail},
    {"P66", p66_shot_log_dual_slot_generation_flip},
    {"P09", p09_fast_extraction_guard_validation},
    {"P10", p10_auto_to_manual_guard_trend_and_validation},
    {"P12", p12_shot_log_persists_compact_blob},
    {"P47", p47_rejects_non_current_schema_blob},
    {"P47F", p47f_bullseye_melody_persists_as_fixed_record},
    {"P47H", p47h_webhook_url_validation_is_http_only},
    {"P47C", p47c_desired_wifi_power_save_policy},
    {"P47D", p47d_durable_flash_write_gate},
    {"P46", p46_ring_retain_log_level_persists_round_trip},
    {"P43", p43_scale_history_upsert_and_lru},
    {"P70", p70_scale_history_session_consumption_is_bounded_and_reopenable},
    {"P45", p45_scale_mac_nvs_ignores_seq_and_defers_while_linked},
    {"P44", p44_scale_history_canonicalizes_mac_case},
    {"P24", p24_preset_bank_size_and_crud_budgets},
    {"P25", p25_invalid_active_id_keeps_customs},
    {"P26", p26_save_candidate_validation_does_not_require_live_mutation},
    {"P35", p35_invalid_fast_extraction_recipe_keeps_custom},
    {"P38", p38_invalid_slow_extraction_recipe_keeps_custom},
    {"P16", p16_static_ip_address_validation},
    {"P16B", p16b_usb_set_wifi_commits_confirmed_lkg},
    {"P18", p18_shot_log_keeps_history_when_inactive_slot_write_fails},
    {"P19", p19_shot_log_weight_sentinel_allows_int16_max},
    {"P29", p29_last_shot_persists_and_clears},
    {"P48", p48_ble_scan_defaults_and_dual_slot_round_trip},
    {"P49", p49_ble_scan_corruption_falls_back_to_factory_default},
    {"P50", p50_recovery_three_cycles_confirm_network_reset},
    {"P51", p51_recovery_five_cycles_upgrade_factory_candidate},
    {"P52", p52_recovery_rejects_four_slow_and_late_confirmation},
    {"P53", p53_recovery_boundaries_and_millis_wraparound},
    {"P54", p54_recovery_intent_round_trip_corruption_and_clear},
    {"P55", p55_network_access_reset_preserves_non_network_settings},
    {"P56", p56_shot_log_stale_slot_and_foreign_schema_rejected},
    {"P58", p58_reset_all_durable_stores_and_mid_fail_keeps_settings},
    {"P59", p59_deferred_shot_log_append_writes_only_on_flush},
    {"P60", p60_factory_intent_survives_failed_store_reset},
    {"P67", p67_ensure_recovery_intent_skips_rewrite_when_valid},
    {"P68", p68_malformed_recovery_intent_is_abandoned},
    {"P69", p69_history_log_round_trip_eviction_and_paging},
    {"P71", p71_nvs_capacity_budget_keeps_compaction_margin},
    {"P72", p72_factory_intent_recovers_only_from_nvs_no_space},
    {"P73", p73_factory_intent_does_not_free_data_for_other_failures},
    {"P74", p74_factory_intent_second_failure_keeps_settings},
    {"P61", p61_shot_curve_dual_slot_round_trip_and_delete},
    {"P61B", p61b_shot_curve_failed_compaction_restores_durable_ring},
    {"P62", p62_shot_curve_foreign_schema_is_rejected},
    {"P63", p63_flash_io_lock_fails_closed_without_mutex},
};

}  // namespace

int main() {
  for (const TestCase &test : tests) {
    const int before = failures;
    test.function();
    ++testsRun;
    std::cout << test.id << (failures == before ? " PASS" : " FAIL") << '\n';
  }
  std::cout << testsRun << " persistence tests, " << failures
            << " failures\n";
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
