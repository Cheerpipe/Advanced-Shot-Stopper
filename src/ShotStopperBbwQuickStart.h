#pragma once

// Drip quick start: right after the drip is set (or in a fresh preset), the
// next three learning-eligible shots set the drip to their median at once
// when they agree within QUICK_START_SPREAD_CG; otherwise normal learning
// carries on. The record lives in its own NVS key, outside the settings blob:
// older firmware ignores it, and an absent record means "off" so an upgrade
// never jumps a drip the owner already tuned.

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ShotStopperPresets.h"

namespace shotstopper {

constexpr uint8_t QUICK_START_SHOTS = 3;
constexpr int16_t QUICK_START_SPREAD_CG = 40;
constexpr uint32_t QUICK_START_MAGIC = 0x51535442;  // "BTSQ"
constexpr uint16_t QUICK_START_VERSION = 1;
constexpr const char *QUICK_START_KEY = "bbwQs";

enum class QuickStartState : uint8_t { OFF = 0, ARMED, APPLIED, DISAGREED };

struct QuickStartEntry {
  uint8_t presetId = 0;
  uint8_t state = 0;  // QuickStartState
  uint8_t count = 0;
  uint8_t reserved = 0;
  int16_t observedCg[QUICK_START_SHOTS] = {};
  int16_t setCg = 0;    // Drip the owner set; meaningful with setAtId.
  int16_t quickCg = 0;  // Drip the quick start chose.
  uint32_t setAtId = 0;    // First shot-log id after the set; 0 = no marker.
  uint32_t quickAtId = 0;  // Shot-log id of the deciding shot.
};

struct QuickStartRecord {
  uint32_t magic = QUICK_START_MAGIC;
  uint16_t version = QUICK_START_VERSION;
  uint16_t size = 0;
  QuickStartEntry entries[MAX_SHOT_PRESETS];
  uint32_t checksum = 0;
};

inline uint32_t quickStartChecksum(const QuickStartRecord &record) {
  const uint8_t *bytes = reinterpret_cast<const uint8_t *>(&record);
  uint32_t hash = 2166136261U;
  for (size_t i = 0; i < offsetof(QuickStartRecord, checksum); ++i) {
    hash = (hash ^ bytes[i]) * 16777619U;
  }
  return hash;
}

inline void finalizeQuickStart(QuickStartRecord &record) {
  record.magic = QUICK_START_MAGIC;
  record.version = QUICK_START_VERSION;
  record.size = sizeof(QuickStartRecord);
  record.checksum = quickStartChecksum(record);
}

inline bool validQuickStart(const QuickStartRecord &record) {
  return record.magic == QUICK_START_MAGIC &&
         record.version == QUICK_START_VERSION &&
         record.size == sizeof(QuickStartRecord) &&
         record.checksum == quickStartChecksum(record);
}

// The preset's entry; with create, a free slot or one of a deleted preset.
inline QuickStartEntry *quickStartEntry(QuickStartRecord &record,
                                        const ShotPresetBank &bank,
                                        uint8_t presetId, bool create) {
  for (QuickStartEntry &entry : record.entries)
    if (entry.presetId == presetId && presetId != 0) return &entry;
  if (!create) return nullptr;
  for (QuickStartEntry &entry : record.entries) {
    if (entry.presetId == 0 || findShotPresetIndex(bank, entry.presetId) < 0) {
      entry = QuickStartEntry{};
      entry.presetId = presetId;
      return &entry;
    }
  }
  return nullptr;
}

inline void armQuickStart(QuickStartRecord &record, const ShotPresetBank &bank,
                          uint8_t presetId, float dripG, uint32_t setAtId) {
  QuickStartEntry *entry = quickStartEntry(record, bank, presetId, true);
  if (entry == nullptr) return;
  *entry = QuickStartEntry{};
  entry->presetId = presetId;
  entry->state = static_cast<uint8_t>(QuickStartState::ARMED);
  entry->setCg = static_cast<int16_t>(dripG * 100.0f + 0.5f);
  entry->setAtId = setAtId;
}

// A fresh or factory-reset controller: every brew-by-weight preset quick
// starts from its factory drip (no "set" marker).
inline QuickStartRecord armedQuickStart(const ShotPresetBank &bank) {
  QuickStartRecord record;
  for (uint8_t i = 0; i < bank.count; ++i)
    if (bank.presets[i].brewByWeight)
      armQuickStart(record, bank, bank.presets[i].id,
                    bank.presets[i].bbwEwmaOffsetG, 0);
  return record;
}

// Records one eligible observation (offset used + final − cut line, raw) of an
// armed preset and returns true; jumpCg becomes the median when the third one
// agrees with the other two (the deciding shot then skips its EWMA step).
inline bool observeQuickStart(QuickStartRecord &record, const ShotPresetBank &bank,
                              uint8_t presetId, int16_t observedCg,
                              uint32_t recordId, int16_t &jumpCg) {
  QuickStartEntry *entry = quickStartEntry(record, bank, presetId, false);
  if (entry == nullptr || entry->count >= QUICK_START_SHOTS ||
      entry->state != static_cast<uint8_t>(QuickStartState::ARMED)) return false;
  entry->observedCg[entry->count++] = observedCg;
  if (entry->count < QUICK_START_SHOTS) return true;
  const int16_t *o = entry->observedCg;
  const int16_t lo = o[0] < o[1] ? (o[0] < o[2] ? o[0] : o[2]) : (o[1] < o[2] ? o[1] : o[2]);
  const int16_t hi = o[0] > o[1] ? (o[0] > o[2] ? o[0] : o[2]) : (o[1] > o[2] ? o[1] : o[2]);
  entry->quickAtId = recordId;
  if (hi - lo > QUICK_START_SPREAD_CG) {
    entry->state = static_cast<uint8_t>(QuickStartState::DISAGREED);
    return true;
  }
  const int16_t median = static_cast<int16_t>(o[0] + o[1] + o[2] - lo - hi);
  const int16_t maxCg = static_cast<int16_t>(MAX_OFFSET_G * 100.0f);
  jumpCg = median < 0 ? 0 : median > maxCg ? maxCg : median;
  entry->quickCg = jumpCg;
  entry->state = static_cast<uint8_t>(QuickStartState::APPLIED);
  return true;
}

}  // namespace shotstopper

