#define SHOT_STOPPER_HOST_TEST

#include "../ShotStopperScaleProfiler.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>

namespace {

int failures = 0;

#define CHECK(condition)                                              \
  do {                                                                \
    if (!(condition)) {                                               \
      std::cerr << __func__ << ":" << __LINE__                        \
                << ": check failed: " << #condition << "\n";          \
      ++failures;                                                     \
      return;                                                         \
    }                                                                 \
  } while (false)

using namespace shotstopper;

const uint32_t T0 = 100000;

void noteWeightAt(ScaleProfiler &profiler, uint32_t atMs, float weightG,
                  uint32_t captureSequence, uint32_t generation = 2) {
  profiler.noteWeight(weightG, atMs, generation, captureSequence, false, atMs);
}

void testLifecycleCapturesWeightsAndEvents() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  CHECK(profiler.hostState() == ScaleProfilerState::RECORDING);
  CHECK(profiler.hostRecordCount() == 1);  // PROFILE_START marker
  noteWeightAt(profiler, T0 + 102, 125.375f, 41);
  noteWeightAt(profiler, T0 + 203, 0.025f, 42);
  noteWeightAt(profiler, T0 + 203, 0.025f, 43);  // duplicate stays
  noteWeightAt(profiler, T0 + 204, -3.5f, 44);   // negative stays
  profiler.noteEvent(ScaleProfileEvent::TARE_REQUEST, T0 + 105, 2, 40, NAN, 7,
                     static_cast<uint32_t>(ScaleProfileTareOrigin::IDLE_CUP));
  CHECK(profiler.hostRecordCount() == 6);
  profiler.hostStop(ScaleProfilerStopReason::USER, T0 + 1500);
  CHECK(profiler.hostState() == ScaleProfilerState::STOPPED);
}

void testStopReasonUserIsLastRecord() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  noteWeightAt(profiler, T0 + 10, 1.0f, 1);
  profiler.hostStop(ScaleProfilerStopReason::USER, T0 + 500);
  CHECK(profiler.hostState() == ScaleProfilerState::STOPPED);
  const uint32_t count = profiler.hostRecordCount();
  const ScaleProfileRecord terminal = profiler.hostRecord(count - 1);
  CHECK(terminal.kind ==
        static_cast<uint16_t>(ScaleProfileEvent::PROFILE_STOP));
  CHECK(terminal.arg1 == static_cast<uint32_t>(ScaleProfilerStopReason::USER));
  CHECK(terminal.relativeMs == 500);
  // Late producers cannot enter the closed generation.
  noteWeightAt(profiler, T0 + 600, 9.0f, 99);
  CHECK(profiler.hostRecordCount() == count);
}

void testPreEpochAndWrapAroundTimestamps() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  noteWeightAt(profiler, T0 - 5, 1.0f, 1);  // Before the epoch: rejected.
  CHECK(profiler.hostRecordCount() == 1);
  CHECK(profiler.hostLostCount() == 0);
  ScaleProfiler wrapped;
  const uint32_t wrapEpoch = 0xFFFFFF00U;
  CHECK(wrapped.hostStartNoFlash(wrapEpoch));
  wrapped.noteWeight(2.5f, 400U, 1, 7, false, 400U);  // millis() wrapped.
  CHECK(wrapped.hostRecordCount() == 2);
  CHECK(wrapped.hostRecord(1).relativeMs == 0x290);
}

void testCapacityReservesTerminalAndCountsLoss() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  for (uint32_t i = 0; i < SCALE_PROFILE_ORDINARY_CAPACITY + 25; ++i) {
    noteWeightAt(profiler, T0 + i, 1.0f, i + 1);
  }
  CHECK(profiler.hostRecordCount() == SCALE_PROFILE_ORDINARY_CAPACITY);
  CHECK(profiler.hostLostCount() ==
        25 + 1);  // 8191 appends fit after the PROFILE_START marker.
  profiler.hostServiceIdle(T0 + 2000);
  CHECK(profiler.hostState() == ScaleProfilerState::STOPPED);
  CHECK(profiler.hostStopReason() == ScaleProfilerStopReason::BUFFER_FULL);
  CHECK(profiler.hostRecordCount() == SCALE_PROFILE_RECORD_CAPACITY);
  const ScaleProfileRecord terminal =
      profiler.hostRecord(SCALE_PROFILE_RECORD_CAPACITY - 1);
  CHECK(terminal.kind ==
        static_cast<uint16_t>(ScaleProfileEvent::PROFILE_STOP));
}

void testTimeoutStopRecordsOverrun() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  noteWeightAt(profiler, T0 + 100, 1.0f, 1);
  profiler.hostServiceIdle(T0 + SCALE_PROFILE_DURATION_LIMIT_MS + 40);
  CHECK(profiler.hostState() == ScaleProfilerState::STOPPED);
  CHECK(profiler.hostStopReason() == ScaleProfilerStopReason::TIMEOUT);
  const ScaleProfileRecord terminal =
      profiler.hostRecord(profiler.hostRecordCount() - 1);
  CHECK(terminal.kind ==
        static_cast<uint16_t>(ScaleProfileEvent::PROFILE_STOP));
  CHECK(terminal.arg2 == 40);
}

