#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#include "../ShotStopperPersistence.h"
#include "../ShotStopperBleScanPersistence.h"
#include "../ShotStopperDurableStores.h"
#include "../ShotStopperFirmwareMode.h"
#include "../ShotStopperRecovery.h"
#include "../ShotStopperRecoveryGesture.h"
#include "../ShotStopperShotLog.h"
#include "../ShotStopperShotCurve.h"
#include "../ShotStopperHistory.h"
#include "../ShotStopperActivationStores.h"
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
  CHECK(!settings.runtime.showDiagnosticPage);
  CHECK(settings.runtime.ntpSyncEnabled);
  CHECK(settings.runtime.timezoneAutomatic == 1);
  CHECK(settings.runtime.autoTareOutsideBrew);
  CHECK(idleAccessoryRetareEnabled(settings.runtime.noScaleBbwMode));
  CHECK(settings.runtime.powerManagementEnabled);
  CHECK(!settings.webhook.deferDuringShot);
  CHECK(settings.runtime.fastSideEnabled);
  CHECK(settings.runtime.fastMarginDg == DEFAULT_FAST_MARGIN_DG);
  CHECK(settings.runtime.fastBeforeMs == DEFAULT_FAST_BEFORE_MS);
  CHECK(settings.runtime.slowSideEnabled);
  CHECK(settings.runtime.slowMarginDg == DEFAULT_SLOW_MARGIN_DG);
  CHECK(settings.runtime.slowAfterMs == DEFAULT_SLOW_AFTER_MS);
  CHECK(settings.runtime.scaleLossEnabled);
  CHECK(settings.runtime.scaleLossFixedMs == 0);
  CHECK(std::fabs(settings.runtime.weightOffsetBaselineG -
                  DEFAULT_WEIGHT_OFFSET_G) < 0.001f);
  CHECK(validPersistedSettings(settings));
  CHECK(passwordIsFactoryDefault(settings));
  CHECK(passwordIsFactoryDefault(settings));
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.preferredScaleName[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::PREFER));
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
  CHECK(noScaleBbwModeValue(settings.runtime.noScaleBbwMode) ==
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
  settings.runtime.goalWeightDg = 470;
  settings.runtime.noScaleBbwMode &= ~IDLE_ACCESSORY_RETARE;
  settings.runtime.soundAlertsMuted = true;
  settings.runtime.cupProtectionEnabled = false;
  settings.runtime.stopIfCupRemoved = false;
  settings.runtime.requireCupToStart = true;
  settings.runtime.avoidAccidentalTouchEnabled = false;
  settings.runtime.cupPresentWeightG = 4.5f;
  settings.runtime.cupRemovedWeightG = -6.0f;
  settings.presets.presets[0].bbwEwmaOffsetG = 4.5f;
  settings.presets.presets[0].bbwEwmaAlpha = 50;
  settings.presets.presets[0].bbwAlphaLearned = 1;
  CHECK(savePersistedSettings(settings));
  CHECK(settings.storageRevision == firstRevision + 1);

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightDg == 470);
  CHECK(!idleAccessoryRetareEnabled(loaded.runtime.noScaleBbwMode));
  CHECK(loaded.runtime.soundAlertsMuted);
  CHECK(!loaded.runtime.cupProtectionEnabled);
  CHECK(!loaded.runtime.stopIfCupRemoved);
  CHECK(loaded.runtime.requireCupToStart);
  CHECK(!loaded.runtime.avoidAccidentalTouchEnabled);
  CHECK(std::fabs(loaded.runtime.cupPresentWeightG - 4.5f) < 0.001f);
  CHECK(std::fabs(loaded.runtime.cupRemovedWeightG - (-6.0f)) < 0.001f);
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
  settings.runtime.goalWeightDg = 410;
  CHECK(savePersistedSettings(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightDg == 410);
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
  stale.runtime.goalWeightDg = 360;
  CHECK(savePersistedSettings(stale));

  RuntimeConfig live = stale.runtime;
  live.goalWeightDg = 440;
  ShotPresetBank livePresets = stale.presets;
  overlayLivePersistedSettings(stale, live, livePresets);
  CHECK(stale.runtime.goalWeightDg == 440);
  CHECK(savePersistedSettings(stale));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightDg == 440);
  CHECK(loaded.storageRevision == stale.storageRevision);
}

void p02d_single_scratch_handles_wrap_and_external_destination() {
  resetHostPersistence();
  PersistedSettings beforeWrap;
  CHECK(initializeDefaultSettings(beforeWrap));
  beforeWrap.storageRevision = UINT32_MAX;
  beforeWrap.runtime.goalWeightDg = 400;
  finalizePersistedSettings(beforeWrap);
  PersistedSettings afterWrap = beforeWrap;
  afterWrap.storageRevision = 1;
  afterWrap.runtime.goalWeightDg = 480;
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
  CHECK(loaded->runtime.goalWeightDg == 480);
  heapCapsFree(loaded);
}

void p03_corrupt_newest_slot_falls_back() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightDg = 400;
  CHECK(savePersistedSettings(settings));
  settings.runtime.goalWeightDg = 480;
  CHECK(savePersistedSettings(settings));
  CHECK(persistence_host::corrupt(SETTINGS_NAMESPACE, SETTINGS_SLOT_B,
                                  offsetof(PersistedSettings, runtime)));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightDg == 400);
}

void p04_crc_and_semantic_validation_reject_corruption() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightDg = 500;
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
  record.goalWeightDg = 360;
  record.actualWeightCg = 3600;
  CHECK(log.append(record));
  CHECK(log.count() == 1);

  ShotLog::setHostSaveSucceeds(false);
  ShotLogRecord second = {};
  second.durationDs = 260;
  second.goalWeightDg = 360;
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
  CHECK(!shotLogWeightIsMissing(INT16_MAX));
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
  shot.goalWeightDg = 360;
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
  CHECK(reloaded.get().valid);
  CHECK(reloaded.get().cycleId == 42);
  CHECK(reloaded.get().goalWeightDg == 360);
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
  uint8_t oldBlob[sizeof(LastShotBlob) - 1] = {};
  persistence_host::putRaw("lastshot", "record", oldBlob, sizeof(oldBlob));
  LastShotStore rejected;
  CHECK(rejected.load());
  CHECK(!rejected.get().valid);
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
  settings.runtime.noScaleBbwMode &= ~IDLE_ACCESSORY_RETARE;
  settings.runtime.goalWeightDg = 630;
  strcpy(settings.preferredScaleMac, "AA:BB:CC:DD:EE:FF");
  strcpy(settings.preferredScaleName, "Lunar");
  settings.runtime.scaleMacCacheMode =
      static_cast<uint8_t>(ScaleMacCacheMode::ONLY);
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(idleAccessoryRetareEnabled(settings.runtime.noScaleBbwMode));
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(settings.staWifiSleep);
  CHECK(settings.runtime.goalWeightDg == DEFAULT_GOAL_WEIGHT_DG);
  CHECK(settings.runtime.fastSideEnabled);
  CHECK(settings.runtime.scaleLossEnabled);
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.preferredScaleName[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::PREFER));
  for (const ScaleHistoryEntry &entry : settings.scaleHistory) {
    CHECK(entry.mac[0] == '\0');
    CHECK(entry.name[0] == '\0');
    CHECK(entry.lastSeenSeq == 0);
  }
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(verifyFactorySettings(loaded));
  CHECK(idleAccessoryRetareEnabled(loaded.runtime.noScaleBbwMode));
  CHECK(loaded.runtime.goalWeightDg == DEFAULT_GOAL_WEIGHT_DG);
}

void p08b_scale_preference_without_mac_round_trips() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.preferredScaleMac[0] == '\0');
  CHECK(settings.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::PREFER));
  CHECK(savePersistedSettings(settings));

  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.preferredScaleMac[0] == '\0');
  CHECK(loaded.runtime.scaleMacCacheMode ==
        static_cast<uint8_t>(ScaleMacCacheMode::PREFER));

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
  settings.runtime.goalWeightDg = 630;
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

void p82_ble_scan_strict_versions_and_roundtrip() {
  resetHostPersistence();
  BleScanPersistedSettings old;
  old.version = BLE_SCAN_SETTINGS_VERSION + 1;
  old.revision = 7;
  old.scanIntensity = static_cast<uint8_t>(BleScanIntensity::RELAXED);
  old.checksum = bleScanSettingsChecksum(old);
  persistence_host::putRaw(SETTINGS_NAMESPACE, BLE_SCAN_SLOT_A, &old,
                           sizeof(old));
  BleScanPersistedSettings loaded;
  CHECK(!loadBleScanSettings(loaded));
  loaded = BleScanPersistedSettings{};

  CHECK(persistBleScanSettings(
      loaded, static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE),
      loaded.enabled));
  BleScanPersistedSettings reloaded;
  CHECK(loadBleScanSettings(reloaded));
  CHECK(reloaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE));

  // Both remaining fields in one call cost a single revision bump.
  const uint32_t revisionBefore = loaded.revision;
  CHECK(persistBleScanSettings(loaded,
                               static_cast<uint8_t>(BleScanIntensity::RELAXED),
                               0));
  CHECK(loadBleScanSettings(reloaded));
  CHECK(reloaded.revision == revisionBefore + 1);
  CHECK(reloaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::RELAXED));
  CHECK(reloaded.enabled == 0);

  BleScanPersistedSettings factory;
  finalizeBleScanSettings(factory);
  CHECK(verifyFactoryBleScanSettings(factory));
}

void p82b_ble_scan_legacy_minute_bytes_stay_readable() {
  // Firmware that stored the removed backoff/boost minutes wrote non-zero
  // bytes where reserved0/reserved1 now sit. Those blobs must stay loadable,
  // their reserved values must be ignored, and the next durable write must
  // clear them on disk.
  resetHostPersistence();
  BleScanPersistedSettings legacy;
  CHECK(saveBleScanSettings(legacy));
  legacy.reserved0 = 30;
  legacy.reserved1 = 15;
  legacy.checksum = bleScanSettingsChecksum(legacy);
  persistence_host::putRaw(SETTINGS_NAMESPACE, BLE_SCAN_SLOT_A,
                           &legacy, sizeof(legacy));
  BleScanPersistedSettings loaded;
  CHECK(loadBleScanSettings(loaded));
  CHECK(loaded.reserved0 == 0 && loaded.reserved1 == 0);
  CHECK(loaded.scanIntensity == legacy.scanIntensity);
  CHECK(loaded.enabled == 1);
  // The next durable write also clears the obsolete bytes on disk.
  CHECK(persistBleScanSettings(
      loaded, static_cast<uint8_t>(BleScanIntensity::RELAXED),
      loaded.enabled));
  BleScanPersistedSettings onDisk;
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(onDisk.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::RELAXED));
  CHECK(onDisk.reserved0 == 0 && onDisk.reserved1 == 0);
}

