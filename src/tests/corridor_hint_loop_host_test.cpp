// Closed-loop acceptance test of the grind hint (design 2026-10-16, section
// 10): a simulated barista pulls shots around a true level with 10 % outliers
// and, on every announcement, moves the grind by the offset the hint shows.
#include "../OpenBrewCorridor.h"

#include <algorithm>
#include <iostream>
#include <vector>

using namespace shotstopper::corridor;
using shotstopper::EndReason;

namespace {

int failures = 0;

#define CHECK(condition)                                                     \
  do {                                                                       \
    if (!(condition)) {                                                      \
      if (++failures <= 40)                                                  \
        std::cerr << __func__ << ":" << __LINE__ << ": check failed: "       \
                  << #condition << "\n";                                     \
    }                                                                        \
  } while (false)

constexpr int RUNS = 2000;
constexpr int DIAL_IN_SHOTS = 15;
constexpr int STATIONARY_SHOTS = 30;
constexpr int TO_BAND_SHOTS = 20;
constexpr int SETTLE_MAX_SHOTS = 30;
constexpr int DETECT_SHOTS = 20;
constexpr int DETECT_PASS_SHOT = 10;

// Knuth's 64-bit LCG; Gaussian noise as the Irwin-Hall sum of 12 uniforms,
// so every platform draws the same integer shots.
struct Rng {
  uint64_t state;
  uint32_t next() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<uint32_t>(state >> 32);
  }
  int32_t permille(int32_t lo, int32_t hi) {
    return lo + static_cast<int32_t>(next() % static_cast<uint32_t>(hi - lo + 1));
  }
  int32_t gauss(int32_t sigmaMs) {
    int64_t sum = 0;
    for (int i = 0; i < 12; ++i) sum += next() >> 16;
    return static_cast<int32_t>((sum - 6 * 65536) * sigmaMs / 65536);
  }
  // One shot in ten also carries a 3-sigma upset (dose, prep, channel).
  int32_t noise(int32_t sigmaMs) {
    const int32_t v = gauss(sigmaMs);
    return next() % 10 == 0 ? v + gauss(3 * sigmaMs) : v;
  }
};

struct Setting {
  const char *name;
  int32_t sigmaMs, t1Ms, t2Ms;
  double reversals;  // most direction reversals per dial-in
  double shiftBy10;  // least share of 1-sigma shifts shown by shot 10
  int32_t middle() const { return (t1Ms + t2Ms) / 2; }
  int32_t half() const { return (t2Ms - t1Ms) / 2; }
  int32_t band() const { return bandHalfMs(half(), sigmaMs); }
};

// One simulated preset: its shot log and the replay that reads it.
struct Bench {
  const Setting &setting;
  Rng &rng;
  StatsScope scope;
  std::vector<ArrivalRecord> log;
  GrindHintReplay replay;
  bool crossCheck;
  int staleRepeats = 0;

  Bench(const Setting &s, Rng &r, bool check)
      : setting(s), rng(r), scope(scopeOf(s)), replay(scope), crossCheck(check) {}

  static StatsScope scopeOf(const Setting &s) {
    StatsScope scope;
    scope.presetId = 1;
    scope.fastBeforeMs = s.t1Ms;
    scope.slowAfterMs = s.t2Ms;
    scope.goalDg = 360;
    return scope;
  }

  // Pulls one shot at the true level; true when it announced a hint.
  bool pull(int32_t levelMs) {
    ArrivalRecord r;
    r.recordId = static_cast<uint32_t>(log.size() + 1);
    r.presetId = 1;
    r.endReason = static_cast<uint8_t>(EndReason::SCALE_THRESHOLD);
    r.verdict = static_cast<uint8_t>(Verdict::ON_RECIPE);
    r.arrivalMs = std::max<int32_t>(1000, levelMs + rng.noise(setting.sigmaMs));
    r.durationMs = r.arrivalMs;
    r.fastBeforeMs = setting.t1Ms;
    r.slowAfterMs = setting.t2Ms;
    r.goalDg = 360;
    log.push_back(r);
    const HintDirection before = replay.hint;
    replay.push(r);
    const bool announced = replay.announceId == r.recordId;
    // No stale hint: never announced on a shot that moved the advised way,
    // which shows "checking" while the advice stands.
    if (before != HintDirection::NONE && replay.movedPermille >= R_MOVED) {
      staleRepeats += announced;
      if (replay.hint != HintDirection::NONE) CHECK(replay.out.state == HintState::CHECKING);
    }
    if (crossCheck) {
      const GrindHintV2 pure =
          grindHintOf(log.size(), [this](size_t i) { return log[i]; }, scope);
      const GrindHintV2 &o = replay.out;
      CHECK(pure.state == o.state && pure.direction == o.direction && pure.tier == o.tier &&
            pure.bin == o.bin && pure.n == o.n && pure.offsetMs == o.offsetMs &&
            pure.atLeast == o.atLeast && pure.segmentStartId == o.segmentStartId);
    }
    return announced;
  }

  // Relearn: shots from here on start a new coffee.
  void relearn() {
    scope.epoch = static_cast<uint32_t>(log.size() + 1);
    replay.scope.epoch = scope.epoch;
  }

  // A barista on the previous bag: centred shots that teach the spread.
  void history() {
    for (int i = 0; i < 10; ++i) pull(setting.middle());
    relearn();
  }

  // The barista follows the shown offset, a little more when it is "at least".
  int32_t move() {
    const int32_t ms = std::abs(replay.out.offsetMs) *
                       rng.permille(replay.out.atLeast ? 1000 : 700, 1300) / 1000;
    return replay.out.direction == HintDirection::FINER ? ms : -ms;
  }

