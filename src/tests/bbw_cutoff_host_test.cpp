#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#include "../ShotStopperBbwLearning.h"
#include "../ShotStopperPersistence.h"
#include "../ShotStopperShotLog.h"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace shotstopper;

int main() {
  float times[10], weights[10];
  for (int i = 0; i < 10; ++i) { times[i] = 20.0f + i; weights[i] = 10.0f + 2 * i; }
  assert(std::fabs(bbwEwma::predict(times, weights, 10, 36, 50) - 33) < 1e-5f);
  assert(bbwEwma::predict(times, weights, 9, 36, 50) == 50);
  assert(bbwEwma::predict(times, weights, 10, 20, 50) == 50);
  // The removed regression mode must parse as an alias, never as value 0.
  uint8_t parsedAlgorithm = 2;
  assert(parseBbwAlgorithm("legacy", parsedAlgorithm) && parsedAlgorithm == 1);
  assert(parseBbwAlgorithm("linear_ewma", parsedAlgorithm) &&
         parsedAlgorithm == 1);
  assert(!parseBbwAlgorithm("regression", parsedAlgorithm));
  assert(!parseBbwAlgorithm(nullptr, parsedAlgorithm));
  assert(strcmp(bbwAlgorithmName(1), "linear_ewma") == 0);
  assert(strcmp(bbwAlgorithmName(0), "unknown") == 0);
  assert(bbwAlgorithmVersion(0) == BBW_PROFILE_VERSION &&
         bbwAlgorithmVersion(1) == BBW_PROFILE_VERSION);
  for (float &time : times) time += 10000.0f;
  assert(std::fabs(bbwEwma::predict(times, weights, 10, 36, 50) - 10033) < 0.002f);
  times[5] = times[4];
  assert(bbwEwma::predict(times, weights, 10, 36, 50) == 50);
  times[5] = NAN;
  assert(bbwEwma::predict(times, weights, 10, 36, 50) == 50);
  for (float &time : times) time = 1.0f;
  assert(bbwEwma::predict(times, weights, 10, 36, 50) == 50);
  double meanTime = 0, meanWeight = 0, numerator = 0, denominator = 0;
  for (int i = 0; i < 10; ++i) {
    times[i] = 50 + i * .17f + i * i * .01f;
    weights[i] = 28 + (times[i] - 50) * 2 + (i % 3) * .03f;
    meanTime += times[i] / 10.0;
    meanWeight += weights[i] / 10.0;
  }
  for (int i = 0; i < 10; ++i) {
    numerator += (times[i] - meanTime) * (weights[i] - meanWeight);
    denominator += (times[i] - meanTime) * (times[i] - meanTime);
  }
  const double reference = meanTime + (36 - meanWeight) / (numerator / denominator);
  assert(std::fabs(bbwEwma::predict(times, weights, 10, 36, 60) - reference) < 1e-5);
  for (float &weight : weights) weight = 40 - weight;
  assert(bbwEwma::predict(times, weights, 10, 36, 60) == 60);
  weights[3] = INFINITY;
  assert(bbwEwma::predict(times, weights, 10, 36, 60) == 60);
  float next = 0;
  assert(bbwEwma::learn(1.5f, 36.2f, 36, 30, true, next));
  assert(std::fabs(next - 1.56f) < 1e-5f);
  assert(!bbwEwma::learn(1.5f, 36.2f, 36, 30, false, next));
  assert(!bbwEwma::learn(1.5f, 42, 36, 10, true, next));
  assert(!bbwEwma::learn(NAN, 36, 36, 30, true, next));
  assert(!bbwEwma::learn(1.5f, 36, 36, 0, true, next));
  for (uint8_t gain : {1, 30, 37, 100}) {
    assert(bbwEwma::learn(1.5f, 36.2f, 36, gain, true, next));
    assert(fabsf(next - (1.5f + gain * .002f)) < 1e-5f);
    uint8_t parsed = 0;
    assert(parseBbwAlphaBaseline(gain / 100.0, parsed) && parsed == gain);
    bbwEwma::Evidence custom;
    uint8_t active = gain;
    for (int i = 0; i < 100; ++i) active = custom.observe(1.5f, 1.5f, active);
    assert(active == gain);  // Equal evidence never replaces a custom incumbent.
    custom = {};
    for (int i = 0; i < 25; ++i) {
      active = custom.observe(3.0f, 1.5f, active);
      if (i < 24) assert(active == gain);
    }
    assert(active == 100);
  }
  for (double invalid : {-1.0, 0.0, 0.001, 0.375, 1.01, double(INFINITY), double(NAN)}) {
    uint8_t parsed = 50;
    assert(!parseBbwAlphaBaseline(invalid, parsed) && parsed == 50);
  }
  assert(bbwEwma::learn(0, 35, 36, 30, true, next) && next == 0);
  assert(bbwEwma::learn(5, 36, 36, 30, true, next) && next == 5);
  bbwEwma::Evidence constant, noisy, changed;
  uint8_t alpha = 30;
  for (int i = 0; i < 100; ++i) assert(constant.observe(1.5f, 1.5f, 30) == 30);
  for (int i = 0; i < 100; ++i) {
    const uint8_t selected = noisy.observe(i % 2 ? 2 : 1, 1.5f, alpha);
    if (i < 24) assert(selected == 30);
    alpha = selected;
  }
  assert(alpha == 10);
  const auto beforeInvalid = noisy;
  assert(noisy.observe(NAN, 1.5f, alpha) == alpha);
  assert(memcmp(&beforeInvalid, &noisy, sizeof(noisy)) == 0);
  alpha = 30;
  for (int i = 0; i < 25; ++i) {
    alpha = changed.observe(3, 1.5f, alpha);
    if (i < 24) assert(alpha == 30);
  }
  assert(alpha == 100);
  assert(changed.anchors[0] < changed.anchors[3]);
  bbwEwma::Evidence noLookAhead;
  assert(noLookAhead.observe(2.5f, 1.5f, 30) == 30);
  for (float anchor : noLookAhead.anchors) assert(anchor == 1.5f);
  // Anchors represent the complete discarded prefix, never a reseeded window.
  bbwEwma::Evidence replay;
  std::vector<float> observations;
  uint8_t replayAlpha = 37;
  for (int n = 0; n < 80; ++n) {
    observations.push_back(1.5f + 0.7f * std::sin(n * .23f));
    replayAlpha = replay.observe(observations.back(), 1.5f, replayAlpha);
    for (int i = 0; i < 5; ++i) {
      const float gain = (i < 4 ? BBW_ALPHA_CANDIDATES[i] : 37) / 100.0f;
      float referenceAnchor = 1.5f;
      for (int j = 0; j <= n - 20; ++j)
        referenceAnchor = bbwEwma::clampOffset(referenceAnchor + gain * (observations[j] - referenceAnchor));
      assert(fabsf(replay.anchors[i] - referenceAnchor) < 1e-6f);
    }
  }

  PersistedSettings settings;
  assert(initializeDefaultSettings(settings));
  assert(settings.runtime.bbwAlgorithm == 1);
  for (const auto &preset : settings.presets.presets) {
    assert(preset.bbwAlgorithm == 1);
    assert(preset.bbwEwmaAlpha == 30);
    assert(preset.weightOffsetG == preset.bbwEwmaOffsetG);
  }
  // A blob persisted by pre-removal firmware with the regression mode
  // active must load as EWMA with the regression offset seeded, and the
  // first shot after migration still learns (fields stay consistent).
  persistence_host::reset();
  PersistedSettings legacyBlob = settings;
  legacyBlob.runtime.bbwAlgorithm = 0;
  legacyBlob.presets.presets[0].bbwAlgorithm = 0;
  legacyBlob.presets.presets[0].bbwProfileVersion = 1;
  legacyBlob.presets.presets[0].weightOffsetG = 2.30f;
  legacyBlob.presets.presets[0].bbwEwmaAlpha = 50;
  legacyBlob.presets.presets[0].bbwAlphaLearned = 1;
  legacyBlob.presets.presets[1].bbwAlgorithm = 0;
  legacyBlob.presets.presets[1].bbwProfileVersion = 1;
  legacyBlob.presets.presets[1].weightOffsetG = 0.70f;
  legacyBlob.presets.presets[1].bbwEwmaOffsetG = 0.90f;  // Real EWMA history.
  legacyBlob.presets.presets[1].bbwEwmaAlpha = 10;
  legacyBlob.checksum = persistedSettingsChecksum(legacyBlob);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacyBlob,
                           sizeof(legacyBlob));
  PersistedSettings reloaded;
  assert(loadPersistedSettings(reloaded));
  // The adopted record carried aliases; boot may invalidate learning once,
  // scoped to exactly the presets that aliased (both factory presets here).
  assert(bbwLegacyAliasesApplied);
  assert(bbwLegacyAliasedPresetCount == 2);
  assert(bbwLegacyAliasedPresetIds[0] == legacyBlob.presets.presets[0].id);
  assert(bbwLegacyAliasedPresetIds[1] == legacyBlob.presets.presets[1].id);
  // A clean newer slot A with a stale legacy losing slot B adopts A and must
  // NOT re-arm the migration flag on later boots.
  PersistedSettings cleanBlob = settings;
  cleanBlob.storageRevision = legacyBlob.storageRevision + 10U;
  cleanBlob.checksum = persistedSettingsChecksum(cleanBlob);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &cleanBlob,
                           sizeof(cleanBlob));
  assert(loadPersistedSettings(reloaded));
  assert(!bbwLegacyAliasesApplied);
  assert(bbwLegacyAliasedPresetCount == 0);
  // The losing slot itself still stores the alias: keep a stale legacy B
  // under the newer clean A and re-verify the flag stays disarmed while B's
  // alias-repair is only probed, never adopted.
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_B, &legacyBlob,
                           sizeof(legacyBlob));
  assert(loadPersistedSettings(reloaded));
  assert(!bbwLegacyAliasesApplied);
  assert(bbwLegacyAliasedPresetCount == 0);
  assert(reloaded.storageRevision == cleanBlob.storageRevision);
  // A legacy record that WINS adoption (newer losing-clean B) re-arms the
  // one-shot invalidation: only the adopted record may arm it.
  legacyBlob.storageRevision = cleanBlob.storageRevision + 5U;
  legacyBlob.checksum = persistedSettingsChecksum(legacyBlob);
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_B, &legacyBlob,
                           sizeof(legacyBlob));
  assert(loadPersistedSettings(reloaded));
  assert(bbwLegacyAliasesApplied);
  assert(reloaded.presets.presets[0].bbwAlgorithm == 1);
  // Restore the adopted-alias case for the seed checks below.
  persistence_host::putRaw(SETTINGS_NAMESPACE, SETTINGS_SLOT_A, &legacyBlob,
                           sizeof(legacyBlob));
  persistence_host::records.erase(
      persistence_host::storageKey(SETTINGS_NAMESPACE, SETTINGS_SLOT_B));
  assert(loadPersistedSettings(reloaded));
  assert(bbwLegacyAliasesApplied);
  assert(reloaded.runtime.bbwAlgorithm == 1);
  assert(reloaded.presets.presets[0].bbwAlgorithm == 1);
  assert(reloaded.presets.presets[0].bbwProfileVersion == BBW_PROFILE_VERSION);
  // Untouched EWMA default: the regression offset becomes the seed.
  assert(fabsf(reloaded.presets.presets[0].bbwEwmaOffsetG - 2.30f) < 1e-5f);
  assert(reloaded.presets.presets[0].bbwEwmaAlpha == 50);
  assert(reloaded.presets.presets[0].bbwAlphaLearned == 1);
  // A preset that already ran EWMA keeps its learned offset untouched.
  assert(reloaded.presets.presets[1].bbwAlgorithm == 1);
  assert(fabsf(reloaded.presets.presets[1].bbwEwmaOffsetG - 0.90f) < 1e-5f);
  assert(reloaded.presets.presets[1].bbwEwmaAlpha == 10);
  assert(validPersistedSettings(reloaded));
  // Seeded offset plus a fresh miss keeps learning on the EWMA path.
  float migratedOffset = 0;
  assert(bbwEwma::learn(reloaded.presets.presets[0].bbwEwmaOffsetG, 36.2f,
                        36, reloaded.presets.presets[0].bbwEwmaAlpha, true,
                        migratedOffset));
  assert(fabsf(migratedOffset - (2.30f + 0.5f * 0.2f)) < 1e-5f);
  // The migrated record round-trips byte-exact through save/load.
  assert(savePersistedSettings(reloaded));
  PersistedSettings secondPass;
  assert(loadPersistedSettings(secondPass));
  assert(!bbwLegacyAliasesApplied);
  assert(memcmp(&secondPass, &reloaded, sizeof(secondPass)) == 0);
  secondPass.presets.presets[0].bbwAlgorithm = 255;
  secondPass.checksum = persistedSettingsChecksum(secondPass);
  assert(!validPersistedSettings(secondPass));
  secondPass.presets.presets[0].bbwAlgorithm = 0;
  secondPass.checksum = persistedSettingsChecksum(secondPass);
  // Validation itself rejects the removed id; only the slot-read path aliases.
  assert(!validPersistedSettings(secondPass));

  ShotPresetBank bank = reloaded.presets;
  BbwLearningBank learning;
  auto &state = learning.forPreset(bank.presets[0].id, bank);
  const uint32_t generation = state.generations[0];
  state.evidence = noisy;
  learning.invalidate(bank.presets[0].id, bank, 1);
  assert(state.generations[0] == generation && state.evidence.count == 0);
  uint8_t duplicate = 0;
  bank.presets[0].bbwAlphaBaseline = 37;
  assert(duplicateShotPreset(bank, bank.presets[0].id, duplicate));
  assert(mutableShotPreset(bank, duplicate)->bbwEwmaAlpha == 50);
  assert(mutableShotPreset(bank, duplicate)->bbwAlphaBaseline == 37);
  uint8_t created = 0;
  assert(createUntitledShotPreset(bank, created));
  assert(findShotPreset(bank, created)->bbwAlphaBaseline == 30);
  assert(restoreFactoryShotPresetValues(bank, FACTORY_PRESET_ID_DOUBLE));
  assert(findShotPreset(bank, FACTORY_PRESET_ID_DOUBLE)->bbwAlphaBaseline == 30);
  assert(learning.forPreset(duplicate, bank).evidence.count == 0);
  state.evidence = noisy;
  unsigned char beforeOther[sizeof(state.evidence)];
  memcpy(beforeOther, &state.evidence, sizeof(beforeOther));
  auto &otherState = learning.forPreset(bank.presets[1].id, bank);
  otherState.evidence.observe(3.0f, 3.10f, 10);
  assert(memcmp(&state.evidence, beforeOther, sizeof(beforeOther)) == 0);
  assert(otherState.evidence.count == 1);

  ShotLogStore store;
  resetShotLogStore(store, 7);
  store.header.count = 1;
  store.header.writeIndex = 1;
  auto &record = store.records[0];
  record.offsetUsedCg = 0;
  record.extractionGuardEnabled = shotLogPackRating(3, 5);
  record.extractionExtended = 3;
  record.shotType = static_cast<uint8_t>(ShotLogType::AUTO);
  record.cutType = static_cast<uint8_t>(ShotLogCut::LIMIT);
  for (unsigned id = 0; id <= 255; ++id) {
    shotLogSetPresetId(record, static_cast<uint8_t>(id));
    assert(shotLogPresetId(record) == id);
    assert(shotLogType(record) == ShotLogType::AUTO);
    assert(shotLogCut(record) == ShotLogCut::LIMIT);
  }
  for (uint8_t gain = 1; gain <= 100; ++gain) {
    shotLogSetBbw(record, 1, 2, gain, true);
    for (unsigned id = 0; id <= 255; ++id) {
      shotLogSetPresetId(record, static_cast<uint8_t>(id));
      assert(shotLogBbwAlpha(record) == gain && shotLogPresetId(record) == id);
      assert(shotLogCut(record) == ShotLogCut::LIMIT);
    }
    assert(strcmp(shotLogBbwVersion(record), "2") == 0);
    record.extractionGuardEnabled = shotLogPackRating(record.extractionGuardEnabled, 4);
    assert(shotLogRating(record.extractionGuardEnabled) == 4);
    assert(shotLogFastGuardEnabled(record.extractionGuardEnabled));
    assert(shotLogSlowExtended(record.extractionExtended));
    assert(shotLogBbwAlpha(record) == gain);
    assert(strcmp(shotLogBbwLearningApplied(record), "true") == 0);
    finalizeShotLogStore(store);
    ShotLogStore decoded = store;
    assert(validShotLogStore(decoded));
    assert(decoded.records[0].offsetUsedCg == 0);
    assert(shotLogPresetId(decoded.records[0]) == 255);
    assert(decoded.records[0].extractionExtended == record.extractionExtended);
    decoded.header.checksum ^= 1;
    assert(!validShotLogStore(decoded));
    decoded.header.checksum ^= 1;
    decoded.header.schemaVersion = 5;
    assert(!validShotLogStore(decoded));
  }
  // Reference-relative fitWeightTrend must agree with the exact absolute-time
  // regression on shot-scale times (~60 s) within 0.01 g at every probe.
  {
    float t[WEIGHT_TREND_POINT_COUNT], w[WEIGHT_TREND_POINT_COUNT];
    for (size_t i = 0; i < WEIGHT_TREND_POINT_COUNT; ++i) {
      t[i] = 55.0f + 0.45f * static_cast<float>(i);
      w[i] = 20.0f + 1.7f * (t[i] - 55.0f) + 0.05f * std::sin(static_cast<float>(i));
    }
    w[WEIGHT_TREND_POINT_COUNT - 1] = 28.6f;  // Last-sample gate passes.
    const WeightTrendFit fit = fitWeightTrend(t, w, WEIGHT_TREND_POINT_COUNT);
    assert(fit.valid);
    assert(fabsf(fit.referenceS - t[WEIGHT_TREND_POINT_COUNT - 1]) < 1e-6f);
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double n = static_cast<double>(WEIGHT_TREND_POINT_COUNT);
    for (size_t i = 0; i < WEIGHT_TREND_POINT_COUNT; ++i) {
      sx += t[i];
      sy += w[i];
      sxx += static_cast<double>(t[i]) * t[i];
      sxy += static_cast<double>(t[i]) * w[i];
    }
    const double slope = (n * sxy - sx * sy) / (n * sxx - sx * sx);
    const double intercept = sy / n - slope * (sx / n);
    for (float probe : {55.0f, 57.5f, 59.1f, 60.0f}) {
      const double expectedG = intercept + slope * probe;
      const float gotG = fit.intercept + fit.slope * (probe - fit.referenceS);
      assert(std::fabs(gotG - expectedG) < 0.01);
    }
  }

  // The per-protocol sensor-lag priors must keep reproducing the flat
  // Single/Double seed magnitudes from reference-pair flows; a table edit
  // that breaks this invalidates the documented seed policy.
  {
    const float acaiaLagS =
        kScaleProtocolAcaia.features.sensorLagMs / 1000.0f;
    assert(fabsf(acaiaLagS * 0.72f - 0.5f) < 0.05f);    // Single seed
    assert(fabsf(acaiaLagS * 2.17f - 1.5f) < 0.05f);    // Double seed
    assert(fabsf(acaiaLagS * 0.8f - 0.552f) < 0.005f);  // Documented floor
    assert(kScaleProtocolAcaiaLegacy.features.sensorLagMs ==
           kScaleProtocolAcaia.features.sensorLagMs);
    // Every shipping protocol carries a prior: a zero would silently
    // disable the learned-offset lag floor for that scale.
    for (const ScaleProtocol *protocol : {&kScaleProtocolAcaiaLegacy,
                                          &kScaleProtocolAcaia,
                                          &kScaleProtocolGenericFf11,
                                          &kScaleProtocolFelicita,
                                          &kScaleProtocolEclair,
                                          &kScaleProtocolDecent,
                                          &kScaleProtocolDifluid,
                                          &kScaleProtocolMyscale,
                                          &kScaleProtocolWeighMyBru,
                                          &kScaleProtocolVaria,
                                          &kScaleProtocolEureka}) {
      assert(protocol->features.sensorLagMs >= 200 &&
             protocol->features.sensorLagMs <= 1000);
    }
  }

  std::cout << "BBW numeric, adaptation, state, and history checks passed\n";
}