void p65_factory_settings_survives_second_slot_write_fail() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  settings.runtime.goalWeightDg = 630;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  persistence_host::failNextWriteForKey = SETTINGS_SLOT_B;
  CHECK(resetPersistedSettingsToFactory(settings));
  CHECK(verifyFactorySettings(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(verifyFactorySettings(loaded));
  CHECK(loaded.runtime.goalWeightDg == DEFAULT_GOAL_WEIGHT_DG);
}

void p66_shot_log_dual_slot_generation_flip() {
  resetHostPersistence();
  CHECK(sizeof(ShotLogStore) == 60028);
  CHECK(sizeof(ShotLogStore) % 4 == 0);
  CHECK(sizeof(ShotLogStore) <= SHOT_LOG_FLASH_SLOT_BYTES);
  CHECK(SHOT_LOG_FLASH_SLOT_COUNT * SHOT_LOG_FLASH_SLOT_BYTES <= 0x1E000);
  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightDg = 360;
  record.actualWeightCg = 3600;
  CHECK(log.append(record));
  CHECK(log.count() == 1);

  // A failed inactive-slot write leaves the last-good slot active and the
  // durable history intact.
  ShotLog::setHostSaveSucceeds(false);
  ShotLogRecord second = {};
  second.durationDs = 260;
  second.goalWeightDg = 360;
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

template <typename Log, typename Record>
void check_snapshot_persistence(Record record) {
  Log::resetHostStorage();
  Log live;
  CHECK(live.load());
  Log reloaded;
  for (size_t count = 1; count <= 4; ++count) {
    CHECK(live.append(record, false));
    Log image;
    live.capturePersistenceImage(image);
    CHECK(image.flushStep() == FlashStoreStepResult::COMPLETE);
    live.acknowledgePersisted(image, true);
    CHECK(!live.dirty());
    CHECK(reloaded.load());
    CHECK(reloaded.count() == count);
  }

  // A newer RAM edit must survive acknowledgement of an older snapshot.
  CHECK(live.append(record, false));
  Log image;
  live.capturePersistenceImage(image);
  CHECK(live.append(record, false));
  CHECK(image.flushStep() == FlashStoreStepResult::COMPLETE);
  live.acknowledgePersisted(image, false);
  CHECK(live.dirty());
  CHECK(live.count() == 6);
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 5);
  live.capturePersistenceImage(image);
  CHECK(image.flushStep() == FlashStoreStepResult::COMPLETE);
  live.acknowledgePersisted(image, true);
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 6);

  CHECK(live.append(record, false));
  live.capturePersistenceImage(image);
  Log::setHostSaveSucceeds(false);
  CHECK(image.flushStep() == FlashStoreStepResult::FAILED);
  live.acknowledgePersisted(image, false);
  CHECK(live.dirty());
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 6);
  Log::setHostSaveSucceeds(true);
  live.capturePersistenceImage(image);
  CHECK(image.flushStep() == FlashStoreStepResult::COMPLETE);
  live.acknowledgePersisted(image, true);
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 7);

  CHECK(live.clear(false));
  live.capturePersistenceImage(image);
  CHECK(image.flushStep() == FlashStoreStepResult::COMPLETE);
  live.acknowledgePersisted(image, true);
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 0);
  Log cleanImage = image;
  CHECK(live.append(record));
  live.capturePersistenceImage(cleanImage);
  CHECK(!cleanImage.dirty() && cleanImage.count() == 0);
  CHECK(live.clear());
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 0);
}

void p66b_snapshot_acknowledgement_preserves_newest_generation() {
  resetHostPersistence();
  ShotLogRecord shot = {};
  shot.durationDs = 280;
  shot.actualWeightCg = 3690;
  check_snapshot_persistence<ShotLog>(shot);
  check_snapshot_persistence<HistoryLog>(HistoryRecord{});
  ShotCurveRecord curve = emptyShotCurveRecord();
  curve.shotId = 1;
  curve.count = 1;
  curve.weightCg[0] = 3690;
  CHECK(validShotCurveRecord(curve));
}

void p09_fast_extraction_guard_validation() {
  // Schema 1 recipe: margins relative to the target, window in 0.5 s steps.
  RuntimeConfig config = {};
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.fastMarginDg = 0;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::FAST_MARGIN);
  config.fastSideEnabled = false;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config = {};
  config.slowMarginDg = 320;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::SLOW_MARGIN);
  config = {};
  config.slowAfterMs = config.fastBeforeMs;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::FAST_BEFORE);
  config = {};
  config.operationalWallMs = config.slowAfterMs;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::SLOW_AFTER);
}

void p10_auto_to_manual_guard_trend_and_validation() {
  RuntimeConfig config = {};
  config.scaleLossFixedMs = 5000;  // Before the earliest weight stop.
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::SCALE_LOSS_FIXED);
  config.scaleLossFixedMs = 30000;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
  config.weightOffsetBaselineG = -0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::WEIGHT_OFFSET_BASELINE);
  config.weightOffsetBaselineG = MAX_OFFSET_G + 0.1f;
  CHECK(validateRuntimeConfig(config) ==
        ConfigValidationError::WEIGHT_OFFSET_BASELINE);
  config.weightOffsetBaselineG = 2.25f;
  CHECK(validateRuntimeConfig(config) == ConfigValidationError::NONE);
}

void p12_shot_log_persists_compact_blob() {
  resetHostPersistence();
  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightDg = 360;
  record.actualWeightCg = 3600;
  record.actualWeightSource =
      static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  strcpy(record.presetName, "Double");
  record.tareAtDs = 34;
  strcpy(record.scaleName, "Lunar");
  CHECK(log.append(record));
  CHECK(log.count() == 1);

  ShotLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 1);
  ShotLogRecord out[1] = {};
  CHECK(reloaded.copyNewestFirst(out, 1) == 1);
  CHECK(out[0].goalWeightDg == 360);
  CHECK(out[0].actualWeightSource ==
        static_cast<uint8_t>(ActualWeightSource::POST_DRIP));
  CHECK(strcmp(out[0].presetName, "Double") == 0);
  CHECK(out[0].tareAtDs == 34);
  CHECK(strcmp(out[0].scaleName, "Lunar") == 0);
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

void p73_scale_friendly_name_validates_and_survives_upsert() {
  CHECK(validScaleFriendlyName(""));
  CHECK(validScaleFriendlyName("Bookoo Themis mini"));
  CHECK(validScaleFriendlyName("A9"));
  CHECK(!validScaleFriendlyName(nullptr));
  CHECK(!validScaleFriendlyName("bad name!"));
  CHECK(!validScaleFriendlyName(" leading"));
  CHECK(!validScaleFriendlyName("trailing "));
  CHECK(!validScaleFriendlyName("trailing-"));
  CHECK(validScaleFriendlyName("spaces ok"));
  char maxName[SCALE_FRIENDLY_NAME_MAX_LEN + 2] = {};
  memset(maxName, 'A', SCALE_FRIENDLY_NAME_MAX_LEN);
  CHECK(validScaleFriendlyName(maxName));
  maxName[SCALE_FRIENDLY_NAME_MAX_LEN] = 'A';
  maxName[SCALE_FRIENDLY_NAME_MAX_LEN + 1] = '\0';
  CHECK(!validScaleFriendlyName(maxName));

  ScaleHistoryEntry entries[SCALE_HISTORY_CAPACITY] = {};
  ScaleHistoryEntry reference[SCALE_HISTORY_CAPACITY] = {};
  uint32_t seq = 0;
  CHECK(upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:01", "One"));
  CHECK(upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:02", "Two"));
  copyCString(entries[0].friendlyName,
              sizeof(entries[0].friendlyName), "Bookoo mini");
  memcpy(reference, entries, sizeof(entries));
  // A re-advertised name keeps the stored friendly override.
  CHECK(upsertScaleHistory(entries, seq, "AA:BB:CC:DD:EE:01", "One-Renamed"));
  CHECK(strcmp(entries[0].name, "One-Renamed") == 0);
  CHECK(strcmp(entries[0].friendlyName, "Bookoo mini") == 0);
  // Friendly-name changes are identity changes (they must persist).
  CHECK(!scaleHistoryIdentityEqual(entries, reference));
  reference[0] = entries[0];
  CHECK(scaleHistoryIdentityEqual(entries, reference));
  // Refreshing :01 made it newest; :02 is the LRU victim when the table fills,
  // and a new occupant never inherits the previous friendly name.
  for (uint8_t i = 3; i <= 9; ++i) {
    char mac[PREFERRED_SCALE_MAC_CAPACITY];
    snprintf(mac, sizeof(mac), "AA:BB:CC:DD:EE:%02X", i);
    CHECK(upsertScaleHistory(entries, seq, mac, "X"));
  }
  CHECK(scaleHistoryOccupiedCount(entries) == SCALE_HISTORY_CAPACITY);
  for (size_t i = 0; i < SCALE_HISTORY_CAPACITY; ++i) {
    if (strcmp(entries[i].mac, "AA:BB:CC:DD:EE:01") == 0) {
      CHECK(strcmp(entries[i].friendlyName, "Bookoo mini") == 0);
      CHECK(strcmp(entries[i].name, "One-Renamed") == 0);
    } else {
      CHECK(entries[i].friendlyName[0] == '\0');
    }
  }
  char friendly[PREFERRED_SCALE_NAME_CAPACITY] = {};
  CHECK(findScaleHistoryFriendlyName(entries, "aa:bb:cc:dd:ee:01", friendly,
                                     sizeof(friendly)));
  CHECK(strcmp(friendly, "Bookoo mini") == 0);

  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  memcpy(settings.scaleHistory, entries, sizeof(settings.scaleHistory));
  finalizePersistedSettings(settings);
  CHECK(validPersistedSettings(settings));
  settings.scaleHistory[0].friendlyName[0] = '"';
  CHECK(!validPersistedSettings(settings));
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
  CHECK(sizeof(PersistedSettings) == 3056);
  CHECK(FLASH_IO_SCRATCH_BYTES >= sizeof(PersistedSettings));
  CHECK(sizeof(RuntimeConfig) == 312);
  CHECK(sizeof(SettingsPersistRequest) <= PERSISTED_SETTINGS_NVS_BUDGET + 16);
  // 6920: +8 bytes for the shot predictor's live hero tint (external
  // work-area growth; see ShotStopperControlStatusTypes.inc).
  CHECK(sizeof(ControlStatusSnapshot) <= 6920);
  CHECK(sizeof(ControlGateSnapshot) <= 32);
  CHECK(sizeof(WebCommand) <= 440);
  WebCommand command;
  command.type = WebCommandType::PRESET_OP;
  command.config.goalWeightDg = 420;
  command.presetId = 3;
  command.wipePassword();
  CHECK(command.config.goalWeightDg == 420);
  WebCommand copied = command;
  CHECK(copied.config.goalWeightDg == 420 && copied.presetId == 3);
  command.setNetworkType(WebCommandType::SAVE_NETWORK);
  copyCString(command.network.ssid, sizeof(command.network.ssid), "test-network");
  copyCString(command.network.password, sizeof(command.network.password), "test-password");
  memcpy(&copied, &command, sizeof(command));
  CHECK(strcmp(copied.network.ssid, "test-network") == 0);
  command.wipePassword();
  CHECK(command.network.password[0] == '\0');
  CHECK(strcmp(copied.network.password, "test-password") == 0);
  command = WebCommand{};
  CHECK(command.config.goalWeightDg == RuntimeConfig{}.goalWeightDg);
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
  // Fresh bank: five permanent factory presets, Manual active.
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  CHECK(bank.count == FACTORY_PRESET_COUNT + 1 && bank.nextId == 5);
  CHECK(bank.activeId == FACTORY_PRESET_ID_MANUAL);
  const struct {
    const char *name;
    uint16_t dose, goal, fast, slow;
    uint32_t t1, t2;
    bool sides;
    float offset;
  } factory[] = {
      {"Double \xC2\xB7 Adaptive", 180, 360, 60, 30, 27000, 36000, true, 1.5f},
      {"Single \xC2\xB7 Adaptive", 90, 180, 40, 15, 23000, 32000, true, 0.5f},
      {"Double \xC2\xB7 Fixed", 180, 360, 60, 30, 27000, 36000, false, 1.5f},
      {"Single \xC2\xB7 Fixed", 90, 180, 40, 15, 23000, 32000, false, 0.5f},
  };
  for (uint8_t i = 0; i < FACTORY_PRESET_COUNT; ++i) {
    const ShotPreset &preset = bank.presets[i];
    CHECK(preset.id == i + 1 && preset.isFactory && preset.brewByWeight);
    CHECK(strcmp(preset.name, factory[i].name) == 0);
    CHECK(preset.doseDg == factory[i].dose && preset.goalWeightDg == factory[i].goal);
    CHECK(preset.fastMarginDg == factory[i].fast && preset.slowMarginDg == factory[i].slow);
    CHECK(preset.fastBeforeMs == factory[i].t1 && preset.slowAfterMs == factory[i].t2);
    CHECK(preset.fastSideEnabled == factory[i].sides &&
          preset.slowSideEnabled == factory[i].sides);
    CHECK(preset.bbwProtectionMs == DEFAULT_BBW_PROTECTION_MS &&
          preset.operationalWallMs == DEFAULT_OPERATIONAL_WALL_MS);
    CHECK(preset.scaleLossFixedMs == 0 && preset.grindHintsEnabled);
    CHECK(preset.bbwEwmaOffsetG == factory[i].offset);
    CHECK(preset.bbwEwmaAlpha == DEFAULT_BBW_EWMA_ALPHA);
    CHECK(validateShotPresetRecipe(preset, DEFAULT_RETARE_WINDOW_MS, true));
    CHECK(!deleteShotPreset(bank, preset.id));
  }
  {
    // Manual: Double · Adaptive values that never stop by weight.
    const ShotPreset &manual = bank.presets[FACTORY_PRESET_COUNT];
    CHECK(manual.id == FACTORY_PRESET_ID_MANUAL && manual.isFactory);
    CHECK(strcmp(manual.name, "Manual") == 0 && !manual.brewByWeight);
    CHECK(!manual.fastSideEnabled && !manual.slowSideEnabled);
    CHECK(manual.goalWeightDg == factory[0].goal && manual.fastBeforeMs == factory[0].t1);
    CHECK(validateShotPresetRecipe(manual, DEFAULT_RETARE_WINDOW_MS, true));
    CHECK(!deleteShotPreset(bank, manual.id));
    ShotPresetBank restored = bank;
    mutableShotPreset(restored, FACTORY_PRESET_ID_MANUAL)->brewByWeight = true;
    CHECK(restoreFactoryShotPresetValues(restored, FACTORY_PRESET_ID_MANUAL));
    CHECK(!findShotPreset(restored, FACTORY_PRESET_ID_MANUAL)->brewByWeight);
    restored.activeId = FACTORY_PRESET_ID_MANUAL;
    CHECK(composeEffectiveConfig(RuntimeConfig{}, restored).timerOnly);
    // Wrapped allocation never hands a user preset a factory id.
    restored.nextId = FACTORY_PRESET_ID_MANUAL;
    uint8_t wrappedId = 0;
    CHECK(createUntitledShotPreset(restored, wrappedId));
    CHECK(wrappedId == FACTORY_PRESET_COUNT + 1);
  }

  {
    // Device-level protections never travel with a preset.
    ShotPreset recipe{};
    RuntimeConfig cfg{};
    cfg.cupProtectionEnabled = false;
    cfg.requireCupToStart = true;
    cfg.avoidAccidentalTouchEnabled = false;
    cfg.scaleLossEnabled = false;
    cfg.scaleLossFixedMs = 30000;
    copyUserRecipeFromConfig(cfg, recipe);
    CHECK(recipe.scaleLossFixedMs == 30000);
    CHECK(recipe.bbwEwmaOffsetG == DEFAULT_WEIGHT_OFFSET_G);
    RuntimeConfig other{};
    applyShotPresetToConfig(recipe, other);
    CHECK(other.cupProtectionEnabled && !other.requireCupToStart);
    CHECK(other.avoidAccidentalTouchEnabled && other.scaleLossEnabled);
    CHECK(other.scaleLossFixedMs == 30000);
  }

  ShotPresetBank resetBank = bank;
  ShotPreset *single = mutableShotPreset(resetBank, FACTORY_PRESET_ID_SINGLE_FIXED);
  CHECK(single != nullptr);
  single->fastSideEnabled = true;
  single->goalWeightDg = 200;
  copyCString(single->name, sizeof(single->name), "My single");
  CHECK(restoreFactoryShotPresetValues(resetBank, FACTORY_PRESET_ID_SINGLE_FIXED));
  single = mutableShotPreset(resetBank, FACTORY_PRESET_ID_SINGLE_FIXED);
  CHECK(!single->fastSideEnabled && single->goalWeightDg == 180);
  CHECK(strcmp(single->name, "My single") == 0);

  ShotPreset *dbl = mutableShotPreset(bank, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE);
  CHECK(dbl != nullptr);
  dbl->goalWeightDg = 40;
  CHECK(restoreFactoryShotPresetValues(bank, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE));
  CHECK(findShotPreset(bank, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE)->goalWeightDg ==
        DEFAULT_GOAL_WEIGHT_DG);

  uint8_t newId = 0;
  CHECK(createUntitledShotPreset(bank, newId));
  CHECK(newId != 0);
  CHECK(bank.activeId == newId);
  const ShotPreset *created = findShotPreset(bank, newId);
  CHECK(created != nullptr);
  CHECK(created->goalWeightDg == 360);
  CHECK(std::fabs(created->weightOffsetBaselineG - 1.5f) < 0.001f);
  CHECK(std::fabs(created->bbwEwmaOffsetG - 1.5f) < 0.001f);
  CHECK(!created->isFactory && deleteShotPreset(bank, newId));
  CHECK(createUntitledShotPreset(bank, newId));

  uint8_t copyId = 0;
  CHECK(duplicateShotPreset(bank, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE, copyId));
  const ShotPreset *copy = findShotPreset(bank, copyId);
  CHECK(copy != nullptr);
  // Long names keep a 15-byte base so " copy 99" always fits.
  CHECK(strcmp(copy->name, "Double \xC2\xB7 Adapt copy") == 0);
  CHECK(!copy->isFactory);

  uint8_t copy2 = 0;
  CHECK(duplicateShotPreset(bank, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE, copy2));
  const ShotPreset *copyB = findShotPreset(bank, copy2);
  CHECK(copyB != nullptr);
  CHECK(strcmp(copyB->name, "Double \xC2\xB7 Adapt copy 2") == 0);

  CHECK(renameShotPreset(bank, copyId, "Double light"));
  CHECK(strcmp(findShotPreset(bank, copyId)->name, "Double light") == 0);
  CHECK(!renameShotPreset(bank, copy2, "Double light"));

  RuntimeConfig machine = {};
  machine.retareWindowMs = DEFAULT_RETARE_WINDOW_MS;
  machine.autoRetare = true;
  machine.timerOnly = true;  // stale controller-wide Manual from earlier firmware
  RuntimeConfig composed = composeEffectiveConfig(machine, bank);
  CHECK(!composed.timerOnly);  // stop mode follows the active preset only
  CHECK(composed.goalWeightDg == activeShotPreset(bank).goalWeightDg);

  while (bank.count < MAX_SHOT_PRESETS) {
    uint8_t id = 0;
    CHECK(createUntitledShotPreset(bank, id));
  }
  uint8_t overflow = 0;
  CHECK(!createUntitledShotPreset(bank, overflow));
  CHECK(!duplicateShotPreset(bank, FACTORY_PRESET_ID_SINGLE_ADAPTIVE, overflow));
}


void p25_invalid_active_id_keeps_customs() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == FACTORY_PRESET_COUNT + 2);
  bank.activeId = 99;  // missing
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == FACTORY_PRESET_COUNT + 2);
  CHECK(findShotPresetIndex(bank, customId) >= 0);
  CHECK(findShotPresetIndex(bank, bank.activeId) >= 0);

  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(createUntitledShotPreset(settings.presets, customId));
  settings.presets.activeId = 99;
  settings.runtime.timerOnly = true;
  ensurePersistedPresetBank(settings);
  CHECK(settings.presets.count == FACTORY_PRESET_COUNT + 2);
  CHECK(findShotPresetIndex(settings.presets, customId) >= 0);
  const ShotPreset *dbl =
      findShotPreset(settings.presets, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE);
  CHECK(dbl != nullptr);
  CHECK(dbl->brewByWeight);  // a stale controller-wide Manual never leaks in
}

