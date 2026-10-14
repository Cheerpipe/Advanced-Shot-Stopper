#include "../ShotStopperCorridor.h"

#include <cJSON.h>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

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

cJSON *loadVectors() {
  std::ifstream file(CORRIDOR_FIXTURE_DIR "/corridor_vectors_v1.json");
  std::stringstream text;
  text << file.rdbuf();
  return cJSON_Parse(text.str().c_str());
}

int32_t num(const cJSON *object, const char *key) {
  return static_cast<int32_t>(cJSON_GetObjectItemCaseSensitive(object, key)->valuedouble);
}
bool flag(const cJSON *object, const char *key) {
  return cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(object, key));
}

Params paramsOf(const cJSON *presets, const char *name) {
  const cJSON *p = cJSON_GetObjectItemCaseSensitive(presets, name);
  Params out;
  out.goalDg = num(p, "goalDg");
  out.fastMarginDg = num(p, "fastMarginDg");
  out.slowMarginDg = num(p, "slowMarginDg");
  out.fastOn = flag(p, "fastOn");
  out.slowOn = flag(p, "slowOn");
  out.delayMs = num(p, "delayMs");
  out.fastBeforeMs = num(p, "fastBeforeMs");
  out.slowAfterMs = num(p, "slowAfterMs");
  out.wallMs = num(p, "wallMs");
  out.rampMs = num(p, "rampMs");
  return out;
}

// f bounded, non-increasing, exact at the knots and close to straight ramps.
void checkCutLineInvariants(const Params &p) {
  const int32_t ceiling = ceilingDg(p), floor = floorDg(p);
  CHECK(cutLineDg(p, p.fastBeforeMs) == p.goalDg);
  CHECK(cutLineDg(p, p.slowAfterMs) == (p.rampMs > 0 ? p.goalDg : floor));
  CHECK(cutLineDg(p, fastRampStartMs(p)) == (fastRampStartMs(p) < p.fastBeforeMs ? ceiling : p.goalDg));
  CHECK(cutLineDg(p, slowRampEndMs(p)) == floor);
  const int32_t a = fastRampStartMs(p), b = slowRampEndMs(p);
  const int32_t fastBound = static_cast<int32_t>(std::ceil(0.0963 * (ceiling - p.goalDg)));
  const int32_t slowBound = static_cast<int32_t>(std::ceil(0.0963 * (p.goalDg - floor)));
  int32_t previous = cutLineDg(p, p.delayMs);
  for (int32_t t = p.delayMs; t <= p.wallMs; ++t) {
    const int32_t f = cutLineDg(p, t);
    CHECK(f <= previous);
    CHECK(f <= ceiling && f >= floor);
    previous = f;
    if (t >= a && t < p.fastBeforeMs && p.fastBeforeMs > a) {
      const double straight = p.goalDg + (ceiling - p.goalDg) *
          static_cast<double>(p.fastBeforeMs - t) / (p.fastBeforeMs - a);
      CHECK(std::fabs(f - straight) <= fastBound + 1);
    }
    if (t > p.slowAfterMs && t < b) {
      const double straight = p.goalDg - (p.goalDg - floor) *
          static_cast<double>(t - p.slowAfterMs) / (b - p.slowAfterMs);
      CHECK(std::fabs(f - straight) <= slowBound + 1);
    }
    if (t >= p.fastBeforeMs && t < p.slowAfterMs) CHECK(f == p.goalDg);
  }
}

int32_t bruteCrossing(const Params &p, int32_t tk, int32_t w, int32_t q, int32_t tau,
                      int32_t r) {
  const int32_t lead = leadDg(q, tau);
  for (int32_t t = tk; t <= p.wallMs; ++t) {
    if (w + static_cast<int64_t>(q) * (t - tk) / 100000 + lead + r >= cutLineDg(p, t)) return t;
  }
  return p.wallMs;
}

