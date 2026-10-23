#include "OpenBrewByWeightAuditStore.h"

// Durable audit store: target builds talk to the auditlog data partition;
// the dedicated host test compiles this file with a RAM-backed partition
// fake (tests/audit_store_host_stubs.h) and exercises the same bodies,
// including power-cut recovery. Plain host control builds compile this file
// out and link the contract stubs from tests/open_brew_by_weight_host_stubs.h.
#if !defined(OPEN_BREW_BY_WEIGHT_HOST_TEST) || defined(OPEN_BREW_BY_WEIGHT_AUDIT_HOST_TEST)
#ifndef OPEN_BREW_BY_WEIGHT_AUDIT_HOST_TEST
#include "OpenBrewByWeightFlashIoScratch.h"
#include <esp_partition.h>
#else
#include "tests/audit_store_host_stubs.h"
#endif
#include <string.h>

namespace openbrewbyweight {
namespace {

AuditStoreStatus g_status;

constexpr size_t kMaxScanRuns = 8;
constexpr size_t kRecordsPerSector =
    AUDIT_STORE_SECTOR_BYTES / AUDIT_STORE_RECORD_BYTES;

const esp_partition_t *storePartition() {
  const esp_partition_t *part = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(0x40),
      "auditlog");
  return part && part->size == AUDIT_STORE_PARTITION_BYTES ? part : nullptr;
}

size_t slotOffset(uint32_t absoluteIndex) {
  return (absoluteIndex % AUDIT_STORE_RECORD_CAPACITY) *
         AUDIT_STORE_RECORD_BYTES;
}

bool readSlot(const esp_partition_t *part, uint32_t index, uint8_t *slot) {
  return esp_partition_read(part, slotOffset(index), slot,
                            AUDIT_STORE_RECORD_BYTES) == ESP_OK;
}

struct ScanRun {
  uint32_t start, end;  // slot indexes, half-open
};

// Boot scan: valid runs are separated by erased (wrap-ahead) or torn slots. The active window is the
// run an append would extend: the last run in slot order, merged across the
// wrap when a run reaches the body end and another starts at slot 0.
bool scanWindow(const esp_partition_t *part) {
  ScanRun runs[kMaxScanRuns];
  size_t runCount = 0;
  bool inRun = false;
  AuditRecord record;
  uint8_t slot[AUDIT_STORE_RECORD_BYTES];
  for (uint32_t index = 0; index < AUDIT_STORE_RECORD_CAPACITY; ++index) {
    if (!readSlot(part, index, slot)) return false;
    if (!auditStoreDecodeRecord(slot, record)) {
      inRun = false;
      continue;
    }
    if (!inRun) {
      inRun = true;
      if (runCount == kMaxScanRuns) return false;
      runs[runCount++] = {index, index + 1};
    } else {
      runs[runCount - 1].end = index + 1;
    }
  }
  if (runCount == 0) {
    g_status = AuditStoreStatus{};
    g_status.state = AuditStoreState::READY;
    return true;
  }
  const ScanRun &first = runs[0];
  const ScanRun &last = runs[runCount - 1];
  if (runCount > 1 && last.end == AUDIT_STORE_RECORD_CAPACITY &&
      first.start == 0) {
    g_status.windowStart = last.start;
    g_status.writeCursor = AUDIT_STORE_RECORD_CAPACITY + first.end;
  } else {
    g_status.windowStart = last.start;
    g_status.writeCursor = last.end;
  }
  g_status.seqHigh = 0;
  uint32_t drop = 0;
  for (uint32_t index = g_status.windowStart; index < g_status.writeCursor;
       ++index) {
    if (!readSlot(part, index, slot) || !auditStoreDecodeRecord(slot, record)) {
      return false;
    }
    if (drop == 0 && record.sequence < g_status.seqHigh) drop = index;
    if (record.sequence > g_status.seqHigh) g_status.seqHigh = record.sequence;
  }
  // A full ring (cursor on a not-yet-erased sector) is one run starting at
  // slot 0: the oldest record sits where the sequence drops, not at slot 0.
  if (drop != 0 && g_status.writeCursor - g_status.windowStart ==
                       AUDIT_STORE_RECORD_CAPACITY) {
    g_status.windowStart = drop;
    g_status.writeCursor = AUDIT_STORE_RECORD_CAPACITY + drop;
  }
  return true;
}

}  // namespace