void p26_save_candidate_validation_does_not_require_live_mutation() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  ShotPreset *preset = mutableShotPreset(bank, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE);
  CHECK(preset != nullptr);
  const uint16_t originalGoal = preset->goalWeightDg;
  ShotPreset candidate = *preset;
  candidate.goalWeightDg = 50;  // invalid
  CHECK(!validateShotPresetRecipe(candidate, DEFAULT_RETARE_WINDOW_MS, true));
  CHECK(preset->goalWeightDg == originalGoal);
}

void p35_invalid_fast_extraction_recipe_keeps_custom() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == FACTORY_PRESET_COUNT + 2);
  ShotPreset *custom = mutableShotPreset(bank, customId);
  CHECK(custom != nullptr);
  custom->fastSideEnabled = true;
  custom->fastMarginDg = 0;  // Unusable margin: the recipe resets, the slot stays.
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == FACTORY_PRESET_COUNT + 2);
  const ShotPreset *kept = findShotPreset(bank, customId);
  CHECK(kept != nullptr);
  CHECK(validateShotPresetRecipe(*kept, DEFAULT_RETARE_WINDOW_MS, true));
  CHECK(kept->fastMarginDg == DEFAULT_FAST_MARGIN_DG);
}

void p38_invalid_slow_extraction_recipe_keeps_custom() {
  ShotPresetBank bank;
  seedDefaultShotPresetBank(bank);
  uint8_t customId = 0;
  CHECK(createUntitledShotPreset(bank, customId));
  CHECK(bank.count == FACTORY_PRESET_COUNT + 2);
  ShotPreset *custom = mutableShotPreset(bank, customId);
  CHECK(custom != nullptr);
  custom->slowAfterMs = custom->fastBeforeMs;  // Repaired to a 1 s window.
  ensureShotPresetBank(bank, DEFAULT_RETARE_WINDOW_MS, true);
  CHECK(bank.count == FACTORY_PRESET_COUNT + 2);
  const ShotPreset *kept = findShotPreset(bank, customId);
  CHECK(kept != nullptr);
  CHECK(kept->slowAfterMs == kept->fastBeforeMs + MIN_WINDOW_SPAN_MS);
  CHECK(validateShotPresetRecipe(*kept, DEFAULT_RETARE_WINDOW_MS, true));
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
  CHECK(settings.enabled == 1);
  CHECK(settings.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::BALANCED));
  settings.scanIntensity = 9;
  finalizeBleScanSettings(settings);
  CHECK(settings.scanIntensity == 0);
  CHECK(settings.enabled == 1);
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
  CHECK(loaded.enabled == 1);
  CHECK(loaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE));
  CHECK(loaded.revision == 2);
  CHECK(loaded.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(validBleScanSettingsBlob(loaded));
  BleScanPersistedSettings onDisk;
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(!verifyFactoryBleScanSettings(onDisk));

  // Foreign schema versions are rejected outright: the store never upgrades
  // an older blob, in memory or from NVS.
  BleScanPersistedSettings stale = {};
  stale.magic = BLE_SCAN_SETTINGS_MAGIC;
  stale.version = BLE_SCAN_SETTINGS_VERSION - 1;
  stale.structureSize = sizeof(BleScanPersistedSettings);
  stale.revision = 3;
  stale.enabled = 0;
  stale.scanIntensity = static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE);
  stale.checksum = bleScanSettingsChecksum(stale);
  CHECK(!validBleScanSettingsBlob(stale));
  CHECK(!verifyFactoryBleScanSettings(stale));
  BleScanPersistedSettings newer = stale;
  newer.version = BLE_SCAN_SETTINGS_VERSION + 1;
  newer.checksum = bleScanSettingsChecksum(newer);
  CHECK(!validBleScanSettingsBlob(newer));
  // Current-version blobs carry the switch: only 0/1 is a legal value.
  BleScanPersistedSettings current = stale;
  current.version = BLE_SCAN_SETTINGS_VERSION;
  current.checksum = bleScanSettingsChecksum(current);
  CHECK(validBleScanSettingsBlob(current));
  CHECK(!verifyFactoryBleScanSettings(current));
  current.enabled = 2;
  current.checksum = bleScanSettingsChecksum(current);
  CHECK(!validBleScanSettingsBlob(current));
  current.enabled = 0;
  current.checksum = bleScanSettingsChecksum(current);
  CHECK(validBleScanSettingsBlob(current));

  resetHostPersistence();
  CHECK(lockSettingsNvs());
  ShotStopperPreferences preferences(NvsSubsystem::BLE_SCAN);
  CHECK(preferences.begin(SETTINGS_NAMESPACE, false));
  current.enabled = 1;
  current.checksum = bleScanSettingsChecksum(current);
  CHECK(preferences.putBytes(BLE_SCAN_SLOT_A, &current, sizeof(current)) ==
        sizeof(current));
  preferences.end();
  unlockSettingsNvs();
  CHECK(loadBleScanSettings(loaded));
  CHECK(loaded.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(loaded.enabled == 1);
  CHECK(loaded.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE));
  CHECK(validBleScanSettingsBlob(loaded));
  // The master switch shares the combined save; disabled is durable.
  CHECK(persistBleScanSettings(loaded,
                               static_cast<uint8_t>(BleScanIntensity::AGGRESSIVE),
                               0));
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(onDisk.version == BLE_SCAN_SETTINGS_VERSION);
  CHECK(onDisk.enabled == 0);
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
  CHECK(loaded.enabled == 1);
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