void testVectors() {
  cJSON *root = loadVectors();
  CHECK(root != nullptr);
  if (root == nullptr) return;
  CHECK(num(root, "rampMs") == RAMP_MS);
  const cJSON *presets = cJSON_GetObjectItemCaseSensitive(root, "presets");
  const cJSON *preset = nullptr;
  cJSON_ArrayForEach(preset, presets) checkCutLineInvariants(paramsOf(presets, preset->string));

  const cJSON *lines = cJSON_GetObjectItemCaseSensitive(root, "cutLine");
  const cJSON *line = nullptr;
  cJSON_ArrayForEach(line, lines) {
    const Params p = paramsOf(presets, line->string);
    const cJSON *point = nullptr;
    cJSON_ArrayForEach(point, line) {
      CHECK(cutLineDg(p, cJSON_GetArrayItem(point, 0)->valueint) ==
            cJSON_GetArrayItem(point, 1)->valueint);
    }
  }

  const cJSON *item = nullptr;
  cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "solver")) {
    const Params p = paramsOf(presets, cJSON_GetObjectItemCaseSensitive(item, "preset")->valuestring);
    int32_t out = -1;
    const bool found = crossingMs(p, num(item, "tkMs"), num(item, "weightDg"), num(item, "rateX100"),
                                  num(item, "tauMs"), num(item, "offsetDg"), out);
    const cJSON *expected = cJSON_GetObjectItemCaseSensitive(item, "crossingMs");
    CHECK(found == !cJSON_IsNull(expected));
    if (found) {
      CHECK(out == expected->valueint);
      CHECK(out == bruteCrossing(p, num(item, "tkMs"), num(item, "weightDg"), num(item, "rateX100"),
                                 num(item, "tauMs"), num(item, "offsetDg")));
    }
  }

  cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "verdict")) {
    const Params p = paramsOf(presets, cJSON_GetObjectItemCaseSensitive(item, "preset")->valuestring);
    const ArrivalVerdict v = arrivalVerdict(p, num(item, "cutMs"), num(item, "rateX100"));
    CHECK(static_cast<int32_t>(v.verdict) == num(item, "verdict"));
    CHECK(v.arrivalMs == num(item, "arrivalMs"));
  }

  ArrivalRecord records[32];
  size_t count = 0;
  cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "statsRecords")) {
    ArrivalRecord &r = records[count++];
    r.recordId = static_cast<uint32_t>(cJSON_GetArrayItem(item, 0)->valueint);
    r.presetId = static_cast<uint8_t>(cJSON_GetArrayItem(item, 1)->valueint);
    r.endReason = static_cast<uint8_t>(cJSON_GetArrayItem(item, 2)->valueint);
    r.verdict = static_cast<uint8_t>(cJSON_GetArrayItem(item, 3)->valueint);
    r.arrivalMs = cJSON_GetArrayItem(item, 4)->valueint;
  }
  // Order must not matter: also run over the reversed array.
  ArrivalRecord reversed[32];
  for (size_t i = 0; i < count; ++i) reversed[i] = records[count - 1 - i];
  cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "stats")) {
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(item, "expected");
    for (const ArrivalRecord *source : {records, reversed}) {
      const ArrivalStats s = arrivalStats(source, count, static_cast<uint8_t>(num(item, "presetId")),
                                          static_cast<uint32_t>(num(item, "epoch")));
      CHECK(s.n == num(e, "n"));
      CHECK(s.minMs == num(e, "minMs"));
      CHECK(s.maxMs == num(e, "maxMs"));
      CHECK(s.medianMs == num(e, "medianMs"));
      CHECK(s.sigmaMs == num(e, "sigmaMs"));
      CHECK(s.recentN == num(e, "recentN"));
      CHECK(s.recentMedianMs == num(e, "recentMedianMs"));
    }
  }

  const char *hintNames[] = {"none", "fast_edge", "slow_edge", "faster_than_window",
                             "slower_than_window"};
  cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "grindHint")) {
    const Params p = paramsOf(presets, cJSON_GetObjectItemCaseSensitive(item, "preset")->valuestring);
    ArrivalStats s;
    s.recentN = static_cast<uint8_t>(num(item, "recentN"));
    s.recentMedianMs = num(item, "recentMedianMs");
    CHECK(std::string(hintNames[static_cast<int>(grindHint(p, s))]) ==
          cJSON_GetObjectItemCaseSensitive(item, "hint")->valuestring);
  }

  cJSON_ArrayForEach(item, cJSON_GetObjectItemCaseSensitive(root, "deadline")) {
    const Params p = paramsOf(presets, cJSON_GetObjectItemCaseSensitive(item, "preset")->valuestring);
    LossInputs in;
    in.nowMs = num(item, "nowMs");
    in.lastSampleMs = num(item, "lastSampleMs");
    in.fixedMs = static_cast<uint32_t>(num(item, "fixedMs"));
    in.beforeFirstDrops = flag(item, "beforeFirstDrops");
    in.crossingValid = flag(item, "crossingValid");
    in.crossingMs = num(item, "crossingMs");
    in.history.n = static_cast<uint8_t>(num(item, "historyN"));
    in.history.medianMs = num(item, "historyMedianMs");
    in.history.sigmaMs = num(item, "historySigmaMs");
    const LossDeadline d = lossDeadline(p, in);
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(item, "expected");
    CHECK(d.deadlineMs == num(e, "deadlineMs"));
    CHECK(static_cast<int32_t>(d.source) == num(e, "source"));
    CHECK(d.cutNow == flag(e, "cutNow"));
    CHECK(d.deadlineMs >= p.delayMs && d.deadlineMs <= p.wallMs);
  }
  cJSON_Delete(root);
}

