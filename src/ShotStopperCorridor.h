#pragma once

#include <stddef.h>
#include <stdint.h>

#include "ShotStopperBrewTypes.h"

// =============================================================================
// Smooth cut ("corridor") kernel — pure integer math, no globals, no heap.
// =============================================================================
// Units: weights in decigrams (dg), times in milliseconds measured from the
// machine running start, flow rates in dg/s x 100. Every time is a stop-COMMAND
// time; machine stop latency lives only inside tau. The control task owns all
// state built from these functions.

namespace shotstopper {
namespace corridor {

// Ramp length D: the ceiling/floor ramps to the target over D before t1 and
// after t2. D = 0 is exactly the legacy Fast/Slow guard step shape.
constexpr int32_t RAMP_MS = 5000;
static_assert(RAMP_MS <= 20000, "D bound keeps MR*D^3 inside int64 and 2^53");
// Bisection over [t_k, wall] in integer ms needs at most 16 halvings.
static_assert(HARD_MAX_CIRCUIT_CLOSED_MS < 65536, "solver iteration bound");
constexpr int32_t STOP_LATENCY_MS = 100;
constexpr int32_t RATE_WINDOW_MS = 3000;
constexpr uint8_t RATE_MIN_SAMPLES = 4;
constexpr int32_t RATE_MIN_SPAN_MS = 2000;
constexpr int32_t MAX_RATE_X100 = 4000;            // 40 dg/s
constexpr int32_t MIN_PREDICTION_RATE_X100 = 300;  // 3 dg/s
constexpr int32_t MAX_LEAD_DG = 40;
constexpr int32_t MAD_FLOOR_DG = 3;
constexpr uint8_t MAD_MIN_INNOVATIONS = 4;
constexpr uint8_t MAX_CONSECUTIVE_REJECTIONS = 2;
constexpr int32_t VERDICT_DEAD_BAND_MS = 500;
constexpr int32_t ARRIVAL_CLAMP_MS = 5000;
// Replay-tuned (W3.1): 30 dg/s catches late surges a 50 dg/s floor misses.
constexpr int32_t CHANNELING_RATE_X100 = 3000;
constexpr int32_t CHANNELING_BASE_RATE_X100 = 500;
constexpr int32_t CHANNELING_LOOKBACK_MS = 1000;
constexpr uint8_t CHANNELING_CONFIRM_SAMPLES = 2;
constexpr uint8_t STATS_MAX_SHOTS = 10;
constexpr int32_t STATS_SIGMA_FLOOR_MS = 750;
constexpr uint8_t SCALE_LOSS_MIN_SHOTS = 3;
constexpr uint8_t SHOT_STATS_MIN_SHOTS = 3;
constexpr uint8_t GRIND_HINT_RECENT_SHOTS = 5;
constexpr int32_t GRIND_HINT_EDGE_MS = 1000;
constexpr int32_t PROJECTION_HORIZON_MS = 3000;
constexpr int32_t HISTORY_MARGIN_MS = 500;

struct Params {
  int32_t goalDg = 0;
  int32_t fastMarginDg = 0;
  int32_t slowMarginDg = 0;
  bool fastOn = false;
  bool slowOn = false;
  int32_t delayMs = 0;
  int32_t fastBeforeMs = 0;  // t1
  int32_t slowAfterMs = 0;   // t2
  int32_t wallMs = 0;
  int32_t rampMs = RAMP_MS;  // D
};

inline int32_t ceilingDg(const Params &p) {
  return p.goalDg + (p.fastOn ? p.fastMarginDg : 0);
}
inline int32_t floorDg(const Params &p) {
  return p.goalDg - (p.slowOn ? p.slowMarginDg : 0);
}
inline int32_t fastRampStartMs(const Params &p) {
  const int32_t a = p.fastBeforeMs - p.rampMs > p.delayMs ? p.fastBeforeMs - p.rampMs
                                                          : p.delayMs;
  return a < p.fastBeforeMs ? a : p.fastBeforeMs;
}
inline int32_t slowRampEndMs(const Params &p) {
  const int32_t b = p.slowAfterMs + p.rampMs < p.wallMs ? p.slowAfterMs + p.rampMs
                                                        : p.wallMs;
  return b > p.slowAfterMs ? b : p.slowAfterMs;
}

// f(t): ceiling held to a, smoothstep down to the target at t1, straight
// target through [t1, t2], smoothstep down to the floor at b, floor held.
// Ramps truncate toward the target; zero-length ramps are legacy steps.
inline int32_t cutLineDg(const Params &p, int32_t t) {
  const int32_t a = fastRampStartMs(p);
  if (t < p.fastBeforeMs) {
    if (t < a) return ceilingDg(p);
    const int64_t d = p.fastBeforeMs - a, n = t - a;
    const int64_t margin = ceilingDg(p) - p.goalDg;
    return p.goalDg + static_cast<int32_t>(
        margin * (d * d * d - 3 * n * n * d + 2 * n * n * n) / (d * d * d));
  }
  if (t < p.slowAfterMs || (t == p.slowAfterMs && p.rampMs > 0)) return p.goalDg;
  const int32_t b = slowRampEndMs(p);
  if (t >= b) return floorDg(p);
  const int64_t d = b - p.slowAfterMs, n = t - p.slowAfterMs;
  const int64_t margin = p.goalDg - floorDg(p);
  return p.goalDg - static_cast<int32_t>(
      margin * (3 * n * n * d - 2 * n * n * n) / (d * d * d));
}

// EndReason of a weight cut = the segment of f that fired.
inline EndReason cutReason(const Params &p, int32_t lineDg) {
  if (lineDg > p.goalDg) return EndReason::FAST_EXTRACTION_MAX_WEIGHT;
  if (lineDg < p.goalDg) return EndReason::SLOW_EXTRACTION_MIN_WEIGHT;
  return EndReason::SCALE_THRESHOLD;
}

inline bool weightCutReason(uint8_t reason) {
  return reason == static_cast<uint8_t>(EndReason::SCALE_THRESHOLD) ||
         reason == static_cast<uint8_t>(EndReason::FAST_EXTRACTION_MAX_WEIGHT) ||
         reason == static_cast<uint8_t>(EndReason::SLOW_EXTRACTION_MIN_WEIGHT);
}

// tau = protocol sensor lag + valve stop latency + momentary stop pulse.
inline int32_t tauMs(uint16_t sensorLagMs, uint32_t momentaryStopPulseMs) {
  return static_cast<int32_t>(sensorLagMs) + STOP_LATENCY_MS +
         static_cast<int32_t>(momentaryStopPulseMs);
}

inline int32_t leadDg(int32_t rateX100, int32_t tau) {
  const int64_t lead = static_cast<int64_t>(rateX100) * tau / 100000;
  return lead > MAX_LEAD_DG ? MAX_LEAD_DG : static_cast<int32_t>(lead);
}

// Small in-place sort for <= 32 values (no heap).
inline void sortSmall(int32_t *values, uint8_t count) {
  for (uint8_t i = 1; i < count; ++i) {
    const int32_t v = values[i];
    uint8_t j = i;
    for (; j > 0 && values[j - 1] > v; --j) values[j] = values[j - 1];
    values[j] = v;
  }
}
inline int32_t medianOfSorted(const int32_t *sorted, uint8_t count) {
  if (count == 0) return 0;
  return count % 2 ? sorted[count / 2]
                   : (sorted[count / 2 - 1] + sorted[count / 2]) / 2;
}
inline int32_t madOf(const int32_t *values, uint8_t count, int32_t median) {
  int32_t deviations[32];
  for (uint8_t i = 0; i < count; ++i) {
    deviations[i] = values[i] > median ? values[i] - median : median - values[i];
  }
  sortSmall(deviations, count);
  return medianOfSorted(deviations, count);
}

// Time-window least-squares flow estimator with a MAD innovation gate.
struct RateEstimator {
  static constexpr uint8_t CAPACITY = 32;
  static constexpr int32_t NO_RATE = INT32_MIN;
  enum class Result : uint8_t { ACCEPTED, REJECTED, IGNORED };

