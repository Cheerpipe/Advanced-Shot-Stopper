#pragma once

#include "ShotStopperMicraPowerState.h"

struct cJSON;

namespace shotstopper {

enum class MicraCleaningState : uint8_t { UNKNOWN, OFF, REQUESTED, CLEANING };

struct MicraObservationStamp {
  uint32_t identity = 0;
  uint32_t epoch = 0;
  uint32_t intent = 0;
  uint32_t powerRevision = 0;
  uint32_t temperatureRevision = 0;
  uint32_t connectionRevision = 0;
  uint32_t onlineRevision = 0;
};

struct MicraObservation {
  MicraObservationStamp stamp;
  uint32_t receivedAtMs = 0;
  uint16_t targetDeciC = 0;
  MicraObservationSource source = MicraObservationSource::HTTP;
  LineaMicraObservedMode mode = LineaMicraObservedMode::NONE;
  MicraCleaningState cleaning = MicraCleaningState::UNKNOWN;
  char cleaningLabel[33] = {};
  bool powerPresent = false;
  bool temperaturePresent = false;
  bool temperatureValid = false;
  bool cleaningPresent = false;
  bool cleaningAvailable = false;
  bool connectedPresent = false;
  bool connected = false;
};

bool decodeMicraDashboard(const cJSON *root, MicraObservation &update);

// The facade owns this revision fence and the authoritative status; no copy
// of machine power lives in an adapter. Call only under the facade mutex.
struct MicraObservationFence {
  uint32_t epoch = 0;
  uint32_t powerRevision = 0;
  uint32_t temperatureRevision = 0;
  uint32_t connectionRevision = 0;
  uint32_t onlineRevision = 0;
  bool offline = false;
  bool synchronized = false;

  void invalidate() {
    ++connectionRevision;
    synchronized = false;
  }

  MicraObservationStamp stamp(uint32_t identity, uint32_t intent) const {
    return {identity, epoch, intent, powerRevision, temperatureRevision, connectionRevision, onlineRevision};
  }

  bool merge(LineaMicraStatus &status, LineaMicraPowerStateTracker &power,
             const MicraObservation &update, bool observing,
             bool reconcile = false) {
    if (update.stamp.identity != status.identityGeneration ||
        update.stamp.epoch != epoch) return false;
    const bool push = update.source == MicraObservationSource::WEBSOCKET;
    if (!push && (update.stamp.connectionRevision != connectionRevision ||
        (update.connectedPresent && !update.connected &&
         (update.stamp.powerRevision != powerRevision ||
          update.stamp.onlineRevision != onlineRevision)))) return true;
    if (update.connectedPresent && (push || update.stamp.onlineRevision == onlineRevision)) {
      ++onlineRevision;
      if (offline != !update.connected) {
        offline = !update.connected;
        invalidate();
      }
    }
    if (offline) {
      power.hold(status, observing, update.receivedAtMs);
      status.quality = LineaMicraObservationQuality::STALE;
      return true;
    }
    if (update.powerPresent && observing &&
        (push || update.stamp.powerRevision == powerRevision) &&
        power.noteEvidence(update.stamp.intent,
                           lineaMicraPowerStateForMode(update.mode),
                           update.receivedAtMs, reconcile)) {
      status.observedMode = update.mode;
      status.powerState = lineaMicraPowerStateForMode(update.mode);
      status.effectiveOn = status.powerState != LineaMicraPowerState::OFF;
      status.sampleAtMs = update.receivedAtMs;
      status.powerSource = update.source;
      synchronized = status.powerState != LineaMicraPowerState::UNKNOWN;
      status.quality = update.mode == LineaMicraObservedMode::UNSUPPORTED
                           ? LineaMicraObservationQuality::UNSUPPORTED
                           : update.mode == LineaMicraObservedMode::NONE
                           ? LineaMicraObservationQuality::STALE
                           : LineaMicraObservationQuality::CURRENT;
      ++powerRevision;
    }
    if (update.temperaturePresent &&
        (push || update.stamp.temperatureRevision == temperatureRevision)) {
      status.targetValid = update.temperatureValid;
      status.targetDeciC = update.targetDeciC;
      status.temperatureAtMs = update.receivedAtMs;
      ++temperatureRevision;
    }
    return true;
  }
};

static_assert(std::is_trivially_copyable<MicraObservation>::value);
static_assert(sizeof(MicraObservation) <= 80);

}  // namespace shotstopper