Params doubleAdaptive() {
  Params p;
  p.goalDg = 360;
  p.fastMarginDg = 40;
  p.slowMarginDg = 30;
  p.fastOn = p.slowOn = true;
  p.delayMs = 12000;
  p.fastBeforeMs = 27000;
  p.slowAfterMs = 34000;
  p.wallMs = 50000;
  return p;
}

void testStepsAndZeroLengthSegments() {
  Params p = doubleAdaptive();
  p.rampMs = 0;
  CHECK(cutLineDg(p, 26999) == 400);
  CHECK(cutLineDg(p, 27000) == 360);
  CHECK(cutLineDg(p, 33999) == 360);
  CHECK(cutLineDg(p, 34000) == 330);
  // Delay later than t1 - D clips the fast ramp start to the delay.
  p = doubleAdaptive();
  p.delayMs = 25000;
  CHECK(fastRampStartMs(p) == 25000);
  CHECK(cutLineDg(p, 25000) == 400);
  // Wall closer than t2 + D clips the slow ramp end to the wall.
  p = doubleAdaptive();
  p.wallMs = 36000;
  CHECK(slowRampEndMs(p) == 36000);
  CHECK(cutLineDg(p, 36000) == 330);
  // Sides off are the target line everywhere.
  p = doubleAdaptive();
  p.fastOn = p.slowOn = false;
  for (int32_t t = 0; t <= 50000; t += 250) CHECK(cutLineDg(p, t) == 360);
  CHECK(cutReason(doubleAdaptive(), 380) == EndReason::FAST_EXTRACTION_MAX_WEIGHT);
  CHECK(cutReason(doubleAdaptive(), 360) == EndReason::SCALE_THRESHOLD);
  CHECK(cutReason(doubleAdaptive(), 340) == EndReason::SLOW_EXTRACTION_MIN_WEIGHT);
}

void testSolverAgainstBruteForce() {
  const Params p = doubleAdaptive();
  int32_t none = 0;
  CHECK(!crossingMs(p, 20000, 200, 299, 790, 15, none));
  uint32_t seed = 12345;
  auto next = [&](uint32_t mod) {
    seed = seed * 1103515245u + 12345u;
    return static_cast<int32_t>((seed >> 8) % mod);
  };
  for (int i = 0; i < 400; ++i) {
    const int32_t tk = 12000 + next(36000), w = next(420), q = 300 + next(3800),
                  tau = 300 + next(700), r = next(50);
    int32_t out = 0;
    CHECK(crossingMs(p, tk, w, q, tau, r, out));
    CHECK(out == bruteCrossing(p, tk, w, q, tau, r));
    CHECK(out >= tk && out <= p.wallMs);
  }
}