  int32_t atMs[CAPACITY] = {};
  int32_t rawRateX100[CAPACITY] = {};
  int16_t weightDg[CAPACITY] = {};
  int16_t innovationDg[CAPACITY] = {};
  uint8_t head = 0;  // index of the newest sample
  uint8_t count = 0;
  uint8_t rejectedRun = 0;
  uint8_t channelingRun = 0;
  bool valid = false;
  bool channeling = false;  // latched for the rest of the shot
  int32_t rateX100 = 0;     // clamped q-hat
  int32_t rawX100 = 0;      // unclamped q-hat

  void reset() {
    const bool latched = channeling;
    *this = RateEstimator{};
    channeling = latched;
  }

  uint8_t index(uint8_t age) const {
    return static_cast<uint8_t>((head + CAPACITY - age) % CAPACITY);
  }

  Result push(int32_t t, int32_t weight) {
    if (count > 0 && t <= atMs[head]) return Result::IGNORED;
    if (weight > INT16_MAX) weight = INT16_MAX;
    if (weight < INT16_MIN) weight = INT16_MIN;
    while (count > 0 && atMs[index(count - 1)] <= t - RATE_WINDOW_MS) --count;
    int32_t innovation = 0;
    if (count > 0) {
      const int32_t predicted = weightDg[head] +
          (valid ? static_cast<int32_t>(static_cast<int64_t>(rateX100) *
                                        (t - atMs[head]) / 100000)
                 : 0);
      innovation = weight - predicted;
    }
    if (count >= MAD_MIN_INNOVATIONS) {
      int32_t innovations[CAPACITY];
      uint8_t n = 0;
      for (uint8_t age = 0; age + 1 < count; ++age)
        innovations[n++] = innovationDg[index(age)];
      sortSmall(innovations, n);
      const int32_t median = medianOfSorted(innovations, n);
      const int32_t mad = madOf(innovations, n, median);
      const int64_t deviation = innovation > median ? innovation - median
                                                    : median - innovation;
      // 4.5 * max(1.4826 * MAD, 3 dg), in 1e-4 dg.
      const int64_t scaled = static_cast<int64_t>(mad) * 66717;
      const int64_t limit = scaled > 45000 * MAD_FLOOR_DG ? scaled : 45000 * MAD_FLOOR_DG;
      if (deviation * 10000 > limit) {
        if (++rejectedRun <= MAX_CONSECUTIVE_REJECTIONS) return Result::REJECTED;
        // The stream moved for good: relearn from this sample.
        reset();
        innovation = 0;
      }
    }
    rejectedRun = 0;
    head = static_cast<uint8_t>((head + 1) % CAPACITY);
    if (count < CAPACITY) ++count;
    atMs[head] = t;
    weightDg[head] = static_cast<int16_t>(weight);
    innovationDg[head] = static_cast<int16_t>(
        innovation > INT16_MAX ? INT16_MAX : innovation < INT16_MIN ? INT16_MIN : innovation);
    fit();
    rawRateX100[head] = valid ? rawX100 : NO_RATE;
    updateChanneling();
    return Result::ACCEPTED;
  }