void p87_recovery_grace_beeps_six_times_then_elapses() {
  RecoveryGraceWindow grace;
  grace.begin(0);
  CHECK(grace.update(0, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(1, true) == RecoveryGraceResult::HOLDING);
  CHECK(grace.update(4999, true) == RecoveryGraceResult::HOLDING);
  uint8_t beeps = 1;
  for (uint32_t atMs = RECOVERY_GRACE_BEEP_INTERVAL_MS;
       atMs < RECOVERY_ENTRY_GRACE_MS; atMs += RECOVERY_GRACE_BEEP_INTERVAL_MS) {
    CHECK(grace.update(atMs - 1, true) == RecoveryGraceResult::HOLDING);
    CHECK(grace.update(atMs, true) == RecoveryGraceResult::BEEP_DUE);
    ++beeps;
  }
  CHECK(beeps == 6);
  CHECK(grace.update(RECOVERY_ENTRY_GRACE_MS - 1, true) ==
        RecoveryGraceResult::HOLDING);
  CHECK(grace.update(RECOVERY_ENTRY_GRACE_MS, true) ==
        RecoveryGraceResult::ELAPSED);
  CHECK(grace.update(RECOVERY_ENTRY_GRACE_MS + 1, true) ==
        RecoveryGraceResult::HOLDING);
}

void p88_recovery_grace_release_aborts_and_wins_over_expiry() {
  RecoveryGraceWindow grace;
  grace.begin(0);
  CHECK(grace.update(0, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(12000, false) == RecoveryGraceResult::ABORTED);
  CHECK(grace.update(30000, true) == RecoveryGraceResult::HOLDING);
  CHECK(!grace.active);

  grace.begin(500);
  CHECK(grace.update(500, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(30500, false) == RecoveryGraceResult::ABORTED);

  grace.begin(0);
  CHECK(grace.update(29999, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(30000, false) == RecoveryGraceResult::ABORTED);
}

void p89_recovery_grace_late_updates_and_wraparound() {
  RecoveryGraceWindow grace;
  grace.begin(0);
  CHECK(grace.update(7000, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(7100, true) == RecoveryGraceResult::HOLDING);
  CHECK(grace.update(10000, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(26000, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(30000, true) == RecoveryGraceResult::ELAPSED);

  constexpr uint32_t base = UINT32_MAX - 1000U;
  grace.begin(base);
  CHECK(grace.update(base, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(base + 4999U, true) == RecoveryGraceResult::HOLDING);
  CHECK(grace.update(base + 5000U, true) == RecoveryGraceResult::BEEP_DUE);
  CHECK(grace.update(base + RECOVERY_ENTRY_GRACE_MS, true) ==
        RecoveryGraceResult::ELAPSED);
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
  settings.runtime.goalWeightDg = 410;
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
  CHECK(reset.runtime.goalWeightDg == 410);
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
  current.records[0].goalWeightDg = 360;
  current.records[0].actualWeightCg = 3600;
  current.records[0].actualWeightSource =
      static_cast<uint8_t>(ActualWeightSource::POST_DRIP);
  strcpy(current.records[0].presetName, "Double");
  finalizeShotLogStore(current);
  CHECK(current.header.schemaVersion == SHOT_LOG_SCHEMA_VERSION);
  CHECK(current.records[0].goalWeightDg == 360);
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
  foreign.header.schemaVersion = 0;  // The 1.0 epoch.
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
  settings.runtime.goalWeightDg = 410;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));

  ShotLog log;
  CHECK(log.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightDg = 360;
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
  CHECK(ble.enabled == 1);
  CHECK(ble.scanIntensity ==
        static_cast<uint8_t>(BleScanIntensity::BALANCED));
  BleScanPersistedSettings onDisk;
  CHECK(readLatestBleScanSettings(onDisk));
  CHECK(verifyFactoryBleScanSettings(onDisk));

  settings.runtime.goalWeightDg = 400;
  finalizePersistedSettings(settings);
  CHECK(savePersistedSettings(settings));
  CHECK(log.append(record));
  ShotLog::setHostSaveSucceeds(false);
  CHECK(!resetAllDurableStores(settings, ble, log, history, lastShot,
                               curves));
  ShotLog::setHostSaveSucceeds(true);
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.goalWeightDg == 400);
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
  record.goalWeightDg = 360;
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
  settings.runtime.goalWeightDg = 410;
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
  CHECK(SHOT_CURVE_MAX_POINTS == 1201);
  CHECK(sizeof(ShotCurveRecord) == 5132);
  CHECK(sizeof(ShotCurveStore) == 2566020);
  CHECK(sizeof(ShotCurveStore) % 4 == 0);
  CHECK(SHOT_CURVE_PARTITION_BYTES == 4016 * 1024);
  ShotCurveRecord full = emptyShotCurveRecord();
  full.count = SHOT_CURVE_MAX_POINTS;
  for (size_t i = 0; i < SHOT_CURVE_MAX_POINTS; ++i) {
    full.weightCg[i] = -32767;
    full.atMs[i] = static_cast<uint16_t>(i * 50);
    full.breakBefore[i / 8U] |= static_cast<uint8_t>(1U << (i % 8U));
    full.held[i / 8U] |= static_cast<uint8_t>(1U << (i % 8U));
  }
  char curveJson[SHOT_CURVE_JSON_CAPACITY] = {};
  CHECK(formatShotCurveJsonBody(curveJson, sizeof(curveJson), full));
  CHECK(strstr(curveJson, "\"wHeld\":[0,1,2") != nullptr);
  CHECK(strstr(curveJson, "\"wAtMs\":[0,50,100") != nullptr);
  CHECK(strstr(curveJson, "60000]") != nullptr);
  ShotCurveLog curves;
  CHECK(curves.load());
  CHECK(curves.count() == 0);
  ShotCurveRecord first = emptyShotCurveRecord();
  first.shotId = 7;
  first.count = 3;
  first.firstDrop.atMs = 4557;
  first.firstDrop.weightCg = 50;
  first.extended.atMs = 18013;
  first.extended.weightCg = 1800;
  first.atm.atMs = 20027;
  first.atm.weightCg = 1900;
  first.ended.atMs = 22057;
  first.ended.weightCg = 1900;
  first.weightCg[0] = 0;
  first.weightCg[1] = 900;
  first.weightCg[2] = 1800;
  first.atMs[2] = 22000;
  CHECK(appendShotCurveObservation(first, 19.0f, 23057, true));
  CHECK(curves.append(first));
  ShotCurveRecord second = emptyShotCurveRecord();
  second.shotId = 8;
  second.count = 2;
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
  CHECK(newest[1].firstDrop.atMs == 4557);
  CHECK(newest[1].extended.atMs == 18013);
  CHECK(newest[1].atm.atMs == 20027);
  CHECK(newest[1].ended.atMs == 22057);
  CHECK(newest[1].count == 4 && newest[1].atMs[3] == 23057);
  CHECK(newest[1].weightCg[3] == 1900 && shotCurveBreakBefore(newest[1], 3));
  CHECK(formatShotCurveJsonBody(curveJson, sizeof(curveJson), newest[1]));
  CHECK(strstr(curveJson, "\"dropS\":4.557") != nullptr);
  CHECK(strstr(curveJson, "\"extendedS\":18.013") != nullptr);
  CHECK(strstr(curveJson, "\"atmS\":20.027") != nullptr);
  CHECK(strstr(curveJson, "\"endS\":22.057") != nullptr);
  CHECK(newest[1].weightCg[2] == 1800);
  ShotCurveRecord exact = {};
  CHECK(reloaded.copyByShotId(7, exact));
  CHECK(exact.shotId == 7);
  CHECK(exact.weightCg[2] == 1800);
  CHECK(!appendShotCurveObservation(exact, 20, 21000, false));
  CHECK(exact.count == 4);
  CHECK(!appendShotCurveObservation(exact, 20, 60001, false));
  CHECK(exact.truncated && exact.count == 4);
  CHECK(!reloaded.copyByShotId(99, exact));
  CHECK(reloaded.removeById(7));
  CHECK(!reloaded.copyByShotId(7, exact));
  CHECK(reloaded.count() == 1);
  CHECK(reloaded.copyNewestFirst(newest, 2) == 1);
  CHECK(newest[0].shotId == 8);
  CHECK(reloaded.clear());
  CHECK(reloaded.count() == 0);
  ShotCurveLog afterClear;
  CHECK(afterClear.load());
  CHECK(afterClear.count() == 0);
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

// Golden pre-schema-3 block, hand-built with the absolute point layout older
// firmware wrote (schema 1 has no held bitmap; schema 2 adds it).
void writeLegacyCurveBlock(uint8_t *block, const ShotCurveRecord &r,
                           uint16_t schema, uint32_t sequence) {
  memset(block, 0xff, SHOT_CURVE_BLOCK_BYTES);
  uint8_t *p = block + sizeof(ShotCurveBlockHeader);
  memcpy(p, &r.atmClearedMs, 18); p += 18;
  for (size_t i = 0; i < r.count; ++i) {
    memcpy(p, &r.atMs[i], 2); p += 2;
    memcpy(p, &r.weightCg[i], 2); p += 2;
  }
  const size_t bits = (r.count + 7U) / 8U;
  memcpy(p, r.breakBefore, bits); p += bits;
  if (schema == 2U) { memcpy(p, r.held, bits); p += bits; }
  ShotCurveBlockHeader h = {};
  h.magic = SHOT_CURVE_MAGIC;
  h.schema = schema;
  h.bytes = static_cast<uint16_t>((p - block + 3U) & ~size_t(3U));
  h.epoch = 0;
  h.sequence = sequence;
  h.floor = 0;
  h.shotId = r.shotId;
  h.count = r.count;
  h.flags = 1;
  h.validity = UINT32_MAX - 1U;  // kCommitted
  memcpy(block, &h, sizeof(h));
  uint32_t crc = crc32Update(0xffffffffU, block, offsetof(ShotCurveBlockHeader, checksum));
  h.checksum = ~crc32Update(crc, block + sizeof(h), h.bytes - sizeof(h));
  memcpy(block, &h, sizeof(h));
}

uint8_t *findCurveBlock(uint32_t shotId) {
  uint8_t *flash = ShotCurveLog::hostFlashForTest();
  for (size_t b = 0; b < SHOT_CURVE_BLOCK_COUNT; ++b) {
    uint8_t *block = flash + 2U * FLASH_IO_SECTOR_BYTES + b * SHOT_CURVE_BLOCK_BYTES;
    ShotCurveBlockHeader h = {};
    memcpy(&h, block, sizeof(h));
    if (h.magic == SHOT_CURVE_MAGIC && h.shotId == shotId &&
        h.validity == UINT32_MAX - 1U) return block;
  }
  return nullptr;
}

void p61c_shot_curve_schema_1_blocks_survive_update() {
  // Curves written before the held bitmap and before delta encoding must
  // still load after an update, beside current-schema records.
  resetHostPersistence();
  ShotCurveRecord legacy = emptyShotCurveRecord();
  legacy.shotId = 41;
  legacy.count = 9;  // Two bitmap bytes: the schemas differ in length.
  for (size_t i = 0; i < legacy.count; ++i) {
    legacy.atMs[i] = static_cast<uint16_t>(i * 100U);
    legacy.weightCg[i] = static_cast<int16_t>(i * 40U);
  }
  legacy.breakBefore[1] = 0x01;  // Index 8 starts a segment.
  ShotCurveRecord held = legacy;
  held.shotId = 42;
  held.held[0] = 0x0c;  // Indices 2 and 3 were held.
  uint8_t *flash = ShotCurveLog::hostFlashForTest();
  writeLegacyCurveBlock(flash + 2U * FLASH_IO_SECTOR_BYTES, legacy, 1, 1);
  writeLegacyCurveBlock(flash + 2U * FLASH_IO_SECTOR_BYTES + SHOT_CURVE_BLOCK_BYTES,
                        held, 2, 2);

  ShotCurveLog updated;
  CHECK(updated.load());
  CHECK(updated.count() == 2);
  ShotCurveRecord loaded = {};
  CHECK(updated.copyByShotId(41, loaded));
  CHECK(loaded.count == 9 && loaded.weightCg[8] == 320);
  CHECK(shotCurveBreakBefore(loaded, 8) && !shotCurveBreakBefore(loaded, 7));
  for (size_t i = 0; i < loaded.count; ++i) CHECK(!shotCurveHeld(loaded, i));
  CHECK(updated.copyByShotId(42, loaded));
  CHECK(shotCurveHeld(loaded, 2) && shotCurveHeld(loaded, 3) &&
        !shotCurveHeld(loaded, 4) && shotCurveBreakBefore(loaded, 8));

  // New records are written as the current schema into the same mixed store;
  // every schema must stay enumerated or older blocks silently disappear.
  ShotCurveRecord current = legacy;
  current.shotId = 43;
  current.held[0] = 0x0c;
  CHECK(updated.append(current));
  ShotCurveLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == 3);
  CHECK(reloaded.copyByShotId(41, loaded) && !shotCurveHeld(loaded, 2));
  CHECK(reloaded.copyByShotId(42, loaded) && shotCurveHeld(loaded, 2));
  CHECK(reloaded.copyByShotId(43, loaded));
  CHECK(shotCurveHeld(loaded, 2) && shotCurveHeld(loaded, 3) &&
        !shotCurveHeld(loaded, 4) && shotCurveBreakBefore(loaded, 8));
  char json[SHOT_CURVE_JSON_CAPACITY] = {};
  CHECK(formatShotCurveJsonBody(json, sizeof(json), loaded));
  CHECK(strstr(json, "\"wBreakBefore\":[8],\"wHeld\":[2,3]") != nullptr);
  CHECK(formatShotCurveJsonBody(json, sizeof(json), loaded, 3));
  CHECK(strstr(json, "\"wHeld\":[3]") != nullptr);
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
  CHECK(strcmp(historyTypeName(HistoryType::BACKFLUSH), "backflush") == 0);
  CHECK(strcmp(historyTypeName(static_cast<HistoryType>(255)), "unknown") == 0);
  CHECK(strcmp(historyTypeName(HistoryType::NO_SCALE_GUARD_ABORTED),
               "no_scale_guard_aborted") == 0);

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
  record.type = static_cast<uint8_t>(HistoryType::BACKFLUSH);
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
  CHECK(page.records[0].type == static_cast<uint8_t>(HistoryType::BACKFLUSH));
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
  CHECK(sizeof(PersistedSettings) == 3056U);
  CHECK(settingsEntries == 196U);
  CHECK(lastShotEntries == 14U);
  CHECK(applicationEntries == 275U);
  CHECK(conservativeEntries == 2394U);
  CHECK(conservativeEntries - applicationEntries == 2119U);
}

void p72_factory_intent_recovers_only_from_nvs_no_space() {
  resetHostPersistence();
  ShotLog log;
  LastShotStore lastShot;
  CHECK(log.load());
  CHECK(lastShot.load());
  ShotLogRecord record = {};
  record.durationDs = 250;
  record.goalWeightDg = 360;
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
  settings.runtime.goalWeightDg = 400;
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
  CHECK(loaded.runtime.goalWeightDg == 400);
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
  CHECK(strcmp(DEFAULT_DEVICE_NAME, "openbrewbyweight") == 0);
  CHECK(validDeviceName("Cafe Bar 2"));
  CHECK(!validDeviceName(""));
  CHECK(!validDeviceName(" leading"));
  CHECK(!validDeviceName("trailing-"));
  CHECK(!validDeviceName("bad_underscore"));
  char host[DEVICE_NAME_CAPACITY];
  CHECK(deviceNameToMdnsHost(host, sizeof(host), "Cafe Bar 2") == 10);
  CHECK(strcmp(host, "cafe-bar-2") == 0);
  CHECK(deviceNameToMdnsHost(host, sizeof(host), DEFAULT_DEVICE_NAME) == 16);
  CHECK(strcmp(host, "openbrewbyweight") == 0);
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
  CHECK(settings.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(settings.lineaMicra.options == LINEA_MICRA_DEFAULT_OPTIONS);
  CHECK(settings.lineaMicra.scaleOptions == LINEA_MICRA_KNOWN_SCALE_OPTIONS);
  CHECK(settings.lineaMicra.connectionType == 0);

  // A checksum-corrupt record must be rejected, never repaired.
  settings.checksum ^= 1;
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &settings,
                           sizeof(settings));
  PersistedSettings migrated;
  CHECK(!loadPersistedSettings(migrated));
  settings.schemaVersion = CONFIG_SCHEMA_VERSION;
  settings.lineaMicra.connectionType = 0;
  settings.checksum = persistedSettingsChecksum(settings);
  settingsSchemaWritesAdmitted.store(false);
  const auto trialRecords = persistence_host::records;
  CHECK(!savePersistedSettings(settings));
  CHECK(!resetPersistedSettingsToFactory(settings));
  CHECK(persistence_host::records == trialRecords);
  settingsSchemaWritesAdmitted.store(true);

  uint8_t oldBlob[sizeof(PersistedSettings) - 1] = {};
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, oldBlob,
                           sizeof(oldBlob));
  PersistedSettings rejected;
  CHECK(!loadPersistedSettings(rejected));

  // A checksum-valid v1 blob is accepted without conversion.
  settings.schemaVersion = CONFIG_SCHEMA_VERSION;
  settings.checksum = 0;
  settings.checksum = persistedSettingsChecksum(settings);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_B, &settings,
                           sizeof(settings));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.schemaVersion == CONFIG_SCHEMA_VERSION);
  CHECK(loaded.lineaMicra.scaleOptions == settings.lineaMicra.scaleOptions);
  CHECK(loaded.lineaMicra.options == settings.lineaMicra.options);
  CHECK(strcmp(loaded.lineaMicra.username, settings.lineaMicra.username) == 0);
  // A corrupted v1 checksum stays invalid.
  settings.checksum ^= 0xff;
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_B, &settings,
                           sizeof(settings));
  CHECK(!loadPersistedSettings(rejected));

  strcpy(settings.lineaMicra.username, "barista@example.com");
  strcpy(settings.lineaMicra.password, "correct horse battery staple");
  memset(settings.lineaMicra.installationPrivateKey, 0x5a,
         sizeof(settings.lineaMicra.installationPrivateKey));
  strcpy(settings.lineaMicra.selectedSerial, "MR123456");
  strcpy(settings.lineaMicra.selectedName, "Kitchen Micra");
  settings.lineaMicra.accountConfigured = true;
  settings.lineaMicra.connectionType = static_cast<uint8_t>(MicraConnectionType::API);
  setLineaMicraOptions(settings.lineaMicra, true, true, true,
                              false, false, 0, false, false, false);
  strcpy(settings.deviceName, "Cafe Bar 2");
  settings.presets.presets[0].lineaMicraBrewTargetDeciC = 935;
  CHECK(savePersistedSettings(settings));
  CHECK(loadPersistedSettings(settings));
  CHECK(settings.lineaMicra.options ==
        (LINEA_MICRA_APPLY_TEMPERATURE | LINEA_MICRA_OBSERVE_STATE |
         LINEA_MICRA_RECOGNIZE_WAKE));
  CHECK(settings.lineaMicra.accountConfigured);
  CHECK(settings.lineaMicra.connectionType == static_cast<uint8_t>(MicraConnectionType::API));
  CHECK(strcmp(settings.lineaMicra.username, "barista@example.com") == 0);
  CHECK(strcmp(settings.lineaMicra.selectedSerial, "MR123456") == 0);
  CHECK(settings.presets.presets[0].lineaMicraBrewTargetDeciC == 935);
  CHECK(strcmp(settings.deviceName, "Cafe Bar 2") == 0);

  setLineaMicraOptions(settings.lineaMicra, false, false, false,
                              true, false, 3, false, false, false);
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
  CHECK(settings.lineaMicra.scaleOptions == LINEA_MICRA_KNOWN_SCALE_OPTIONS);
  CHECK(strcmp(settings.deviceName, DEFAULT_DEVICE_NAME) == 0);
  CHECK(settings.presets.presets[0].lineaMicraBrewTargetDeciC == 930);
}

struct TestCase {
  const char *id;
  void (*function)();
};

void p86_timezone_preference_and_durable_initialization() {
  resetHostPersistence();
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
  CHECK(settings.runtime.timezoneAutomatic == 1);
  CHECK(!durableTimezoneSaved().load());
  strcpy(settings.runtime.timezoneId, "America/Santiago");
  settings.runtime.timezoneSource = 1;
  settings.runtime.timezoneAutomatic = 0;
  persistence_host::failNextWrite = true;
  CHECK(!savePersistedSettings(settings));
  CHECK(!durableTimezoneSaved().load());
  persistence_host::corruptNextWrite = true;
  CHECK(!savePersistedSettings(settings));
  CHECK(!durableTimezoneSaved().load());
  CHECK(savePersistedSettings(settings));
  CHECK(durableTimezoneSaved().load());
  resetDurableStorageRevision();
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(durableTimezoneSaved().load());
  CHECK(loaded.runtime.timezoneAutomatic == 0);
  settings.runtime.timezoneAutomatic = 1;
  CHECK(savePersistedSettings(settings));
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.timezoneAutomatic == 1);

  settings.runtime.ntpSyncEnabled = false;
  CHECK(savePersistedSettings(settings));
  CHECK(loadPersistedSettings(loaded));
  CHECK(!loaded.runtime.ntpSyncEnabled);
  settings.runtime.ntpSyncEnabled = true;
  CHECK(savePersistedSettings(settings));
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.runtime.ntpSyncEnabled);

  // An out-of-range preference byte rejects the record.
  resetHostPersistence();
  settings.runtime.timezoneAutomatic = 0xA5;
  settings.checksum = persistedSettingsChecksum(settings);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &settings, sizeof(settings));
  CHECK(!loadPersistedSettings(loaded));
  CHECK(!durableTimezoneSaved().load());
}

