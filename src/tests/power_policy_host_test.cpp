#include "../ShotStopperPowerManagement.h"
#include <cassert>
#include <thread>

using namespace shotstopper;

int main() {
  PowerPolicy policy;
  PowerInputs in;
  assert(policy.update(0, in) == PowerProfile::OFF);
  in.enabled = true;
  assert(policy.update(1, in) == PowerProfile::SETTLING);
  assert(policy.update(1001, in) == PowerProfile::IDLE);
  in.machineBusy = true;
  in.physicalEdge = true;
  assert(policy.update(2000, in) == PowerProfile::MANUAL);
  in.physicalEdge = false;
  // A timer started at the initial press must not lower an ongoing operation.
  assert(policy.update(602000, in) == PowerProfile::MANUAL);
  in.machineBusy = false;
  assert(policy.update(602001, in) == PowerProfile::COOLDOWN);
  assert(policy.cooldownRemaining(602001) == 300000);
  in.webActive = true;
  assert(policy.update(603000, in) == PowerProfile::WEB);
  in.webActive = false;
  assert(policy.update(783000, in) == PowerProfile::COOLDOWN);
  in.physicalEdge = true;
  assert(policy.update(902000, in) == PowerProfile::COOLDOWN);
  in.physicalEdge = false;
  assert(policy.update(1201999, in) == PowerProfile::COOLDOWN);
  assert(policy.update(1202000, in) == PowerProfile::SETTLING);
  assert(policy.update(1203000, in) == PowerProfile::IDLE);
  in.scaleBusy = true;
  assert(policy.update(1203001, in) == PowerProfile::WAITING);
  assert(powerMinMhz(PowerProfile::WAITING) == 160);
  assert(powerMaxMhz(PowerProfile::WAITING) == 160);
  in.machineBusy = true;
  assert(policy.update(1203002, in) == PowerProfile::WORKING);
  in.scaleBusy = false;
  in.webActive = true;
  assert(policy.update(1600000, in) == PowerProfile::WORKING);
  assert(policy.update(1609999, in) == PowerProfile::WORKING);
  in.scaleBusy = true;
  assert(policy.update(1610000, in) == PowerProfile::WORKING);
  in.scaleBusy = false;
  assert(policy.update(1610001, in) == PowerProfile::WORKING);
  assert(policy.update(1640000, in) == PowerProfile::WORKING);
  assert(policy.update(1640001, in) == PowerProfile::MANUAL);
  in.scaleBusy = true;
  assert(policy.update(1640002, in) == PowerProfile::WORKING);
  in.scaleBusy = false;
  assert(policy.update(1640003, in) == PowerProfile::WORKING);
  in.machineBusy = false;
  in.webActive = false;
  assert(policy.update(1640004, in) == PowerProfile::WAITING);
  assert(policy.update(1670003, in) == PowerProfile::COOLDOWN);
  in.enabled = false;
  in.scaleBusy = true;
  assert(policy.update(1670004, in) == PowerProfile::OFF);

  // Wraparound, connection flaps and maintenance hold must not admit idle.
  policy = PowerPolicy{};
  in = PowerInputs{};
  in.enabled = true;
  in.physicalEdge = true;
  assert(policy.update(UINT32_MAX - 1000, in) == PowerProfile::COOLDOWN);
  in.physicalEdge = false;
  assert(policy.update(1000, in) == PowerProfile::COOLDOWN);
  assert(policy.cooldownRemaining(1000) == 297999);
  in.maintenance = true;
  assert(policy.update(400000, in) == PowerProfile::MAINTENANCE);
  in.maintenance = false;
  assert(policy.update(400001, in) == PowerProfile::SETTLING);
  in.scaleBusy = true;
  assert(policy.update(400500, in) == PowerProfile::WAITING);
  in.scaleBusy = false;
  assert(policy.update(401001, in) == PowerProfile::WAITING);
  assert(policy.update(431000, in) == PowerProfile::WAITING);
  assert(policy.update(431001, in) == PowerProfile::SETTLING);
  assert(policy.update(432001, in) == PowerProfile::IDLE);
  policy = PowerPolicy{};
  in.scaleBusy = true;
  assert(policy.update(UINT32_MAX - 1001, in) == PowerProfile::WAITING);
  in.scaleBusy = false;
  assert(policy.update(UINT32_MAX - 1000, in) == PowerProfile::WAITING);
  assert(policy.update(28998, in) == PowerProfile::WAITING);
  assert(policy.update(28999, in) == PowerProfile::SETTLING);

  notePowerWebActivity(UINT32_MAX - 1000, 30);
  assert(powerWebActive(1000));
  assert(!powerWebActive(29000));
  assert(!powerWebActive(UINT32_MAX - 1000)); // Expiry cannot resurrect.
  notePowerWebActivity(100, 31);
  assert(!powerWebActive(101));
  notePowerWebActivity(100, 2);
  assert(powerWebActive(2099));
  assert(!powerWebActive(2100));
  std::thread http([] {
    for (uint32_t now = 1; now < 10000; ++now) notePowerWebActivity(now, 30);
  });
  for (uint32_t now = 1; now < 10000; ++now) (void)powerWebActive(now);
  http.join();
  assert(powerWebActive(10000));

  PowerClock clock;
  int calls = 0;
  const auto configure = [&](int minimum, int maximum) {
    ++calls;
    assert(minimum <= maximum);
    return maximum == 160 ? -1 : 0;
  };
  assert(clock.apply(PowerProfile::OFF, configure));
  assert(clock.apply(PowerProfile::IDLE, configure));
  assert(clock.apply(PowerProfile::WAITING, configure));
  assert(clock.applied() == PowerProfile::OFF && clock.error() == -1);
  assert(calls == 4);
  assert(clock.apply(PowerProfile::WORKING, configure));
  assert(calls == 4);  // Latched fault, no reconfiguration storm.
  PowerClock fatal;
  assert(!fatal.apply(PowerProfile::WORKING, [](int, int) { return -2; }));
  assert(!fatal.apply(PowerProfile::OFF, [](int, int) { return 0; }));
}