  void fit() {
    valid = false;
    if (count < RATE_MIN_SAMPLES ||
        atMs[head] - atMs[index(count - 1)] < RATE_MIN_SPAN_MS) return;
    int64_t sumT = 0, sumW = 0;
    for (uint8_t age = 0; age < count; ++age) {
      sumT += atMs[index(age)] - atMs[head];
      sumW += weightDg[index(age)];
    }
    int64_t sxx = 0, sxy = 0;
    for (uint8_t age = 0; age < count; ++age) {
      const int64_t dt = (atMs[index(age)] - atMs[head]) * count - sumT;
      const int64_t dw = static_cast<int64_t>(weightDg[index(age)]) * count - sumW;
      sxx += dt * dt;
      sxy += dt * dw;
    }
    if (sxx <= 0) return;
    const int64_t raw = sxy * 100000 / sxx;
    const int64_t bounded = raw > INT32_MAX / 2 ? INT32_MAX / 2 : raw;
    rawX100 = static_cast<int32_t>(bounded < -INT32_MAX / 2 ? -INT32_MAX / 2 : bounded);
    rateX100 = rawX100 < 0 ? 0 : rawX100;
    if (rateX100 > MAX_RATE_X100) rateX100 = MAX_RATE_X100;
    valid = true;
  }

