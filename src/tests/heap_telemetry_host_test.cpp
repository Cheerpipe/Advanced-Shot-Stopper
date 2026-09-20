#define SHOT_STOPPER_HOST_TEST
#include "../ShotStopperPsram.h"

#include <cassert>
#include <cstdint>

using namespace shotstopper;

static HeapCapSnapshot snapshot(uint32_t freeBytes, uint32_t largest,
                                uint32_t allocatedBlocks,
                                uint32_t freeBlocks) {
  HeapCapSnapshot result;
  result.internalFree = freeBytes;
  result.internalLargest = largest;
  result.internalAllocatedBlocks = allocatedBlocks;
  result.internalFreeBlocks = freeBlocks;
  result.internalFragmentationPermille =
      heapFragmentationPermille(freeBytes, largest);
  return result;
}

int main() {
  assert(heapFragmentationPermille(0, 0) == 0);
  assert(heapFragmentationPermille(1000, 1000) == 0);
  assert(heapFragmentationPermille(1000, 250) == 750);
  assert(heapFragmentationPermille(1000, 2000) == 0);

  hostHeapCapsSnapshot() = snapshot(1000, 500, 12, 3);
  const HeapCapSnapshot injected = sampleHeapCaps();
  assert(injected.internalFreeBlocks == 3);
  assert(injected.internalFragmentationPermille == 500);

  HeapLifecycleTracker tracker;
  beginHeapLifecycle(tracker, HeapLifecycleEvent::WIFI_CONNECT,
                     snapshot(1000, 500, 12, 3));
  beginHeapLifecycle(tracker, HeapLifecycleEvent::WIFI_DISCONNECT,
                     snapshot(900, 400, 14, 5));
  assert(tracker.aggregate.staleReplacements == 1);
  assert(finishHeapLifecycle(tracker, HeapLifecycleResult::SUCCESS,
                             snapshot(800, 300, 16, 8)));
  assert(!finishHeapLifecycle(tracker, HeapLifecycleResult::FAILURE,
                              snapshot(0, 0, 0, 0)));
  assert(tracker.aggregate.cycles == 1);
  assert(tracker.aggregate.lastEvent == HeapLifecycleEvent::WIFI_DISCONNECT);
  assert(tracker.aggregate.lastDelta.freeBytes == -100);
  assert(tracker.aggregate.lastDelta.largestBlock == -100);
  assert(tracker.aggregate.lastDelta.allocatedBlocks == 2);
  assert(tracker.aggregate.lastDelta.freeBlocks == 3);
  assert(tracker.aggregate.maximumFreeBlocksIncrease == 3);

  beginHeapLifecycle(tracker, HeapLifecycleEvent::WIFI_DISCONNECT,
                     snapshot(800, 300, 16, 8));
  assert(finishHeapLifecycle(tracker, HeapLifecycleResult::FAILURE,
                             snapshot(950, 600, 11, 2)));
  assert(tracker.aggregate.cycles == 2);
  assert(tracker.aggregate.worstFreeDelta == -100);
  assert(tracker.aggregate.worstLargestDelta == -100);
  assert(tracker.aggregate.maximumFreeBlocksIncrease == 3);
}