#if !defined(SHOT_STOPPER_HOST_TEST) ||                                      \
    defined(SHOT_STOPPER_PERSISTENCE_HOST_TEST)
#include "ShotStopperPersistence.h"
#include "ShotStopperPreferences.h"

namespace shotstopper {

// An absent, unreadable or invalid record leaves every preset off.
inline bool loadQuickStart(QuickStartRecord &record) {
  QuickStartRecord read;
  bool ok = false;
  if (lockSettingsNvs()) {
    ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
    if (preferences.begin(SETTINGS_NAMESPACE, true)) {
      ok = preferences.getBytesLength(QUICK_START_KEY) == sizeof(read) &&
           preferences.getBytes(QUICK_START_KEY, &read, sizeof(read)) ==
               sizeof(read) &&
           validQuickStart(read);
      preferences.end();
    }
    unlockSettingsNvs();
  }
  if (ok) record = read;
  return ok;
}

// Writes and read-back verifies the record. NVS replaces a key atomically,
// so a power cut leaves either the previous or the new record.
inline bool saveQuickStart(QuickStartRecord record) {
  finalizeQuickStart(record);
  if (!lockSettingsNvs()) return false;
  ShotStopperPreferences preferences(NvsSubsystem::SETTINGS);
  if (!preferences.begin(SETTINGS_NAMESPACE, false)) {
    unlockSettingsNvs();
    return false;
  }
  QuickStartRecord verified;
  const bool saved =
      preferences.putBytes(QUICK_START_KEY, &record, sizeof(record)) ==
          sizeof(record) &&
      preferences.getBytes(QUICK_START_KEY, &verified, sizeof(verified)) ==
          sizeof(verified) &&
      memcmp(&verified, &record, sizeof(record)) == 0;
  preferences.end();
  unlockSettingsNvs();
  return saved;
}

}  // namespace shotstopper
#endif