  // Predicate on consecutive accepted samples: q_raw >= 30 dg/s and >= 1.5x
  // the q_raw at least 1 s earlier, which itself was >= 5 dg/s.
  void updateChanneling() {
    bool surge = false;
    if (valid && rawX100 >= CHANNELING_RATE_X100) {
      for (uint8_t age = 1; age < count; ++age) {
        const uint8_t i = index(age);
        if (atMs[i] > atMs[head] - CHANNELING_LOOKBACK_MS) continue;
        surge = rawRateX100[i] != NO_RATE && rawRateX100[i] >= CHANNELING_BASE_RATE_X100 &&
                static_cast<int64_t>(rawX100) * 2 >= static_cast<int64_t>(rawRateX100[i]) * 3;
        break;
      }
    }
    channelingRun = surge ? static_cast<uint8_t>(channelingRun + 1) : 0;
    if (channelingRun >= CHANNELING_CONFIRM_SAMPLES) channeling = true;
  }
};

// Per-shot kernel state owned by the control task.
struct CorridorState {
  Params params;
  RateEstimator rate;
  int32_t tauMs = 0;           // sensor lag + stop latency (+ stop pulse)
  int32_t offsetDg = 0;        // learned drip offset r
};
static_assert(sizeof(CorridorState) <= 512, "corridor state RAM budget");

// Earliest t >= t_k where W_k + q(t - t_k) + q*tau + r >= f(t). Returns false
// ("none") when q < 3 dg/s; returns the wall when the line is never met.
inline bool crossingMs(const Params &p, int32_t tk, int32_t weightDgAtK,
                       int32_t rateX100, int32_t tau, int32_t offsetDg,
                       int32_t &out) {
  if (rateX100 < MIN_PREDICTION_RATE_X100) return false;
  const int32_t lead = leadDg(rateX100, tau);
  auto met = [&](int32_t t) {
    const int64_t ahead = static_cast<int64_t>(rateX100) * (t - tk) / 100000;
    return weightDgAtK + ahead + lead + offsetDg >= cutLineDg(p, t);
  };
  if (tk >= p.wallMs || met(tk)) {
    out = tk;
    return true;
  }
  if (!met(p.wallMs)) {
    out = p.wallMs;
    return true;
  }
  int32_t lo = tk, hi = p.wallMs;
  while (hi - lo > 1) {
    const int32_t mid = lo + (hi - lo) / 2;
    (met(mid) ? hi : lo) = mid;
  }
  out = hi;
  return true;
}

// Backstop direct-law threshold (no prediction lead).
inline int32_t backstopThresholdDg(const Params &p, int32_t t, int32_t offsetDg) {
  return cutLineDg(p, t) - offsetDg;
}

enum class Verdict : uint8_t { ON_RECIPE = 0, FAST = 1, SLOW = 2, UNCLASSIFIED = 3 };

struct ArrivalVerdict {
  Verdict verdict = Verdict::UNCLASSIFIED;
  int32_t arrivalMs = 0;
};

// t_arr = t_cut + (target - f(t_cut)) / q_cut, clamped to +-5 s.
inline ArrivalVerdict arrivalVerdict(const Params &p, int32_t cutMs, int32_t rateX100) {
  ArrivalVerdict out;
  const int32_t line = cutLineDg(p, cutMs);
  if (line == p.goalDg) {
    out.arrivalMs = cutMs;
  } else {
    if (rateX100 < MIN_PREDICTION_RATE_X100) return out;
    int64_t shift = static_cast<int64_t>(p.goalDg - line) * 100000 / rateX100;
    if (shift > ARRIVAL_CLAMP_MS) shift = ARRIVAL_CLAMP_MS;
    if (shift < -ARRIVAL_CLAMP_MS) shift = -ARRIVAL_CLAMP_MS;
    out.arrivalMs = cutMs + static_cast<int32_t>(shift);
  }
  out.verdict = out.arrivalMs < p.fastBeforeMs - VERDICT_DEAD_BAND_MS ? Verdict::FAST
              : out.arrivalMs > p.slowAfterMs + VERDICT_DEAD_BAND_MS ? Verdict::SLOW
                                                                     : Verdict::ON_RECIPE;
  return out;
}

// Persisted verdict byte: bits 0-1 verdict, bit 2 channeling, bits 3-4
// scale-loss source, bit 5 blind (scale-loss shot without a valid weight).
enum class LossSource : uint8_t { PROJECTION = 0, HISTORY = 1, WINDOW = 2, FIXED = 3 };
constexpr uint8_t VERDICT_CHANNELING_BIT = 0x04;
constexpr uint8_t VERDICT_BLIND_BIT = 0x20;
inline uint8_t packVerdict(Verdict verdict, bool channeling, LossSource source, bool blind) {
  return static_cast<uint8_t>(static_cast<uint8_t>(verdict) |
                              (channeling ? VERDICT_CHANNELING_BIT : 0) |
                              (static_cast<uint8_t>(source) << 3) |
                              (blind ? VERDICT_BLIND_BIT : 0));
}
inline Verdict verdictOf(uint8_t packed) { return static_cast<Verdict>(packed & 3); }
inline bool channelingOf(uint8_t packed) { return (packed & VERDICT_CHANNELING_BIT) != 0; }
inline LossSource lossSourceOf(uint8_t packed) {
  return static_cast<LossSource>((packed >> 3) & 3);
}
inline bool blindOf(uint8_t packed) { return (packed & VERDICT_BLIND_BIT) != 0; }

// One shot-log row as the statistics see it.
struct ArrivalRecord {
  uint32_t recordId = 0;
  uint8_t presetId = 0;
  uint8_t endReason = 0;
  uint8_t verdict = 0;  // packed
  int32_t arrivalMs = 0;
};

struct ArrivalStats {
  uint8_t n = 0;
  int32_t minMs = 0;
  int32_t maxMs = 0;
  int32_t medianMs = 0;
  int32_t sigmaMs = 0;
  uint8_t recentN = 0;
  int32_t recentMedianMs = 0;
};

inline bool admissibleArrival(const ArrivalRecord &r, uint8_t presetId, uint32_t epoch) {
  return r.presetId == presetId && r.recordId >= epoch && weightCutReason(r.endReason) &&
         verdictOf(r.verdict) != Verdict::UNCLASSIFIED && !channelingOf(r.verdict) &&
         !blindOf(r.verdict) && r.arrivalMs > 0;
}

// Statistics over the newest <= 10 admissible arrivals of one preset.
inline ArrivalStats arrivalStats(const ArrivalRecord *records, size_t count,
                                 uint8_t presetId, uint32_t epoch) {
  uint32_t ids[STATS_MAX_SHOTS] = {};
  int32_t times[STATS_MAX_SHOTS] = {};
  uint8_t n = 0;
  for (size_t i = 0; i < count; ++i) {
    const ArrivalRecord &r = records[i];
    if (!admissibleArrival(r, presetId, epoch)) continue;
    uint8_t slot = n;
    if (n == STATS_MAX_SHOTS) {
      slot = 0;  // replace the oldest kept if this one is newer
      for (uint8_t j = 1; j < n; ++j) if (ids[j] < ids[slot]) slot = j;
      if (ids[slot] >= r.recordId) continue;
    } else {
      ++n;
    }
    ids[slot] = r.recordId;
    times[slot] = r.arrivalMs;
  }
  ArrivalStats out;
  out.n = n;
  if (n == 0) return out;
  int32_t sorted[STATS_MAX_SHOTS];
  for (uint8_t i = 0; i < n; ++i) sorted[i] = times[i];
  sortSmall(sorted, n);
  out.minMs = sorted[0];
  out.maxMs = sorted[n - 1];
  out.medianMs = medianOfSorted(sorted, n);
  const int32_t sigma = static_cast<int32_t>(
      static_cast<int64_t>(madOf(sorted, n, out.medianMs)) * 14826 / 10000);
  out.sigmaMs = sigma > STATS_SIGMA_FLOOR_MS ? sigma : STATS_SIGMA_FLOOR_MS;
  // Recent median: the newest GRIND_HINT_RECENT_SHOTS by record id.
  int32_t recent[GRIND_HINT_RECENT_SHOTS];
  bool taken[STATS_MAX_SHOTS] = {};
  uint8_t r = 0;
  for (; r < GRIND_HINT_RECENT_SHOTS && r < n; ++r) {
    uint8_t best = 0;
    while (best + 1 < n && taken[best]) ++best;
    for (uint8_t j = best + 1; j < n; ++j)
      if (!taken[j] && ids[j] > ids[best]) best = j;
    taken[best] = true;
    recent[r] = times[best];
  }
  sortSmall(recent, r);
  out.recentN = r;
  out.recentMedianMs = medianOfSorted(recent, r);
  return out;
}

enum class GrindHint : uint8_t { NONE, FAST_EDGE, SLOW_EDGE, FASTER_THAN_WINDOW, SLOWER_THAN_WINDOW };

// Compares the median of the last 5 arrivals with the window edges.
inline GrindHint grindHint(const Params &p, const ArrivalStats &stats) {
  if (stats.recentN < GRIND_HINT_RECENT_SHOTS) return GrindHint::NONE;
  const int32_t recent = stats.recentMedianMs;
  if (recent < p.fastBeforeMs) return GrindHint::FASTER_THAN_WINDOW;
  if (recent > p.slowAfterMs) return GrindHint::SLOWER_THAN_WINDOW;
  if (recent < p.fastBeforeMs + GRIND_HINT_EDGE_MS) return GrindHint::FAST_EDGE;
  if (recent > p.slowAfterMs - GRIND_HINT_EDGE_MS) return GrindHint::SLOW_EDGE;
  return GrindHint::NONE;
}

struct LossInputs {
  int32_t nowMs = 0;
  int32_t lastSampleMs = 0;  // t_p
  uint32_t fixedMs = 0;      // 0 = none
  bool beforeFirstDrops = false;
  bool crossingValid = false;
  int32_t crossingMs = 0;    // t* from the last accepted sample
  ArrivalStats history;
};

struct LossDeadline {
  int32_t deadlineMs = 0;
  LossSource source = LossSource::WINDOW;
  bool cutNow = false;
};

// Scale-loss deadline, first applicable: fixed time, R0 cut now, R1 projection,
// R2 history, R3 window midpoint; clamped to [max(t_p, delay), wall].
inline LossDeadline lossDeadline(const Params &p, const LossInputs &in) {
  LossDeadline out;
  const bool project = !in.beforeFirstDrops && in.crossingValid;
  if (in.fixedMs != 0) {
    out.source = LossSource::FIXED;
    out.deadlineMs = static_cast<int32_t>(in.fixedMs);
  } else if (project && in.crossingMs <= in.nowMs) {
    out.source = LossSource::PROJECTION;
    out.deadlineMs = in.nowMs;
  } else if (project && in.crossingMs - in.lastSampleMs <= PROJECTION_HORIZON_MS) {
    out.source = LossSource::PROJECTION;
    out.deadlineMs = in.crossingMs;
  } else if (in.history.n >= SCALE_LOSS_MIN_SHOTS) {
    out.source = LossSource::HISTORY;
    const int32_t fromHistory =
        in.history.medianMs + 2 * in.history.sigmaMs / 3 + HISTORY_MARGIN_MS;
    out.deadlineMs = fromHistory < p.slowAfterMs ? fromHistory : p.slowAfterMs;
  } else {
    out.source = LossSource::WINDOW;
    out.deadlineMs = (p.fastBeforeMs + p.slowAfterMs) / 2;
  }
  const int32_t floor = in.lastSampleMs > p.delayMs ? in.lastSampleMs : p.delayMs;
  if (out.deadlineMs < floor) out.deadlineMs = floor;
  if (out.deadlineMs > p.wallMs) out.deadlineMs = p.wallMs;
  out.cutNow = out.deadlineMs <= in.nowMs;
  return out;
}

}  // namespace corridor

// Timed-law sentinel (s): past every wall, so only the direct backstop can cut.
constexpr float NO_CUT_PREDICTION_S = HARD_MAX_CIRCUIT_CLOSED_MS / 1000.0f + 1.0f;

}  // namespace shotstopper