void testIndexOrdersByTimeThenOrdinal() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  noteWeightAt(profiler, T0 + 300, 3.0f, 30);
  noteWeightAt(profiler, T0 + 100, 1.0f, 10);
  profiler.noteEvent(ScaleProfileEvent::WEIGHT_ACCEPTED, T0 + 100, 2, 10, 1.0f,
                     5, 0);  // Same time as the first weight, later ordinal.
  noteWeightAt(profiler, T0 + 200, 2.0f, 20);
  profiler.hostStop(ScaleProfilerStopReason::USER, T0 + 400);
  profiler.hostServiceIdle(T0 + 500);
  CHECK(profiler.hostExportRecord(1).sequence == 10);
  CHECK(profiler.hostExportRecord(1).kind ==
        static_cast<uint16_t>(ScaleProfileEvent::WEIGHT));
  CHECK(profiler.hostExportRecord(2).kind ==
        static_cast<uint16_t>(ScaleProfileEvent::WEIGHT_ACCEPTED));
  CHECK(profiler.hostExportRecord(2).ordinal ==
        profiler.hostExportRecord(1).ordinal + 1);
  CHECK(profiler.hostExportRecord(3).sequence == 20);
  CHECK(profiler.hostExportRecord(4).sequence == 30);
}

void testTxtRowShapeAndPrecision() {
  ScaleProfileRecord weight = {};
  weight.relativeMs = 102;
  weight.connectionGeneration = 2;
  weight.sequence = 41;
  weight.weightG = 125.375f;
  weight.kind = static_cast<uint16_t>(ScaleProfileEvent::WEIGHT);
  char line[160];
  CHECK(formatScaleProfileRow(line, sizeof(line), weight) > 0);
  CHECK(std::string(line) ==
        "+000000.102 \xE2\x80\x93 125.375|WEIGHT seq=41 connection=2");

  ScaleProfileRecord weightDisc = weight;
  weightDisc.flags = SCALE_PROFILE_FLAG_DISCONTINUITY;
  CHECK(formatScaleProfileRow(line, sizeof(line), weightDisc) > 0);
  CHECK(std::string(line).find("disc=1") != std::string::npos);

  ScaleProfileRecord event = {};
  event.relativeMs = 105;
  event.kind = static_cast<uint16_t>(ScaleProfileEvent::TARE_REQUEST);
  event.arg1 = 7;
  event.arg2 = static_cast<uint32_t>(ScaleProfileTareOrigin::IDLE_CUP);
  event.sequence = 40;
  CHECK(formatScaleProfileRow(line, sizeof(line), event) > 0);
  CHECK(std::string(line) ==
        "+000000.105 \xE2\x80\x93 \xE2\x80\x94|TARE_REQUEST request=7 origin=2 "
        "seq=40");

  // Nine significant digits round-trip the captured float32.
  ScaleProfileRecord precise = {};
  precise.relativeMs = 1;
  precise.weightG = 0.123456791f;
  precise.kind = static_cast<uint16_t>(ScaleProfileEvent::WEIGHT);
  CHECK(formatScaleProfileRow(line, sizeof(line), precise) > 0);
  float roundTrip = 0.0f;
  const char *field = std::strchr(line, '|');
  CHECK(field != nullptr);
  CHECK(std::sscanf(line, "+000000.001 \xE2\x80\x93 %g|", &roundTrip) == 1);
  CHECK(roundTrip == precise.weightG);
}

void testDownloadLeasePinsGeneration() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  noteWeightAt(profiler, T0 + 10, 1.0f, 1);
  profiler.hostStop(ScaleProfilerStopReason::USER, T0 + 100);
  CHECK(profiler.acquireDownloadLease());
  CHECK(profiler.downloadLeaseGeneration() != 0);
  CHECK(profiler.downloadFromRam());
  CHECK(profiler.downloadRecordCount() == 3);
  // Start and Delete refuse while the lease pins the frozen trace.
  (void)profiler.hostService(ScaleProfilerRequest::START, T0 + 200);
  CHECK(profiler.hostState() == ScaleProfilerState::STOPPED);
  CHECK(profiler.hostLastError() == ScaleProfilerError::BUSY);
  (void)profiler.hostService(ScaleProfilerRequest::DELETE, T0 + 300);
  CHECK(profiler.hostState() == ScaleProfilerState::STOPPED);
  profiler.releaseDownloadLease();
  // Let the dispatched save finish; the trace becomes flash-backed.
  profiler.hostServiceIdle(T0 + 400);
  profiler.hostPublishFlashResult(true);
  profiler.hostServiceIdle(T0 + 500);
  CHECK(profiler.hostState() == ScaleProfilerState::SAVED);
  CHECK(profiler.acquireDownloadLease());
  (void)profiler.hostService(ScaleProfilerRequest::DELETE, T0 + 600);
  CHECK(profiler.hostState() == ScaleProfilerState::SAVED);
  profiler.releaseDownloadLease();
  (void)profiler.hostService(ScaleProfilerRequest::DELETE, T0 + 700);
  CHECK(profiler.hostState() == ScaleProfilerState::EMPTY);
}

