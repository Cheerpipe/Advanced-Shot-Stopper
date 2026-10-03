#include "machine/ShotStopperMicraObservation.h"
#include <cJSON.h>
#include <cassert>
#include <cstring>
#include <mutex>
#include <thread>

using namespace shotstopper;

static bool decode(const char *json, MicraObservation &update) {
  cJSON *root = cJSON_Parse(json);
  const bool valid = decodeMicraDashboard(root, update);
  cJSON_Delete(root);
  return valid;
}

static void initialStateContinuity() {
  for (bool pushFirst : {false, true}) {
    LineaMicraStatus state;
    state.identityGeneration = 1;
    state.connectionFreshness = true;
    LineaMicraPowerStateTracker power;
    MicraObservationFence fence;
    MicraObservation initial;
    initial.stamp = fence.stamp(1, power.generation());
    initial.source = MicraObservationSource::HTTP_INITIAL;
    initial.powerPresent = initial.temperaturePresent = initial.temperatureValid = true;
    initial.mode = LineaMicraObservedMode::STANDBY;
    initial.targetDeciC = 930;
    initial.receivedAtMs = 100;
    MicraObservation push = initial;
    push.source = MicraObservationSource::WEBSOCKET;
    push.temperaturePresent = false;
    push.mode = LineaMicraObservedMode::BREWING;
    push.receivedAtMs = 101;
    if (pushFirst) assert(fence.merge(state, power, push, true));
    assert(fence.merge(state, power, initial, true, true));
    assert(state.targetDeciC == 930);
    assert(state.powerSource == (pushFirst ? MicraObservationSource::WEBSOCKET :
                                            MicraObservationSource::HTTP_INITIAL));
    assert(state.powerState == (pushFirst ? LineaMicraPowerState::ON : LineaMicraPowerState::OFF));
    assert(fence.synchronized);
    for (uint32_t now : {30100U, 60100U, 600100U, 3600100U})
      assert(power.effectiveStatus(state, true, now).quality == LineaMicraObservationQuality::CURRENT);
    assert(fence.merge(state, power, push, true));
    assert(state.powerSource == MicraObservationSource::WEBSOCKET);
    auto staleOffline = initial;
    staleOffline.connectedPresent = true;
    staleOffline.connected = false;
    assert(fence.merge(state, power, staleOffline, true, true));
    assert(fence.synchronized && !fence.offline && !power.retained());
    initial.stamp = fence.stamp(1, power.generation());
    initial.source = MicraObservationSource::HTTP;
    initial.receivedAtMs = 200;
    assert(fence.merge(state, power, initial, true, true));  // Later explicit refresh.
    assert(state.powerSource == MicraObservationSource::HTTP && state.sampleAtMs == 200);

    MicraObservation offline;
    offline.source = MicraObservationSource::WEBSOCKET;
    offline.stamp = fence.stamp(1, power.generation());
    offline.connectedPresent = true;
    offline.receivedAtMs = 300;
    auto late = initial;
    late.stamp = offline.stamp;
    assert(fence.merge(state, power, offline, true));
    assert(fence.offline && !fence.synchronized && power.retained());
    late.connectedPresent = late.connected = true;
    assert(fence.merge(state, power, late, true, true));
    assert(fence.offline && power.retained() && state.sampleAtMs == 200);
    offline.connected = true;
    assert(fence.merge(state, power, offline, true));
    assert(!fence.offline && !fence.synchronized && power.retained());
    push.powerPresent = false;
    assert(fence.merge(state, power, push, true));
    assert(power.effectiveStatus(state, true, 400).quality == LineaMicraObservationQuality::STALE);
    push.powerPresent = true;
    push.receivedAtMs = 500;
    assert(fence.merge(state, power, push, true));
    assert(fence.synchronized && !power.retained());
    late.connected = false;  // An older HTTP offline result cannot undo recovery.
    assert(fence.merge(state, power, late, true, true));
    assert(fence.synchronized && !fence.offline && state.sampleAtMs == 500);
    ++fence.epoch;
    assert(!fence.merge(state, power, late, true, true));

    state.connectionFreshness = false;
    assert(power.effectiveStatus(state, true, 30500).quality == LineaMicraObservationQuality::STALE);
  }
}

