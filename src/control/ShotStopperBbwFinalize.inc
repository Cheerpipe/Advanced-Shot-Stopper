// Control-owned post-shot learning; no actuation or direct flash writes.
bool learnPendingBbw(const PendingShotFinalize &snapshot, float finalWeightG,
                     bool postDripWeightValid) {
  if (pendingFinalizeIsRinse(snapshot) || !snapshot.offsetAnalysis ||
      snapshot.extractionExtended || snapshot.slowExtractionExtended) {
    return false;
  }

  if (!scaleAvailable() || !isfinite(finalWeightG) ||
      !isfinite(snapshot.weightOffsetG) ||
      currentWeightSequence == snapshot.endedWeightSequence ||
      static_cast<int32_t>(currentWeightReceivedAtMs - snapshot.endedAtMs) <=
          0) {
    serialTrace(LogLevel::INFO,
                "Final weight unavailable; offset unchanged");
    return false;
  }

  if (!shotLogWeightEligible(finalWeightG, true)) {
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
  if (!learnBbwOffset(snapshot.bbwAlgorithm, snapshot.weightOffsetG, finalWeightG,
                       snapshot.goalWeightG, snapshot.bbwAlpha, acceptedFresh,
                       updatedOffset)) {
    serialTrace(LogLevel::WARNING,
                "Calculated offset outside safe range; offset unchanged");
    return false;
  }

  ShotPreset *preset = mutableShotPreset(
      presetBank, snapshot.activePresetId != 0 ? snapshot.activePresetId
                                               : presetBank.activeId);
  if (preset == nullptr) {
    serialTrace(LogLevel::WARNING,
                "Offset learn skipped; shot preset no longer exists");
    return false;
  }
  if (snapshot.bbwAlgorithm > 1 ||
      snapshot.bbwProfileVersion != preset->bbwProfileVersion) return false;
  auto &learning = bbwLearningBank.forPreset(preset->id, presetBank);
  if (snapshot.bbwLearningGeneration != learning.generations[snapshot.bbwAlgorithm])
    return false;
  if (snapshot.bbwAlgorithm == 0) {
    preset->weightOffsetG = updatedOffset;
  } else {
    preset->bbwEwmaOffsetG = updatedOffset;
    const uint8_t nextAlpha = learning.evidence.observe(
        snapshot.weightOffsetG + (finalWeightG - snapshot.goalWeightG),
        snapshot.weightOffsetG, snapshot.bbwAlpha);
    if (nextAlpha != preset->bbwEwmaAlpha) preset->bbwAlphaLearned = 1;
    preset->bbwEwmaAlpha = nextAlpha;
  }
  learning.generations[snapshot.bbwAlgorithm] = bbwLearningBank.newGeneration();
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