void p90_firmware_mode_store_round_trip_and_factory_reset() {
  resetHostPersistence();
  // A fresh controller (no record) starts transparent.
  CHECK(loadFirmwareMode() == FirmwareMode::COMPATIBILITY);
  CHECK(saveFirmwareMode(FirmwareMode::FULL));
  CHECK(loadFirmwareMode() == FirmwareMode::FULL);
  CHECK(saveFirmwareMode(FirmwareMode::COMPATIBILITY));
  CHECK(loadFirmwareMode() == FirmwareMode::COMPATIBILITY);
  CHECK(!saveFirmwareMode(static_cast<FirmwareMode>(0)));
  CHECK(loadFirmwareMode() == FirmwareMode::COMPATIBILITY);
  // An invalid stored byte also reads as compatibility.
  const uint8_t invalid = 0x7F;
  persistence_host::putRaw(FIRMWARE_MODE_NAMESPACE, FIRMWARE_MODE_KEY, &invalid,
                           sizeof(invalid));
  CHECK(loadFirmwareMode() == FirmwareMode::COMPATIBILITY);
  // Factory reset clears the record: the controller is fresh again.
  CHECK(saveFirmwareMode(FirmwareMode::FULL));
  PersistedSettings settings;
  CHECK(initializeDefaultSettings(settings));
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
  CHECK(resetAllDurableStores(settings, ble, log, history, lastShot,
                              curves));
  CHECK(loadFirmwareMode() == FirmwareMode::COMPATIBILITY);
}