int main() {
  initialStateContinuity();
  // Synthetic dashboard fixtures; no cloud or hardware is contacted.
  MicraObservation update;
  assert(decode(R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"StandBy","status":"Brewing"}},{"code":"CMCoffeeBoiler","output":{"targetTemperature":93.5}}]})", update));
  assert(update.mode == LineaMicraObservedMode::STANDBY);
  assert(update.targetDeciC == 935 && update.temperatureValid);
  update = {};
  assert(!decode(R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"BrewingMode"}},{"code":"CMCoffeeBoiler","output":{"targetTemperature":"93"}}]})", update));
  assert(!update.powerPresent);  // No partial publication from invalid data.
  assert(!decode(R"({"widgets":[{"code":"CMCoffeeBoiler","output":{"targetTemperature":100.1}}]})", update));
  assert(decode(R"({"connected":false,"widgets":[{"code":"FutureWidget"}]})", update));
  assert(update.connectedPresent && !update.connected && !update.powerPresent);
  update = {};
  assert(!decode(R"({"connected":true,"connected":false})", update));
  assert(!decode(R"({"widgets":[],"widgets":[]})", update));
  assert(!decode(R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"StandBy"}},{"code":"CMMachineStatus","output":{"mode":"BrewingMode"}}]})", update));
  assert(!update.powerPresent);
  assert(!decode(R"({"widgets":[{"code":"CMMachineStatus","output":{"mode":"BrewingMode","mode":null}}]})", update));
  assert(!decode(R"({"widgets":[{"code":"CMMachineStatus","code":"Future","output":{"mode":"BrewingMode"}}]})", update));
  update.source = MicraObservationSource::WEBSOCKET;
  assert(decode(R"({"widgets":[{"code":"CMBackFlush","output":{"status":"123456789012345678901234567890éé"}}]})", update));
  assert(std::strcmp(update.cleaningLabel, "123456789012345678901234567890é") == 0);
  assert(update.cleaning == MicraCleaningState::UNKNOWN);

  for (const char *label : {"Requested", "Cleaning", "Off", "Future"}) {
    char json[120];
    std::snprintf(json, sizeof(json), "{\"widgets\":[{\"code\":\"CMBackFlush\",\"output\":{\"status\":\"%s\"}}]}", label);
    update = {};
    assert(decode(json, update) && !update.cleaningPresent);
    update.source = MicraObservationSource::WEBSOCKET;
    assert(decode(json, update) && update.cleaningPresent && update.cleaningAvailable);
    assert(std::strcmp(update.cleaningLabel, label) == 0);
  }
  update = {};
  update.source = MicraObservationSource::WEBSOCKET;
  assert(decode(R"({"removedWidgets":["CMBackFlush","CMMachineStatus","CMCoffeeBoiler"]})", update));
  assert(update.cleaningPresent && !update.cleaningAvailable);
  assert(update.powerPresent && update.mode == LineaMicraObservedMode::NONE);
  assert(update.temperaturePresent && !update.temperatureValid);
  update = {};
  assert(decode(R"({"removedWidgets":[{"code":"CMMachineStatus"},{"code":"CMCoffeeBoiler"}]})", update));
  assert(update.powerPresent && update.mode == LineaMicraObservedMode::NONE);
  assert(update.temperaturePresent && !update.temperatureValid);

  LineaMicraStatus state;
  state.identityGeneration = 1;
  state.sampleAtMs = 100;
  state.powerState = LineaMicraPowerState::ON;
  state.quality = LineaMicraObservationQuality::CURRENT;
  LineaMicraPowerStateTracker power;
  MicraObservationFence fence;
  MicraObservation old;
  old.stamp = fence.stamp(1, power.generation());
  old.powerPresent = old.temperaturePresent = old.temperatureValid = true;
  old.mode = LineaMicraObservedMode::BREWING;
  old.targetDeciC = 930;
  old.receivedAtMs = 200;
  MicraObservation push = old;
  push.source = MicraObservationSource::WEBSOCKET;
  push.temperaturePresent = false;
  push.mode = LineaMicraObservedMode::STANDBY;
  push.receivedAtMs = 150;
  assert(fence.merge(state, power, push, true));
  assert(fence.merge(state, power, old, true));
  assert(state.powerState == LineaMicraPowerState::OFF && state.sampleAtMs == 150);
  assert(state.targetDeciC == 930);  // Independent field revisions.

  assert(power.notePhysicalStart(state, true, true, 300));
  const auto intent = power.generation();
  assert(!power.notePhysicalStart(state, true, true, 301));
  assert(fence.merge(state, power, old, true));
  assert(power.effectiveStatus(state, true, 302).optimisticOn);
  push.stamp = fence.stamp(1, intent);
  push.receivedAtMs = 400;
  assert(fence.merge(state, power, push, true));  // Contradictory WS keeps optimism.
  assert(power.effectiveStatus(state, true, 401).optimisticOn);
  power.hold(state, true, 402);
  ++fence.epoch;
  assert(!fence.merge(state, power, push, true));
  assert(power.effectiveStatus(state, true, 100000).effectiveOn);
  assert(power.effectiveStatus(state, true, 100000).quality == LineaMicraObservationQuality::STALE);
  push.stamp = fence.stamp(1, intent);
  push.powerPresent = false;
  push.temperaturePresent = push.temperatureValid = true;
  assert(fence.merge(state, power, push, true) && power.retained());
  push.powerPresent = true;
  push.mode = LineaMicraObservedMode::BREWING;
  push.receivedAtMs = 100001;
  assert(fence.merge(state, power, push, true) && !power.retained());
  assert(power.effectiveStatus(state, true, 100002).effectiveOn);

  // Same production reducer, concurrent facade-style publication/snapshots.
  std::mutex mutex;
  std::thread receiver([&] {
    for (unsigned i = 0; i < 1000; ++i) {
      std::lock_guard<std::mutex> lock(mutex);
      push.stamp = fence.stamp(1, power.generation());
      push.receivedAtMs = 110000 + i;
      assert(fence.merge(state, power, push, true));
    }
  });
  for (unsigned i = 0; i < 1000; ++i) {
    std::lock_guard<std::mutex> lock(mutex);
    assert(power.effectiveStatus(state, true, 112000).effectiveOn);
  }
  receiver.join();
}