void auditStoreBootOpen(AuditLogLatch &latch, uint32_t &latchPendingAfter) {
  g_status = AuditStoreStatus{};
  latchPendingAfter = 0;
  const esp_partition_t *part = storePartition();
  if (part == nullptr) {
    g_status.state = AuditStoreState::UNSUPPORTED;
    return;
  }
  if (!tryLockFlashIo()) {
    g_status.state = AuditStoreState::IO_ERROR;
    return;
  }
  bool ok = scanWindow(part);
  // Backfill the latch from the retained window, oldest first, in one pass;
  // adopt() keeps the newest records when the window exceeds the latch ring.
  // Every adopted record is already durable, so nothing starts pending.
  AuditRecord record;
  uint8_t slot[AUDIT_STORE_RECORD_BYTES];
  for (uint32_t index = g_status.windowStart; ok && index < g_status.writeCursor;
       ++index) {
    if (!readSlot(part, index, slot) ||
        !auditStoreDecodeRecord(slot, record)) {
      ok = false;
      break;
    }
    latch.adopt(record);
  }
  if (ok) latchPendingAfter = g_status.seqHigh;
  g_status.state = ok ? AuditStoreState::READY : AuditStoreState::IO_ERROR;
  g_status.retained = g_status.writeCursor - g_status.windowStart;
  unlockFlashIo();
}

bool auditStoreAppend(const AuditRecord &record) {
  if (g_status.state != AuditStoreState::READY) return false;
  const esp_partition_t *part = storePartition();
  if (part == nullptr || !tryLockFlashIo()) return false;
  bool ok = true;
  const size_t offset = slotOffset(g_status.writeCursor);
  // Entering a fresh sector: erase it ahead of the first append so a wrap
  // never programs over live bits, and retire the previous cycle's records
  // it held — the oldest window slice, one full ring behind the cursor.
  if (offset % AUDIT_STORE_SECTOR_BYTES == 0) {
    uint8_t probe[AUDIT_STORE_RECORD_BYTES];
    ok = esp_partition_read(part, offset, probe, sizeof(probe)) == ESP_OK;
    if (ok && !auditStoreSlotEmpty(probe)) {
      ok = esp_partition_erase_range(part, offset, AUDIT_STORE_SECTOR_BYTES) ==
           ESP_OK;
      // Before the first lap the sector holds no window records (only stale or
      // torn bytes), so nothing retires and the subtraction cannot wrap.
      if (ok && g_status.writeCursor >= AUDIT_STORE_RECORD_CAPACITY) {
        const uint32_t retiredThrough = g_status.writeCursor -
                                        AUDIT_STORE_RECORD_CAPACITY +
                                        kRecordsPerSector - 1;
        if (g_status.windowStart <= retiredThrough) {
          g_status.windowStart = retiredThrough + 1;
        }
      }
      if (ok) feedFlashIoWatchdog();
    }
  }
  uint8_t slot[AUDIT_STORE_RECORD_BYTES];
  auditStoreEncodeRecord(slot, record);
  ok = ok && esp_partition_write(part, offset, slot, sizeof(slot)) == ESP_OK;
  if (ok) {
    ++g_status.writeCursor;
    if (record.sequence > g_status.seqHigh) g_status.seqHigh = record.sequence;
  }
  g_status.retained = g_status.writeCursor - g_status.windowStart;
  unlockFlashIo();
  return ok;
}

size_t auditStoreCopyAfter(uint32_t afterSequence, AuditRecord *output,
                           size_t capacity, uint32_t *resumeSlot) {
  if (output == nullptr || capacity == 0 ||
      g_status.state != AuditStoreState::READY) {
    return 0;
  }
  const esp_partition_t *part = storePartition();
  if (part == nullptr || !tryLockFlashIo()) return 0;
  size_t copied = 0;
  AuditRecord record;
  uint8_t slot[AUDIT_STORE_RECORD_BYTES];
  uint32_t index = g_status.windowStart;
  if (resumeSlot != nullptr && *resumeSlot > index) index = *resumeSlot;
  for (; index < g_status.writeCursor && copied < capacity; ++index) {
    if (!readSlot(part, index, slot)) {
      copied = 0;  // a torn read aborts the page rather than lying partially
      break;
    }
    if (!auditStoreDecodeRecord(slot, record)) continue;
    if (record.sequence > afterSequence) output[copied++] = record;
  }
  if (resumeSlot != nullptr) *resumeSlot = index;
  unlockFlashIo();
  return copied;
}

bool auditStoreEraseAll() {
  const esp_partition_t *part = storePartition();
  if (part == nullptr) return true;
  if (!tryLockFlashIo()) return false;
  bool ok = true;
  for (size_t offset = 0; ok && offset < AUDIT_STORE_PARTITION_BYTES;
       offset += AUDIT_STORE_SECTOR_BYTES) {
    ok = esp_partition_erase_range(part, offset, AUDIT_STORE_SECTOR_BYTES) ==
         ESP_OK;
    feedFlashIoWatchdog();
  }
  if (ok) {
    g_status = AuditStoreStatus{};
    g_status.state = AuditStoreState::READY;
  }
  unlockFlashIo();
  return ok;
}

AuditStoreStatus auditStoreStatus() { return g_status; }

}  // namespace openbrewbyweight
#endif