void p61c_timestamped_capacity_and_interrupted_blocks() {
  ShotCurveSampler sampler;
  sampler.reset(UINT32_MAX - 1000U);
  sampler.latchFirstDrop(sampler.startMs + 1057U, 0.5f);
  CHECK(sampler.firstDrop.atMs == 1057);
  for (uint32_t t = 0; t <= 60000; t += 50)
    sampler.accept(12.34f, sampler.startMs + t);
  CHECK(sampler.count == 1201);
  CHECK(!sampler.truncated);
  CHECK(sampler.atMs[0] == 0 && sampler.atMs[1200] == 60000);
  sampler.accept(12.34f, sampler.startMs + 60000);
  CHECK(sampler.truncated && sampler.count == 1201);
  ShotCurveRecord complete;
  sampler.snapshot(complete, 101);
  complete.truncated = false;
  ShotCurveLog::resetHostStorage();
  ShotCurveLog maximum;
  const size_t denseErased = ShotCurveLog::hostEraseBytes();
  const size_t denseWritten = ShotCurveLog::hostWriteBytes();
  CHECK(maximum.load() && maximum.append(complete) && maximum.load());
  // Dense curves commit in one sector: the delta stream halves the payload.
  CHECK(ShotCurveLog::hostEraseBytes() - denseErased == 4096);
  CHECK(ShotCurveLog::hostWriteBytes() - denseWritten == 2760);
  ShotCurveRecord restored;
  CHECK(maximum.copyByShotId(101, restored));
  CHECK(restored.count == 1201 && !restored.truncated && restored.atMs[1200] == 60000);
  CHECK(restored.firstDrop.atMs == 1057);
  ShotCurveRecord full;
  sampler.snapshot(full, 101);
  CHECK(validShotCurveRecord(full));
  char json[SHOT_CURVE_JSON_CAPACITY] = {};
  CHECK(formatShotCurveJsonBody(json, sizeof(json), full));
  CHECK(strstr(json, "\"wTruncated\":true") != nullptr);

  // Interrupt every erase/body/header operation across both physical sectors.
  for (int stop = 0; stop <= 8; ++stop) {
    ShotCurveLog::resetHostStorage();
    ShotCurveLog live;
    CHECK(live.load());
    ShotCurveRecord small = emptyShotCurveRecord();
    small.count = 1;
    small.weightCg[0] = 100;
    for (uint32_t id = 1; id <= SHOT_CURVE_CAPACITY; ++id) {
      small.shotId = id;
      CHECK(live.append(small));
    }
    const size_t erased = ShotCurveLog::hostEraseBytes();
    const size_t written = ShotCurveLog::hostWriteBytes();
    CHECK(live.append(full, false));
    CHECK(ShotCurveLog::hostEraseBytes() == erased);
    ShotCurveLog::setHostFailAfter(stop);
    while (live.flushStep() == FlashStoreStepResult::MORE) {}
    ShotCurveLog::setHostFailAfter(-1);
    ShotCurveLog reloaded;
    CHECK(reloaded.load());
    CHECK(reloaded.count() == SHOT_CURVE_CAPACITY);
    const bool committed = reloaded.containsShotId(SHOT_CURVE_CAPACITY + 1);
    CHECK(reloaded.containsShotId(1) != committed);
    CHECK(ShotCurveLog::hostEraseBytes() - erased <= 8192);
    CHECK(ShotCurveLog::hostWriteBytes() - written <= 2760);
    if (committed) {
      ShotCurveRecord out;
      CHECK(reloaded.copyByShotId(SHOT_CURVE_CAPACITY + 1, out));
      CHECK(out.count == 1201 && out.atMs[1200] == 60000);
      CHECK(out.weightCg[1200] == 1234 && out.truncated);
      CHECK(reloaded.removeById(SHOT_CURVE_CAPACITY + 1));
      CHECK(live.load());
      CHECK(!live.containsShotId(1)); // Retention floor survives newest deletion.
      CHECK(live.count() == SHOT_CURVE_CAPACITY - 1);
    }
  }

  // Genuine repeated weights, duplicate timestamps and healthy slow cadence.
  sampler.reset(0);
  sampler.accept(1, 0);
  sampler.accept(1, 500);
  sampler.accept(1, 500);
  sampler.markBreak();
  sampler.accept(2, 1250);
  CHECK(sampler.count == 4 && sampler.atMs[2] == 500);
  ShotCurveRecord sparse;
  sampler.snapshot(sparse, 1);
  CHECK(!shotCurveBreakBefore(sparse, 1));
  CHECK(shotCurveBreakBefore(sparse, 3));
  sampler.captureEnd(1400, 2);
  sampler.accept(3, 1600);
  CHECK(sampler.count == 4);
  sampler.snapshot(sparse, 1);
  CHECK(settleShotCurveEndWeight(sparse, 3));
  CHECK(sparse.weightCg[3] == 200 && sparse.ended.weightCg == 300);

  // Reuse a former long block for a short record; its untouched second sector
  // is padding, never another curve or part of the short-record checksum.
  ShotCurveLog::resetHostStorage();
  ShotCurveLog reuse;
  CHECK(reuse.load());
  complete.shotId = 1;
  CHECK(reuse.append(complete));
  sparse.count = 1;
  sparse.weightCg[0] = 100;
  for (uint32_t id = 2; id <= SHOT_CURVE_CAPACITY; ++id) {
    sparse.shotId = id;
    CHECK(reuse.append(sparse));
  }
  const size_t erased = ShotCurveLog::hostEraseBytes();
  const size_t programmed = ShotCurveLog::hostWriteBytes();
  sparse.shotId = SHOT_CURVE_CAPACITY + 1;
  CHECK(reuse.append(sparse));
  CHECK(ShotCurveLog::hostEraseBytes() - erased == 4096);
  CHECK(ShotCurveLog::hostWriteBytes() - programmed == 60);
  ShotCurveLog::corruptHostByte(8192 + 4096);
  CHECK(reuse.load() && reuse.count() == SHOT_CURVE_CAPACITY);
  CHECK(reuse.copyByShotId(SHOT_CURVE_CAPACITY + 1, sparse) && sparse.count == 1);
}

void p61d_clear_epochs_and_stale_worker_images() {
  for (int stop = 0; stop <= 1; ++stop) {
    ShotCurveLog::resetHostStorage();
    ShotCurveLog live;
    CHECK(live.load());
    ShotCurveRecord r = emptyShotCurveRecord();
    r.shotId = 1;
    r.count = 1;
    CHECK(live.append(r));
    CHECK(live.removeById(1, false));
    ShotCurveLog::setHostFailAfter(stop);
    while (live.flushStep() == FlashStoreStepResult::MORE) {}
    ShotCurveLog::setHostFailAfter(-1);
    ShotCurveLog reboot;
    CHECK(reboot.load());
    CHECK(reboot.count() == (stop == 0 ? 1 : 0));
  }
  for (int stop = 0; stop <= 3; ++stop) {
    ShotCurveLog::resetHostStorage();
    ShotCurveLog live;
    CHECK(live.load());
    ShotCurveRecord r = emptyShotCurveRecord();
    r.shotId = 1;
    r.count = 1;
    r.weightCg[0] = 100;
    CHECK(live.append(r));
    CHECK(live.clear(false));
    ShotCurveLog::setHostFailAfter(stop);
    while (live.flushStep() == FlashStoreStepResult::MORE) {}
    ShotCurveLog::setHostFailAfter(-1);
    ShotCurveLog reboot;
    CHECK(reboot.load());
    CHECK(reboot.count() == (stop == 3 ? 0 : 1));
    if (stop == 3) {
      r.weightCg[0] = 200;
      CHECK(reboot.append(r));
      CHECK(live.load());
      CHECK(live.copyByShotId(1, r));
      CHECK(r.weightCg[0] == 200);
    }
  }
  ShotCurveLog::resetHostStorage();
  ShotCurveLog live;
  CHECK(live.load());
  ShotCurveRecord r = emptyShotCurveRecord();
  r.shotId = 1;
  r.count = 1;
  r.weightCg[0] = 100;
  CHECK(live.append(r, false));
  ShotCurveLog image;
  live.capturePersistenceImage(image);
  CHECK(live.clear(false));
  while (image.flushStep() == FlashStoreStepResult::MORE) {}
  live.acknowledgePersisted(image, false);
  CHECK(live.dirty() && live.count() == 0);
  CHECK(live.flush());
  ShotCurveLog reboot;
  CHECK(reboot.load() && reboot.count() == 0);
  r.weightCg[0] = 200;
  CHECK(live.append(r, false));
  live.capturePersistenceImage(image);
  CHECK(live.removeById(1, false));
  while (image.flushStep() == FlashStoreStepResult::MORE) {}
  live.acknowledgePersisted(image, false);
  CHECK(live.flush());
  CHECK(reboot.load() && reboot.count() == 0);
  r.shotId = 2;
  CHECK(live.append(r, false));
  live.capturePersistenceImage(image);
  r.shotId = 3;
  CHECK(live.append(r, false));
  while (image.flushStep() == FlashStoreStepResult::MORE) {}
  live.acknowledgePersisted(image, false);
  CHECK(live.dirty() && live.count() == 2);
  CHECK(live.flush());
  CHECK(reboot.load() && reboot.containsId(2) && reboot.containsId(3));
  const size_t writes = ShotCurveLog::hostWriteBytes();
  CHECK(live.append(r));
  CHECK(ShotCurveLog::hostWriteBytes() == writes && live.count() == 2);
}

uint32_t curveFuzzState = 0x9E3779B9U;
uint32_t curveFuzzRand() {
  return curveFuzzState = curveFuzzState * 1664525U + 1013904223U;
}

// Deterministic firmware-producible record: monotonic atMs <= 60000,
// non-missing weights, bitmap bits only below count (clean tails).
ShotCurveRecord curveFuzzRecord(uint32_t id) {
  ShotCurveRecord r = emptyShotCurveRecord();
  r.shotId = id;
  r.truncated = (curveFuzzRand() & 1U) != 0U;
  r.firstDrop.atMs = static_cast<uint16_t>(curveFuzzRand() % 60000U);
  r.firstDrop.weightCg = static_cast<int16_t>(curveFuzzRand() % 30000U);
  r.extended.atMs = static_cast<uint16_t>(curveFuzzRand() % 60000U);
  r.extended.weightCg = static_cast<int16_t>(curveFuzzRand() % 30000U);
  uint32_t at = 0;
  int32_t weight = 0;
  const uint16_t wanted = static_cast<uint16_t>(curveFuzzRand() % 260U);
  size_t i = 0;
  for (; i < wanted; ++i) {
    at += curveFuzzRand() % 700U;
    if (at > 60000U) break;
    int32_t dw = static_cast<int32_t>(curveFuzzRand() % 512U) - 256;
    if (curveFuzzRand() % 16U == 0U) dw = (curveFuzzRand() & 1U) ? 20000 : -20000;
    weight += dw;
    if (weight > 32767) weight -= 40000;
    else if (weight < -32767) weight += 40000;
    r.atMs[i] = static_cast<uint16_t>(at);
    r.weightCg[i] = static_cast<int16_t>(weight);
    if (curveFuzzRand() & 1U)
      r.breakBefore[i / 8U] |= static_cast<uint8_t>(1U << (i % 8U));
    if (curveFuzzRand() & 2U)
      r.held[i / 8U] |= static_cast<uint8_t>(1U << (i % 8U));
  }
  r.count = static_cast<uint16_t>(i);
  return r;
}

// Boundary deltas: dt 254/255/256 and 128 (byte 0x80 in time position),
// dw +/-127/+/-128, dw -1 (byte 0xFF in weight position), escaped absolutes
// with low bytes 0xFF/0x80, the maximum domain gap, and full int16 swing.
ShotCurveRecord curveBoundaryRecord(uint32_t id) {
  static const uint16_t at[9] = {0, 254, 509, 765, 893, 1404, 1404, 1915, 60000};
  static const int16_t weight[9] = {0, 127, 0, 255, 127, 126, 510, -32767, 32767};
  ShotCurveRecord r = emptyShotCurveRecord();
  r.shotId = id;
  r.count = 9;  // Two bitmap bytes exercise tail masking (9 % 8 == 1).
  for (size_t i = 0; i < 9; ++i) {
    r.atMs[i] = at[i];
    r.weightCg[i] = weight[i];
  }
  r.breakBefore[1] = 0x01;  // Index 8 starts a segment.
  r.held[0] = 0x81;         // Indices 0 and 7 were held.
  return r;
}

void p61f_shot_curve_schema3_round_trip_is_lossless() {
  resetHostPersistence();
  ShotCurveLog log;
  CHECK(log.load());
  ShotCurveRecord records[52] = {};
  size_t n = 0;
  records[n] = emptyShotCurveRecord();  // Zero points is a legal curve.
  records[n++].shotId = 1;
  records[n] = emptyShotCurveRecord();
  records[n].shotId = 2;
  records[n].count = 1;
  records[n].atMs[0] = 60000;
  records[n++].weightCg[0] = -32767;
  records[n++] = curveBoundaryRecord(3);
  records[n] = emptyShotCurveRecord();  // Typical dense curve, all direct.
  records[n].shotId = 4;
  records[n].count = SHOT_CURVE_MAX_POINTS;
  for (size_t i = 0; i < SHOT_CURVE_MAX_POINTS; ++i) {
    records[n].atMs[i] = static_cast<uint16_t>(i * 50U);
    records[n].weightCg[i] = 1234;
  }
  ++n;
  for (uint32_t id = 5; id <= 52; ++id) records[n++] = curveFuzzRecord(id);
  for (size_t i = 0; i < n; ++i) CHECK(log.append(records[i], false));
  CHECK(log.flush());
  ShotCurveLog reloaded;
  CHECK(reloaded.load());
  CHECK(reloaded.count() == n);
  for (size_t i = 0; i < n; ++i) {
    ShotCurveRecord out = {};
    CHECK(reloaded.copyByShotId(records[i].shotId, out));
    CHECK(memcmp(&out, &records[i], sizeof(out)) == 0);
  }
}

