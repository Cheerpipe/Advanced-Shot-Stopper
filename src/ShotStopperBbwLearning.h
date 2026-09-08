#pragma once

#include "ShotStopperBbwCutoff.h"
#include "ShotStopperPresets.h"

namespace shotstopper {

// Control-owned RAM, keyed by preset identity (not its movable array index).
struct BbwLearningBank {
  struct State {
    uint8_t id = 0;
    uint32_t generations[2] = {};
    bbwEwma::Evidence evidence;
  } states[MAX_SHOT_PRESETS];
  uint32_t nextGeneration = 0;

  uint32_t newGeneration() {
    if (++nextGeneration == 0) ++nextGeneration;
    return nextGeneration;
  }

  State &forPreset(uint8_t id, const ShotPresetBank &bank) {
    for (State &state : states) if (state.id == id) return state;
    for (State &state : states) {
      if (state.id == 0 || findShotPresetIndex(bank, state.id) < 0) {
        state = State{};
        state.id = id;
        state.generations[0] = newGeneration();
        state.generations[1] = newGeneration();
        return state;
      }
    }
    // All eight extant presets are already present, so valid IDs return above.
    return states[0];
  }

  void invalidate(uint8_t id, const ShotPresetBank &bank, int algorithm = -1) {
    State &state = forPreset(id, bank);
    for (int i = 0; i < 2; ++i) {
      if (algorithm < 0 || algorithm == i) state.generations[i] = newGeneration();
    }
    if (algorithm != 0) state.evidence = bbwEwma::Evidence{};
  }
};
static_assert(sizeof(BbwLearningBank) <= 3000, "BBW per-preset RAM budget");

}  // namespace shotstopper
