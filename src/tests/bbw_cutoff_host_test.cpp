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
  for (uint8_t mode : {0, 1}) {
    assert(std::fabs(predictedWeightStopTimeS(times, weights, 10, 36, 50, mode) - 33) < 1e-5f);
    assert(predictedWeightStopTimeS(times, weights, 9, 36, 50, mode) == 50);
    assert(predictedWeightStopTimeS(times, weights, 10, 20, 50, mode) == 50);
  }
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
  assert(learnBbwOffset(0, 1.5f, 36.2f, 36, 100, false, next));
  assert(std::fabs(next - 1.7f) < 1e-5f);
  assert(learnBbwOffset(1, 1.5f, 36.2f, 36, 30, true, next));
  assert(std::fabs(next - 1.56f) < 1e-5f);
  assert(!learnBbwOffset(1, 1.5f, 36.2f, 36, 30, false, next));
  assert(!learnBbwOffset(1, 1.5f, 42, 36, 10, true, next));
  assert(!learnBbwOffset(1, NAN, 36, 36, 30, true, next));
  assert(!learnBbwOffset(1, 1.5f, 36, 36, 0, true, next));
  for (uint8_t gain : {1, 30, 37, 100}) {
    assert(learnBbwOffset(1, 1.5f, 36.2f, 36, gain, true, next));
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
  assert(learnBbwOffset(1, 0, 35, 36, 30, true, next) && next == 0);
  assert(learnBbwOffset(1, 5, 36, 36, 30, true, next) && next == 5);
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
  PersistedSettings migrated = settings;
  migrated.presets.presets[0].bbwAlgorithm = 0;
  migrated.presets.presets[0].bbwEwmaAlpha = 50;
  migrated.presets.presets[0].bbwAlphaLearned = 1;
  migrated.presets.presets[0].bbwEwmaOffsetG = 0.80f;
  migrated.presets.presets[1].bbwEwmaOffsetG = 3.10f;
  migrated.presets.presets[1].bbwEwmaAlpha = 10;
  assert(savePersistedSettings(migrated));
  PersistedSettings reloaded;
  assert(loadPersistedSettings(reloaded));
  assert(reloaded.presets.presets[0].bbwAlgorithm == 0);
  assert(reloaded.presets.presets[0].bbwEwmaAlpha == 50);
  assert(reloaded.presets.presets[0].bbwAlphaLearned == 1);
  assert(reloaded.presets.presets[0].bbwEwmaOffsetG == 0.80f);
  assert(reloaded.presets.presets[1].bbwEwmaOffsetG == 3.10f);
  assert(reloaded.presets.presets[1].bbwEwmaAlpha == 10);
  migrated.presets.presets[0].bbwAlgorithm = 255;
  migrated.checksum = persistedSettingsChecksum(migrated);
  assert(!validPersistedSettings(migrated));

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
    assert(!validShotLogStore(store, 5));
  }
  std::cout << "BBW numeric, adaptation, state, and history checks passed\n";
}