  // Centred shots until the segment has >= 3 shots inside the comfort band:
  // the coffee is dialled in and the run is in maintenance.
  void settle() {
    for (int i = 0; i < SETTLE_MAX_SHOTS; ++i) {
      pull(setting.middle());
      if (replay.out.n >= 3 &&
          std::abs(replay.out.offsetMs) <= bandHalfMs(setting.half(), replay.sigmaMs))
        return;
    }
    CHECK(!"never settled");
  }
};

struct Result {
  double reversalsPerDialIn = 0;
  double falseHintRate = 0;
  int medianShotsToBand = 0;
  double shiftShownBy10 = 0;
  double shiftShownBy8 = 0;
  int medianShiftShot = 0;
  int staleRepeats = 0;
};

Result run(const Setting &s, uint64_t seed) {
  Rng rng{seed};
  Result out;
  int reversals = 0, falseHints = 0;
  std::vector<int> toBand, shift;
  for (int run = 0; run < RUNS; ++run) {
    const bool check = run % 50 == 0;
    // Dial-in: a new bag 3-12 s off the middle, either side.
    {
      Bench b(s, rng, check);
      b.history();
      int32_t level = s.middle() + (rng.next() % 2 ? 1 : -1) * rng.permille(3000, 12000);
      HintDirection last = HintDirection::NONE;
      for (int i = 0; i < DIAL_IN_SHOTS; ++i) {
        if (!b.pull(level)) continue;
        if (last != HintDirection::NONE && b.replay.out.direction != last) ++reversals;
        last = b.replay.out.direction;
        level += b.move();
      }
      out.staleRepeats += b.staleRepeats;
    }
    // Stationary: a dialled-in coffee in maintenance.
    {
      Bench b(s, rng, check);
      b.history();
      b.settle();
      bool any = false;
      for (int i = 0; i < STATIONARY_SHOTS; ++i) any = b.pull(s.middle()) || any;
      falseHints += any;
    }
    // From 6 s off: shots until the true level sits in the comfort band.
    {
      Bench b(s, rng, check);
      b.history();
      int32_t level = s.middle() + (rng.next() % 2 ? 6000 : -6000);
      int shots = TO_BAND_SHOTS + 1;
      for (int i = 0; i < TO_BAND_SHOTS && shots > TO_BAND_SHOTS; ++i) {
        if (b.pull(level)) level += b.move();
        if (std::abs(level - s.middle()) <= s.band()) shots = i + 1;
      }
      toBand.push_back(shots);
      out.staleRepeats += b.staleRepeats;
    }
    // In maintenance the level jumps 1 sigma past the slow comfort edge; the
    // shot on which a coarser hint is first shown, no adjustment made.
    {
      Bench b(s, rng, check);
      b.history();
      b.settle();
      int shown = DETECT_SHOTS + 1;
      for (int i = 0; i < DETECT_SHOTS && shown > DETECT_SHOTS; ++i) {
        b.pull(s.middle() + s.band() + s.sigmaMs);
        if (b.replay.out.state != HintState::NONE &&
            b.replay.out.direction == HintDirection::COARSER)
          shown = i + 1;
      }
      shift.push_back(shown);
    }
  }
  std::sort(toBand.begin(), toBand.end());
  std::sort(shift.begin(), shift.end());
  const auto by = [&shift](int shot) {
    return static_cast<double>(std::upper_bound(shift.begin(), shift.end(), shot) - shift.begin()) / RUNS;
  };
  out.reversalsPerDialIn = static_cast<double>(reversals) / RUNS;
  out.falseHintRate = static_cast<double>(falseHints) / RUNS;
  out.medianShotsToBand = toBand[toBand.size() / 2];
  out.shiftShownBy10 = by(DETECT_PASS_SHOT);
  out.shiftShownBy8 = by(8);
  out.medianShiftShot = shift[shift.size() / 2];
  return out;
}

}  // namespace

int main() {
  // At sigma/h = 0.5 the window is near the limit of what arrival time alone
  // can resolve (the shots-vary guard engages at sigmaLong > 0.8 h); its
  // lower bounds protect against regressions without pretending the setting
  // is resolvable. Its reversals measure a mean of 0.50 across ten seeds:
  // the 0.55 margin removes seed dependence, it does not hide a regression;
  // a change pushing it past 0.55 is real.
  const Setting settings[] = {{"sigma 2.0 s on 27-36 s", 2000, 27000, 36000, 0.5, 0.8},
                              {"sigma 1.0 s on 30-34 s", 1000, 30000, 34000, 0.55, 0.6},
                              {"sigma 3.0 s on 25-39 s", 3000, 25000, 39000, 0.5, 0.8}};
  uint64_t seed = 20261016;
  for (const Setting &s : settings) {
    const Result r = run(s, seed++);
    std::cout << s.name << ": reversals/dial-in " << r.reversalsPerDialIn << ", false hint "
              << r.falseHintRate << ", median shots to band " << r.medianShotsToBand
              << ", 1-sigma shift shown by shot 10 " << r.shiftShownBy10 << " (by 8 "
              << r.shiftShownBy8 << ", median shot " << r.medianShiftShot << "), stale repeats "
              << r.staleRepeats << "\n";
    CHECK(r.reversalsPerDialIn <= s.reversals);
    CHECK(r.falseHintRate <= 0.05);
    CHECK(r.medianShotsToBand <= 5);
    CHECK(r.shiftShownBy10 >= s.shiftBy10);
    CHECK(r.staleRepeats == 0);
  }
  if (failures != 0) {
    std::cerr << failures << " grind hint acceptance check(s) failed\n";
    return 1;
  }
  std::cout << "grind hint closed-loop acceptance test passed\n";
  return 0;
}
