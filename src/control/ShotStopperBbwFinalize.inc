// Control-owned post-shot learning; no actuation or direct flash writes.
bool learnPendingBbw(const PendingShotFinalize &snapshot, float finalWeightG,
                     bool postDripWeightValid) {
  if (pendingFinalizeIsRinse(snapshot) || !snapshot.offsetAnalysis ||
      snapshot.extractionExtended || snapshot.slowExtractionExtended) {
    return false;
  }

  if (!scaleAvailable() || !std::isfinite(finalWeightG) ||
      !std::isfinite(snapshot.weightOffsetG) ||
      currentWeightSequence == snapshot.endedWeightSequence ||
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) <=
          0) {
    serialTrace(LogLevel::INFO,
                "Final weight unavailable; offset unchanged");
    return false;
  }

  if (finalWeightG < 1.0f) {
    serialTrace(LogLevel::INFO,
                "Final weight below history threshold; offset unchanged");
    return false;
  }

  serialTracef(LogLevel::INFO,
               "Final weight: %.2fg; cycle goal: %.2fg; cycle offset: %.2fg",
               finalWeightG, snapshot.goalWeightG, snapshot.weightOffsetG);

  float updatedOffset = 0.0f;
  const bool acceptedFresh = postDripWeightValid && snapshot.scaleBaselineReady &&
      snapshot.endReason == EndReason::SCALE_THRESHOLD &&
      shotLogBbwEligible(snapshot.startedWithScale, snapshot.timerOnly,
                         snapshot.automaticBrew) &&
      currentWeightIsFresh(millis()) &&
      currentWeightConnectionGeneration == snapshot.scaleConnectionGeneration &&
      snapshot.scaleConnectionGeneration == getScaleLinkSnapshot().connectionGeneration;
  if (!bbwEwma::learn(snapshot.weightOffsetG, finalWeightG, snapshot.goalWeightG,
                      snapshot.bbwAlpha, acceptedFresh, updatedOffset)) {
    serialTrace(LogLevel::WARNING,
                "Calculated offset outside safe range; offset unchanged");
    return false;
  }
  // Sensor transport lag sets a physical lower bound: coffee that was in
  // flight for λ_sensor while the scale reported stop. The in-flight rate
  // defaults to 0.8 g/s, but a measured slow shot (long preinfusion,
  // ristretto) lowers the floor to its own true lag mass so learning is not
  // pinned above what the shot physically carried.
  float lagFloorG = 0.0f;
  const uint16_t sensorLagMs = getScaleLinkSnapshot().features.sensorLagMs;
  if (sensorLagMs != 0) {
    float brewS = snapshot.durationDs / 10.0f;
    if (snapshot.firstDropDs != SHOT_LOG_METRIC_MISSING) {
      brewS -= snapshot.firstDropDs / 10.0f;
    }
    const float meanRateG_S =
        brewS > 0.0f ? (finalWeightG - snapshot.scaleBaselineG) / brewS : NAN;
    const float inFlightG_S =
        std::isfinite(meanRateG_S)
            ? fminf(0.8f, fmaxf(meanRateG_S, 0.0f))
            : 0.8f;
    lagFloorG = sensorLagMs / 1000.0f * inFlightG_S;
  }
  updatedOffset = bbwEwma::clampOffset(updatedOffset, lagFloorG);

  ShotPreset *preset = mutableShotPreset(
      presetBank, snapshot.activePresetId != 0 ? snapshot.activePresetId
                                               : presetBank.activeId);
  if (preset == nullptr) {
    serialTrace(LogLevel::WARNING,
                "Offset learn skipped; shot preset no longer exists");
    return false;
  }
  if (snapshot.bbwAlgorithm !=
          static_cast<uint8_t>(BbwAlgorithm::LINEAR_EWMA) ||
      snapshot.bbwProfileVersion != bbwAlgorithmVersion(snapshot.bbwAlgorithm)) return false;
  auto &learning = bbwLearningBank.forPreset(preset->id, presetBank);
  if (snapshot.bbwLearningGeneration != learning.generations[1])
    return false;
  preset->bbwEwmaOffsetG = updatedOffset;
  const uint8_t nextAlpha = learning.evidence.observe(
      snapshot.weightOffsetG + (finalWeightG - snapshot.goalWeightG),
      snapshot.weightOffsetG, snapshot.bbwAlpha);
  if (nextAlpha != preset->bbwEwmaAlpha) preset->bbwAlphaLearned = 1;
  preset->bbwEwmaAlpha = nextAlpha;
  learning.generations[1] = bbwLearningBank.newGeneration();
  RuntimeConfig candidate = runtimeConfig;
  applyShotPresetToConfig(activeShotPreset(presetBank), candidate, true);
  ++candidate.revision;
  if (candidate.revision == 0) {
    candidate.revision = 1;
  }
  commitLiveRuntimeConfig(candidate, RUNTIME_PERSIST_REASON_OFFSET);
  serialTracef(LogLevel::INFO, "New offset pending durable commit: %.2f",
               updatedOffset);
  return true;
}