void testEstimator() {
  // Exact on a clean 2.0 g/s ramp at 10 Hz once 4 samples span >= 2 s.
  RateEstimator e;
  for (int32_t t = 10000; t <= 11900; t += 100) e.push(t, (t - 10000) / 50);
  CHECK(!e.valid);
  e.push(12000, 40);
  CHECK(e.valid && e.rateX100 == 2000);
  for (int32_t t = 12100; t <= 20000; t += 100) e.push(t, (t - 10000) / 50);
  CHECK(e.valid && e.rateX100 == 2000);
  CHECK(e.count <= RateEstimator::CAPACITY);
  CHECK(e.atMs[e.index(e.count - 1)] > 20000 - RATE_WINDOW_MS);

  // Timestamp jitter (+-40 ms) at 1.5 g/s stays within 0.1 g/s.
  RateEstimator j;
  for (int i = 0; i <= 60; ++i) {
    const int32_t t = 10000 + i * 100 + ((i * 37) % 81) - 40;
    j.push(t, (i * 100 * 15) / 1000);
  }
  CHECK(j.valid && std::abs(j.rateX100 - 1500) <= 100);

  // A single spike is excluded; the fit is unchanged.
  RateEstimator s;
  for (int32_t t = 10000; t <= 14000; t += 100) s.push(t, (t - 10000) / 50);
  CHECK(s.push(14100, 300) == RateEstimator::Result::REJECTED);
  CHECK(s.rateX100 == 2000);
  CHECK(s.push(14200, 84) == RateEstimator::Result::ACCEPTED);
  // Three consecutive rejections relearn from the new level.
  CHECK(s.push(14300, 400) == RateEstimator::Result::REJECTED);
  CHECK(s.push(14400, 402) == RateEstimator::Result::REJECTED);
  CHECK(s.push(14500, 404) == RateEstimator::Result::ACCEPTED);
  CHECK(!s.valid && s.count == 1);
  // Duplicate or reordered timestamps are ignored.
  CHECK(s.push(14500, 404) == RateEstimator::Result::IGNORED);

  // Clamps: a falling stream reads 0, a gusher reads 40 dg/s (raw kept).
  RateEstimator down;
  for (int32_t t = 0; t <= 3000; t += 250) down.push(t, 300 - t / 100);
  CHECK(down.valid && down.rateX100 == 0 && down.rawX100 < 0);
  RateEstimator gusher;
  for (int32_t t = 0; t <= 3000; t += 100) gusher.push(t, t * 6 / 100);
  CHECK(gusher.valid && gusher.rateX100 == MAX_RATE_X100 && gusher.rawX100 == 6000);

  // int64 sums at 10 Hz over 3 s with the maximum weight stay exact.
  RateEstimator heavy;
  for (int32_t t = 0; t <= 6000; t += 100) heavy.push(t, 20000 + t / 10);
  CHECK(heavy.valid && heavy.rawX100 == 10000);

  // Reset keeps the channeling latch for the rest of the shot.
  heavy.channeling = true;
  heavy.reset();
  CHECK(!heavy.valid && heavy.count == 0 && heavy.channeling);
}

void testChanneling() {
  // 1.5 g/s at 5 Hz for 4 s, then a surge to 6 g/s: latched on the second
  // qualifying sample.
  RateEstimator e;
  int32_t w = 0;
  for (int32_t t = 0; t <= 4000; t += 200) e.push(t, w += 3);
  CHECK(!e.channeling);
  int32_t latchedAt = -1;
  for (int32_t t = 4200; t <= 8000; t += 200) {
    e.push(t, w += 12);
    if (e.channeling && latchedAt < 0) latchedAt = t;
  }
  CHECK(e.channeling);
  CHECK(latchedAt > 4100 && latchedAt <= 7000);
  // A steady fast shot (6 g/s from the start) is not channeling.
  RateEstimator steady;
  w = 0;
  for (int32_t t = 0; t <= 8000; t += 200) steady.push(t, w += 12);
  CHECK(!steady.channeling);
}

void testVerdictPacking() {
  const uint8_t packed = packVerdict(Verdict::SLOW, true, LossSource::FIXED, true);
  CHECK(verdictOf(packed) == Verdict::SLOW);
  CHECK(channelingOf(packed));
  CHECK(lossSourceOf(packed) == LossSource::FIXED);
  CHECK(blindOf(packed));
  CHECK(packVerdict(Verdict::ON_RECIPE, false, LossSource::PROJECTION, false) == 0);
  CHECK(tauMs(690, 0) == 790);
  CHECK(tauMs(300, 300) == 700);
  CHECK(leadDg(4000, 2000) == MAX_LEAD_DG);
}

void testGrindHintReplay() {
  // Recent medians >= 1 s inside both edges never fire; a drift to the fast
  // edge fires within five shots.
  const Params p = doubleAdaptive();
  ArrivalRecord records[40];
  size_t count = 0;
  for (uint32_t id = 1; id <= 20; ++id) {
    records[count++] = {id, 1, 3, 0, static_cast<int32_t>(28000 + (id * 977) % 5000)};
    CHECK(grindHint(p, arrivalStats(records, count, 1, 0)) == GrindHint::NONE);
  }
  int fired = -1;
  for (uint32_t id = 21; id <= 30; ++id) {
    records[count++] = {id, 1, 3, 0, 27300};
    if (fired < 0 && grindHint(p, arrivalStats(records, count, 1, 0)) == GrindHint::FAST_EDGE)
      fired = static_cast<int>(id - 20);
  }
  CHECK(fired > 0 && fired <= 5);
}

}  // namespace

int main() {
  testVectors();
  testStepsAndZeroLengthSegments();
  testSolverAgainstBruteForce();
  testEstimator();
  testChanneling();
  testVerdictPacking();
  testGrindHintReplay();
  if (failures != 0) {
    std::cerr << failures << " corridor kernel check(s) failed\n";
    return 1;
  }
  std::cout << "corridor kernel host test passed\n";
  return 0;
}