// Escaped stream: 1000 ms gaps with one weight escape. Counts 3 and 5 land
// byte-exact (no padding); count 4 leaves two padding bytes.
ShotCurveRecord curveEscapeRecord(uint32_t id, uint16_t count) {
  ShotCurveRecord r = emptyShotCurveRecord();
  r.shotId = id;
  r.count = count;
  for (size_t i = 0; i < count; ++i) {
    r.atMs[i] = i >= 3 ? 3100 : static_cast<uint16_t>(1000U * (i + 1U));
    r.weightCg[i] = i == 1 ? 3000 : 0;
  }
  r.breakBefore[0] = 0x04;
  r.held[0] = 0x02;
  return r;
}

void resealCurveBlock(uint8_t *block) {
  ShotCurveBlockHeader h = {};
  memcpy(&h, block, sizeof(h));
  uint32_t crc = crc32Update(0xffffffffU, block, offsetof(ShotCurveBlockHeader, checksum));
  h.checksum = ~crc32Update(crc, block + sizeof(h), h.bytes - sizeof(h));
  memcpy(block, &h, sizeof(h));
}

void p61g_shot_curve_schema3_worst_case_stays_within_budget() {
  resetHostPersistence();
  ShotCurveLog log;
  CHECK(log.load());
  ShotCurveRecord r = emptyShotCurveRecord();
  r.shotId = 1;
  r.count = SHOT_CURVE_MAX_POINTS;
  // Byte-maximum valid record: 235 time escapes (255 ms each stay under the
  // 60000 ms domain; more are impossible) and a weight escape on every point.
  for (size_t i = 0; i < r.count; ++i) {
    r.atMs[i] = static_cast<uint16_t>(i < 235U ? 255U * (i + 1U) : 59925U);
    r.weightCg[i] = static_cast<int16_t>(i % 2U == 0U ? 32767 : -32767);
  }
  const size_t erased = ShotCurveLog::hostEraseBytes();
  const size_t written = ShotCurveLog::hostWriteBytes();
  CHECK(log.append(r));
  CHECK(ShotCurveLog::hostEraseBytes() - erased <= 8192);
  CHECK(ShotCurveLog::hostWriteBytes() - written == 5632);
  const uint8_t *block = findCurveBlock(1);
  CHECK(block != nullptr);
  ShotCurveBlockHeader h = {};
  memcpy(&h, block, sizeof(h));
  CHECK(h.schema == 3 && h.count == SHOT_CURVE_MAX_POINTS && h.flags == 1U);
  CHECK(h.bytes == 5632 && h.bytes <= 7564);
  ShotCurveLog reloaded;
  CHECK(reloaded.load() && reloaded.count() == 1);
  ShotCurveRecord out = {};
  CHECK(reloaded.copyByShotId(1, out));
  CHECK(memcmp(&out, &r, sizeof(out)) == 0);
}

void p61h_shot_curve_schema3_malformed_blocks_are_rejected() {
  // CRC is recomputed after every mutation: it is not a semantic gate.
  // Out-of-bounds walking is caught by the ASan/UBSan profile.
  for (int kind = 0; kind < 5; ++kind) {
    ShotCurveLog::resetHostStorage();
    ShotCurveLog log;
    CHECK(log.load());
    const uint16_t baseCount = kind <= 1 ? 5 : kind == 2 ? 4 : 3;
    CHECK(log.append(curveEscapeRecord(1, baseCount)));
    uint8_t *block = findCurveBlock(1);
    CHECK(block != nullptr);
    ShotCurveBlockHeader h = {};
    memcpy(&h, block, sizeof(h));
    if (kind == 0) h.count = 6;  // Stream shorter than count implies.
    else if (kind == 1) block[73] = 0x80;  // Marker as the final stream byte.
    else if (kind == 2) block[75] = 0x40;  // Nonzero padding byte.
    else if (kind == 3) { block[65] = 0x64; block[66] = 0x00; }  // atMs[2] -> 100.
    else h.bytes = 76;  // bytes/count mismatch.
    memcpy(block, &h, sizeof(h));
    resealCurveBlock(block);
    ShotCurveLog reloaded;
    CHECK(reloaded.load());
    CHECK(!reloaded.containsShotId(1));
    CHECK(reloaded.count() == 0);
  }
}

void p61i_shot_curve_schema3_wire_format_is_pinned() {
  // Hand-built golden schema-3 block: the wire format is pinned from the
  // outside, so a symmetric encode/decode bug (both sides wrong the same
  // way) cannot hide. Marker bytes appear in both positions (dt 128 stores
  // 0x80, dw -1 stores 0xFF), escapes store little-endian absolutes, the
  // block carries zero padding, and dirty bitmap tail bits must load masked.
  static const uint8_t stream[36] = {
      0x00, 0x00,                             // dt 0, dw 0
      0xFE, 0x7F,                             // dt 254, dw +127
      0xFF, 0xFD, 0x01, 0x80, 0xFF, 0x00,     // dt 255 (509), dw +128 (255)
      0xFF, 0xFD, 0x02, 0x80, 0x7F, 0x00,     // dt 256 (765), dw -128 (127)
      0x80, 0xFF,                             // dt 128, dw -1
      0xFF, 0x7C, 0x05, 0x80, 0xFE, 0x01,     // dt 511 (1404), dw +384 (510)
      0x00, 0xFF,                             // dt 0, dw -1
      0x7F, 0x80, 0x01, 0x80,                 // dt 127, dw -33276 (-32767)
      0xFF, 0x60, 0xEA, 0x80, 0xFF, 0x7F};    // dt 58469 (60000), dw +65534
  static const uint16_t at[9] = {0, 254, 509, 765, 893, 1404, 1404, 1531, 60000};
  static const int16_t weight[9] = {0, 127, 255, 127, 126, 510, 509, -32767, 32767};
  ShotCurveRecord expected = emptyShotCurveRecord();
  expected.shotId = 44;
  expected.count = 9;
  for (size_t i = 0; i < 9; ++i) {
    expected.atMs[i] = at[i];
    expected.weightCg[i] = weight[i];
  }
  expected.breakBefore[0] = 0x08;
  expected.breakBefore[1] = 0x01;  // Index 8 starts a segment.
  expected.held[0] = 0x81;         // Indices 0 and 7 were held.

  resetHostPersistence();
  uint8_t *block = ShotCurveLog::hostFlashForTest() + 2U * FLASH_IO_SECTOR_BYTES;
  memset(block, 0xff, SHOT_CURVE_BLOCK_BYTES);
  memcpy(block + sizeof(ShotCurveBlockHeader), &expected.atmClearedMs, 18);
  memcpy(block + 54, stream, sizeof(stream));
  block[90] = expected.breakBefore[0];
  block[91] = 0xFF;  // Index 8 plus dirty tail bits 9-15.
  block[92] = expected.held[0];
  block[93] = 0xFE;  // Dirty tail bits 9-15; index 8 was never held.
  block[94] = block[95] = 0;  // Padding past the bitmaps must be zero.
  ShotCurveBlockHeader h = {};
  h.magic = SHOT_CURVE_MAGIC;
  h.schema = 3;
  h.bytes = 96;  // 54 header+fixed + 36 stream + 4 bitmaps + 2 zero padding.
  h.epoch = 0;
  h.sequence = 1;
  h.floor = 0;
  h.shotId = 44;
  h.count = 9;
  h.flags = 1;
  h.validity = UINT32_MAX - 1U;  // kCommitted
  memcpy(block, &h, sizeof(h));
  resealCurveBlock(block);

  ShotCurveLog golden;
  CHECK(golden.load());
  CHECK(golden.count() == 1);
  ShotCurveRecord loaded = {};
  CHECK(golden.copyByShotId(44, loaded));
  CHECK(memcmp(&loaded, &expected, sizeof(loaded)) == 0);
  CHECK(shotCurveBreakBefore(loaded, 8));
  CHECK(shotCurveHeld(loaded, 7) && !shotCurveHeld(loaded, 8));
}

void p66c_activation_capture_skips_clean_payloads() {
  resetHostPersistence();
  ActivationStores live, image;
  CHECK(live.begin() && live.shotLog.flush());
  ShotCurveRecord curve = emptyShotCurveRecord();
  curve.shotId = 1;
  curve.count = 1;
  CHECK(live.shotCurves.append(curve));
  // Stale clean payloads in the reused image must neither copy nor flush.
  CHECK(image.shotLog.append(ShotLogRecord{}, false));
  HistoryRecord activation = {};
  activation.type = static_cast<uint8_t>(HistoryType::POWER_ON);
  CHECK(live.historyLog.append(activation, false));
  const size_t writesBefore = ShotCurveLog::hostWriteBytes();
  live.capturePersistenceImage(image);
  CHECK(!image.shotLog.dirty() && image.shotLog.count() == 1);
  CHECK(!image.shotCurves.dirty() && image.shotCurves.count() == 0);
  CHECK(image.historyLog.dirty());
  ActivationStoresFlushReport report;
  do { report = image.serviceStep(0, nullptr); } while (!report.complete && !report.anyFail);
  CHECK(!report.anyFail);
  live.acknowledgePersisted(image, true);
  CHECK(!live.historyLog.dirty());
  CHECK(ShotCurveLog::hostWriteBytes() == writesBefore);
  HistoryLog reboot;
  CHECK(reboot.load() && reboot.count() == 1);
  HistoryRecord stored;
  CHECK(reboot.copyNewestFirst(&stored, 1) == 1);
  CHECK(stored.type == static_cast<uint8_t>(HistoryType::POWER_ON));
}

void p61e_selective_curve_capture_preserves_retention_and_retry() {
  for (int failureAfter : {-1, 0, 1, 2, 3, 4, 5, 6}) {
    resetHostPersistence();
    ShotCurveLog live, image, reboot;
    CHECK(live.load());
    ShotCurveRecord record = emptyShotCurveRecord();
    record.count = 1;
    for (uint32_t id = 1; id <= SHOT_CURVE_CAPACITY; ++id) {
      record.shotId = id;
      record.weightCg[0] = static_cast<int16_t>(id);
      CHECK(live.append(record));
    }
    live.capturePersistenceImage(image);
    CHECK(!image.dirty() && image.count() == 0);
    record.shotId = SHOT_CURVE_CAPACITY + 1U;
    record.weightCg[0] = 1001;
    CHECK(live.append(record, false));
    live.capturePersistenceImage(image);
    CHECK(image.dirty() && image.count() == 1);
    CHECK(image.containsId(record.shotId));
    CHECK(!image.containsId(SHOT_CURVE_CAPACITY));
    // A newer live record cannot be cleared by this captured generation.
    record.shotId = SHOT_CURVE_CAPACITY + 2U;
    record.weightCg[0] = 1002;
    CHECK(live.append(record, false));
    ShotCurveLog::setHostFailAfter(failureAfter);
    FlashStoreStepResult result;
    do { result = image.flushStep(); } while (result == FlashStoreStepResult::MORE);
    if (failureAfter < 0) CHECK(result == FlashStoreStepResult::COMPLETE);
    live.acknowledgePersisted(image, false);
    CHECK(live.dirty() && live.count() == SHOT_CURVE_CAPACITY);
    ShotCurveLog::setHostFailAfter(-1);
    live.capturePersistenceImage(image);
    CHECK(image.count() >= 1 && image.count() <= 2);
    do { result = image.flushStep(); } while (result == FlashStoreStepResult::MORE);
    CHECK(result == FlashStoreStepResult::COMPLETE);
    live.acknowledgePersisted(image, true);
    CHECK(!live.dirty());
    CHECK(reboot.load() && reboot.count() == SHOT_CURVE_CAPACITY);
    CHECK(!reboot.containsId(1) && !reboot.containsId(2));
    CHECK(reboot.copyByShotId(SHOT_CURVE_CAPACITY + 1U, record));
    CHECK(record.weightCg[0] == 1001);
    CHECK(reboot.copyByShotId(SHOT_CURVE_CAPACITY + 2U, record));
    CHECK(record.weightCg[0] == 1002);
    CHECK(live.removeById(SHOT_CURVE_CAPACITY + 1U, false));
    live.capturePersistenceImage(image);
    CHECK(image.dirty() && image.count() == 0);
    do { result = image.flushStep(); } while (result == FlashStoreStepResult::MORE);
    CHECK(result == FlashStoreStepResult::COMPLETE);
    live.acknowledgePersisted(image, true);
    CHECK(reboot.load() && reboot.count() == SHOT_CURVE_CAPACITY - 1U);
    CHECK(!reboot.containsId(SHOT_CURVE_CAPACITY + 1U));
  }
}