void testSaveResultTransitionsAndFailureRetry() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  noteWeightAt(profiler, T0 + 10, 1.0f, 1);
  profiler.hostStop(ScaleProfilerStopReason::USER, T0 + 100);
  profiler.hostServiceIdle(T0 + 200);
  // Persisted ack: the trace becomes SAVED and RAM is released.
  profiler.hostPublishFlashResult(true);
  profiler.hostServiceIdle(T0 + 300);
  CHECK(profiler.hostState() == ScaleProfilerState::SAVED);
  CHECK(profiler.hostPersistence() == ScaleProfilerPersistence::SAVED);
  CHECK(!profiler.downloadFromRam());
  CHECK(profiler.downloadRecordCount() == 3);

  ScaleProfiler failing;
  CHECK(failing.hostStartNoFlash(T0));
  failing.hostStop(ScaleProfilerStopReason::USER, T0 + 100);
  failing.hostServiceIdle(T0 + 200);
  failing.hostPublishFlashResult(false);
  failing.hostServiceIdle(T0 + 300);
  CHECK(failing.hostPersistence() == ScaleProfilerPersistence::FAILED);
  CHECK(failing.hostLastError() == ScaleProfilerError::SAVE_FAILED);
  // A stopped, failed trace stays downloadable from RAM.
  CHECK(failing.acquireDownloadLease());
  CHECK(failing.downloadFromRam());
  failing.releaseDownloadLease();
}

void testStartRetryNeverRestartsActiveSession() {
  ScaleProfiler profiler;
  CHECK(profiler.hostStartNoFlash(T0));
  noteWeightAt(profiler, T0 + 10, 1.0f, 1);
  (void)profiler.hostService(ScaleProfilerRequest::START, T0 + 20);
  CHECK(profiler.hostState() == ScaleProfilerState::RECORDING);
  CHECK(profiler.hostRecordCount() == 2);  // No second PROFILE_START marker.
  CHECK(profiler.hostHeader().sessionId != 0);
  const uint32_t firstSession = profiler.hostHeader().sessionId;
  profiler.hostStop(ScaleProfilerStopReason::USER, T0 + 100);
  profiler.hostServiceIdle(T0 + 200);
  profiler.hostPublishFlashResult(true);
  profiler.hostServiceIdle(T0 + 300);
  // A new Start replaces even an unsaved session once it can proceed.
  CHECK(profiler.hostStartNoFlash(T0 + 1000));
  CHECK(profiler.hostState() == ScaleProfilerState::RECORDING);
  CHECK(profiler.hostHeader().sessionId == firstSession + 1);
}

void testHeaderContextProviderRunsAtStart() {
  static uint32_t calls = 0;
  calls = 0;
  const auto provider = +[](char *text, size_t capacity) -> uint32_t {
    ++calls;
    snprintf(text, capacity, "firmware=test\nscale=host\n");
    return 1760000000U;
  };
  ScaleProfiler profiler;
  profiler.hostSetContextProvider(provider);
  CHECK(profiler.hostStartNoFlash(T0));
  CHECK(calls == 1);
  CHECK(profiler.hostHeader().startWallUtcSec == 1760000000U);
  CHECK(std::string(profiler.hostHeader().context) == "firmware=test\nscale=host\n");
}

void testBudgetMatchesPlan() {
  CHECK(SCALE_PROFILE_RECORD_CAPACITY * SCALE_PROFILE_RECORD_BYTES ==
        256U * 1024U);
  CHECK(SCALE_PROFILE_PARTITION_BYTES == 0x41000);
  CHECK(SCALE_PROFILE_PARTITION_BYTES - SCALE_PROFILE_PAYLOAD_OFFSET ==
        SCALE_PROFILE_RECORD_CAPACITY * SCALE_PROFILE_RECORD_BYTES);
  CHECK(sizeof(ScaleProfileHeader) <= 4096);
}

void runAll() {
  testLifecycleCapturesWeightsAndEvents();
  testStopReasonUserIsLastRecord();
  testPreEpochAndWrapAroundTimestamps();
  testCapacityReservesTerminalAndCountsLoss();
  testTimeoutStopRecordsOverrun();
  testIndexOrdersByTimeThenOrdinal();
  testTxtRowShapeAndPrecision();
  testDownloadLeasePinsGeneration();
  testSaveResultTransitionsAndFailureRetry();
  testStartRetryNeverRestartsActiveSession();
  testHeaderContextProviderRunsAtStart();
  testBudgetMatchesPlan();
}

}  // namespace

int main() {
  runAll();
  if (failures != 0) {
    std::cerr << failures << " scale profiler check(s) failed\n";
    return 1;
  }
  std::cout << "scale profiler host tests passed\n";
  return 0;
}