// Golden settings blob of schema 1, written once when the layout was cut over
// (W4a). Every later firmware must load it unchanged: OTA updates between
// releases keep settings. A schema-0 or old-size blob never loads.
std::string goldenSettingsPath() {
  const std::string file(__FILE__);
  return file.substr(0, file.find_last_of('/')) + "/fixtures/settings_schema1_golden_hex.txt";
}

void p91_schema1_golden_settings_blob_loads() {
  resetHostPersistence();
  if (getenv("GOLDEN_SETTINGS_WRITE") != nullptr) {
    PersistedSettings settings;
    CHECK(initializeDefaultSettings(settings));
    settings.runtime.scaleLossEnabled = false;
    settings.runtime.cupProtectionEnabled = false;
    settings.runtime.touchStopFallbackEnabled = false;
    ShotPreset &dbl = *mutableShotPreset(settings.presets, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE);
    dbl.fastBeforeMs = 26500;
    dbl.fastMarginDg = 35;
    dbl.scaleLossFixedMs = 31000;
    dbl.doseDg = 185;
    dbl.grindHintsEnabled = false;
    dbl.learningEpoch = 42;
    dbl.lineaMicraBrewTargetDeciC = 940;
    uint8_t customId = 0;
    CHECK(duplicateShotPreset(settings.presets, FACTORY_PRESET_ID_SINGLE_FIXED, customId));
    CHECK(renameShotPreset(settings.presets, customId, "Golden"));
    settings.presets.activeId = FACTORY_PRESET_ID_SINGLE_ADAPTIVE;
    finalizePersistedSettings(settings);
    FILE *out = fopen(goldenSettingsPath().c_str(), "w");
    CHECK(out != nullptr);
    if (out == nullptr) return;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&settings);
    for (size_t i = 0; i < sizeof(settings); ++i)
      fprintf(out, "%02x%s", bytes[i], (i + 1) % 32 == 0 ? "\n" : "");
    fclose(out);
  }
  FILE *in = fopen(goldenSettingsPath().c_str(), "r");
  CHECK(in != nullptr);
  if (in == nullptr) return;
  std::string hex;
  for (int c; (c = fgetc(in)) != EOF;) if (c != '\n') hex += static_cast<char>(c);
  fclose(in);
  CHECK(hex.size() == 2 * sizeof(PersistedSettings));
  PersistedSettings golden;
  auto *bytes = reinterpret_cast<uint8_t *>(&golden);
  for (size_t i = 0; i < sizeof(golden); ++i)
    bytes[i] = static_cast<uint8_t>(strtoul(hex.substr(2 * i, 2).c_str(), nullptr, 16));
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &golden, sizeof(golden));
  PersistedSettings loaded;
  CHECK(loadPersistedSettings(loaded));
  CHECK(loaded.schemaVersion == 1 && loaded.runtime.goalWeightDg == DEFAULT_GOAL_WEIGHT_DG);
  CHECK(!loaded.runtime.scaleLossEnabled && !loaded.runtime.cupProtectionEnabled);
  CHECK(!loaded.runtime.touchStopFallbackEnabled && loaded.runtime.avoidAccidentalTouchEnabled);
  CHECK(loaded.presets.count == FACTORY_PRESET_COUNT + 1);
  CHECK(loaded.presets.activeId == FACTORY_PRESET_ID_SINGLE_ADAPTIVE);
  const ShotPreset *dbl = findShotPreset(loaded.presets, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE);
  CHECK(dbl != nullptr && dbl->fastBeforeMs == 26500 && dbl->fastMarginDg == 35);
  CHECK(dbl->scaleLossFixedMs == 31000 && dbl->doseDg == 185 && !dbl->grindHintsEnabled);
  CHECK(dbl->learningEpoch == 42 && dbl->lineaMicraBrewTargetDeciC == 940);
  const ShotPreset *custom = findShotPreset(loaded.presets, FACTORY_PRESET_COUNT + 1);
  CHECK(custom != nullptr && strcmp(custom->name, "Golden") == 0 && !custom->isFactory);
  CHECK(!custom->fastSideEnabled && custom->goalWeightDg == FACTORY_SINGLE_GOAL_WEIGHT_DG);
  {
    // A bank saved before Manual existed gains it on boot, after its customs.
    ShotPresetBank upgraded = loaded.presets;
    addMissingManualShotPreset(upgraded);
    addMissingManualShotPreset(upgraded);
    CHECK(upgraded.count == loaded.presets.count + 1);
    CHECK(upgraded.presets[loaded.presets.count].id == FACTORY_PRESET_ID_MANUAL);
    CHECK(strcmp(findShotPreset(upgraded, FACTORY_PRESET_COUNT + 1)->name, "Golden") == 0);
    ShotPresetBank named = loaded.presets;
    CHECK(renameShotPreset(named, FACTORY_PRESET_COUNT + 1, "Manual"));
    addMissingManualShotPreset(named);
    CHECK(named.count == loaded.presets.count);
    ShotPresetBank full = loaded.presets;
    for (uint8_t id = 0; full.count < MAX_SHOT_PRESETS;) CHECK(createUntitledShotPreset(full, id));
    addMissingManualShotPreset(full);
    CHECK(findShotPresetIndex(full, FACTORY_PRESET_ID_MANUAL) < 0);
  }

  // The same bytes stamped schema 0 (the 1.0 epoch) never load.
  PersistedSettings stale = golden;
  stale.schemaVersion = 0;
  stale.checksum = persistedSettingsChecksum(stale);
  resetHostPersistence();
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &stale, sizeof(stale));
  CHECK(!loadPersistedSettings(loaded));
  // A 1.0-sized blob (3344 bytes) is rejected by size before any field.
  std::string legacy(3344, '\0');
  memcpy(&legacy[0], &stale, sizeof(PersistedSettingsHeader));
  resetHostPersistence();
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, legacy.data(), legacy.size());
  CHECK(!loadPersistedSettings(loaded));
}

// Golden shot-log blob of schema 1 (W4b): header plus two records, each a
// self-contained snapshot of the cut parameters. Later firmware must accept it.
std::string goldenShotLogPath() {
  const std::string file(__FILE__);
  return file.substr(0, file.find_last_of('/')) + "/fixtures/shotlog_schema1_golden_hex.txt";
}

void p92_schema1_golden_shot_log_loads() {
  if (getenv("GOLDEN_SHOTLOG_WRITE") != nullptr) {
    ShotLogStore store;
    resetShotLogStore(store, 7);
    for (uint16_t i = 0; i < 2; ++i) {
      ShotLogRecord &r = store.records[i];
      r.id = 41U + i;
      r.bootId = 7;
      r.durationDs = i == 0 ? 245 : 352;
      r.goalWeightDg = 360;
      r.actualWeightCg = i == 0 ? 3880 : 3340;
      r.shotType = static_cast<uint8_t>(ShotLogType::AUTO);
      shotLogSetPresetId(r, FACTORY_PRESET_ID_DOUBLE_ADAPTIVE);
      r.sideFlags = shotLogPackRating(shotLogPackGuardFlags(true, true), 4);
      r.extractionExtended = shotLogPackExtendedFlags(i == 0, i == 1);
      r.stopDetail = static_cast<uint8_t>(i == 0 ? ShotLogStopDetail::EXTENDED_MAX_WEIGHT
                                                 : ShotLogStopDetail::SLOW_MIN_WEIGHT);
      r.verdict = corridor::packVerdict(i == 0 ? corridor::Verdict::FAST
                                               : corridor::Verdict::SLOW,
                                        false, corridor::LossSource::PROJECTION, false);
      r.cutLineCg = i == 0 ? 3850 : 3340;
      r.arrivalDs = i == 0 ? 232 : 371;
      r.fastBeforeDs = 270;
      r.slowAfterDs = 340;
      r.delayDs = 120;
      r.wallDs = 500;
      r.fastMarginDg = 40;
      r.slowMarginDg = 30;
      r.cutRampDs = 50;
      copyCString(r.presetName, sizeof(r.presetName), "Double \xC2\xB7 Adaptive");
      copyCString(r.scaleName, sizeof(r.scaleName), "Lunar");
    }
    store.header.count = 2;
    store.header.writeIndex = 2;
    store.header.nextRecordId = 43;
    finalizeShotLogStore(store);
    FILE *out = fopen(goldenShotLogPath().c_str(), "w");
    CHECK(out != nullptr);
    if (out == nullptr) return;
    const auto *bytes = reinterpret_cast<const uint8_t *>(&store);
    const size_t used = sizeof(ShotLogHeader) + 2 * sizeof(ShotLogRecord);
    for (size_t i = 0; i < used; ++i)
      fprintf(out, "%02x%s", bytes[i], (i + 1) % 32 == 0 ? "\n" : "");
    fclose(out);
  }
  FILE *in = fopen(goldenShotLogPath().c_str(), "r");
  CHECK(in != nullptr);
  if (in == nullptr) return;
  std::string hex;
  for (int c; (c = fgetc(in)) != EOF;) if (c != '\n') hex += static_cast<char>(c);
  fclose(in);
  CHECK(hex.size() == 2 * (sizeof(ShotLogHeader) + 2 * sizeof(ShotLogRecord)));
  static ShotLogStore store;
  memset(&store, 0, sizeof(store));
  auto *bytes = reinterpret_cast<uint8_t *>(&store);
  for (size_t i = 0; i < hex.size() / 2; ++i)
    bytes[i] = static_cast<uint8_t>(strtoul(hex.substr(2 * i, 2).c_str(), nullptr, 16));
  CHECK(validShotLogStore(store));
  CHECK(store.header.count == 2 && store.header.nextRecordId == 43);
  const ShotLogRecord &fast = store.records[0];
  CHECK(fast.id == 41 && corridor::verdictOf(fast.verdict) == corridor::Verdict::FAST);
  CHECK(fast.cutLineCg == 3850 && fast.arrivalDs == 232 && shotLogRating(fast.sideFlags) == 4);
  CHECK(fast.fastBeforeDs == 270 && fast.slowAfterDs == 340 && fast.delayDs == 120);
  CHECK(fast.wallDs == 500 && fast.fastMarginDg == 40 && fast.slowMarginDg == 30);
  CHECK(fast.cutRampDs == 50 && shotLogPresetId(fast) == FACTORY_PRESET_ID_DOUBLE_ADAPTIVE);
  CHECK(corridor::verdictOf(store.records[1].verdict) == corridor::Verdict::SLOW);
  store.header.schemaVersion = 0;  // The 1.0 epoch never loads.
  store.header.checksum = 0;
  store.header.checksum = shotLogChecksum(store);
  CHECK(!validShotLogStore(store));
}

const TestCase tests[] = {
    {"P66C", p66c_activation_capture_skips_clean_payloads},
    {"P61E", p61e_selective_curve_capture_preserves_retention_and_retry},
    {"P61C", p61c_timestamped_capacity_and_interrupted_blocks},
    {"P61D", p61d_clear_epochs_and_stale_worker_images},
    {"P90", p90_firmware_mode_store_round_trip_and_factory_reset},
    {"P86", p86_timezone_preference_and_durable_initialization},
    {"P85", p85_schema1_is_strict_and_micra_defaults_round_trip},
    {"P91", p91_schema1_golden_settings_blob_loads},
    {"P92", p92_schema1_golden_shot_log_loads},
    {"P82", p82_ble_scan_strict_versions_and_roundtrip},
    {"P82B", p82b_ble_scan_legacy_minute_bytes_stay_readable},
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
    {"P66B", p66b_snapshot_acknowledgement_preserves_newest_generation},
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
    {"P73", p73_scale_friendly_name_validates_and_survives_upsert},
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
    {"P87", p87_recovery_grace_beeps_six_times_then_elapses},
    {"P88", p88_recovery_grace_release_aborts_and_wins_over_expiry},
    {"P89", p89_recovery_grace_late_updates_and_wraparound},
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
    {"P61C", p61c_shot_curve_schema_1_blocks_survive_update},
    {"P61F", p61f_shot_curve_schema3_round_trip_is_lossless},
    {"P61G", p61g_shot_curve_schema3_worst_case_stays_within_budget},
    {"P61H", p61h_shot_curve_schema3_malformed_blocks_are_rejected},
    {"P61I", p61i_shot_curve_schema3_wire_format_is_pinned},
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
