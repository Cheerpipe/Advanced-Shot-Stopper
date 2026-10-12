#include "ShotStopperAuditStore.h"

// Durable audit store: target builds talk to the auditlog data partition;
// the dedicated host test compiles this file with a RAM-backed partition
// fake (tests/audit_store_host_stubs.h) and exercises the same bodies,
// including power-cut recovery. Plain host control builds compile this file
// out and link the contract stubs from tests/shot_stopper_host_stubs.h.
#if !defined(SHOT_STOPPER_HOST_TEST) || defined(SHOT_STOPPER_AUDIT_HOST_TEST)
#ifndef SHOT_STOPPER_AUDIT_HOST_TEST
#include "ShotStopperFlashIoScratch.h"
#include <esp_partition.h>
#else
#include "tests/audit_store_host_stubs.h"
#endif
#include <string.h>

namespace shotstopper {
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
  return AUDIT_STORE_HEADER_BYTES +
         (absoluteIndex % AUDIT_STORE_RECORD_CAPACITY) *
             AUDIT_STORE_RECORD_BYTES;
}

bool readSlot(const esp_partition_t *part, uint32_t index, uint8_t *slot) {
  return esp_partition_read(part, slotOffset(index), slot,
                            AUDIT_STORE_RECORD_BYTES) == ESP_OK;
}

// Header commits rewrite the same sector, and flash programs can only clear
// bits: every commit erases sector 0 first, then programs the fresh header.
// At a few committed batches per day this stays far inside sector endurance.
bool commitHeader(const esp_partition_t *part) {
  AuditStoreHeader header;
  header.writeCursor = g_status.writeCursor;
  header.windowStart = g_status.windowStart;
  header.seqHigh = g_status.seqHigh;
  header.generation = g_status.generation + 1;
  header.checksum = auditStoreHeaderChecksum(header);
  if (esp_partition_erase_range(part, 0, AUDIT_STORE_SECTOR_BYTES) != ESP_OK) {
    return false;
  }
  feedFlashIoWatchdog();
  if (esp_partition_write(part, 0, &header, sizeof(header)) != ESP_OK) {
    return false;
  }
  g_status.generation = header.generation;
  return true;
}

bool readHeader(const esp_partition_t *part, AuditStoreHeader &header) {
  header = AuditStoreHeader{};
  if (esp_partition_read(part, 0, &header, sizeof(header)) != ESP_OK) {
    return false;
  }
  return auditStoreHeaderValid(header);
}

struct ScanRun {
  uint32_t start, end;  // slot indexes, half-open
};

// Full forward scan for a missing or invalid durable header: valid runs are
// separated by erased (wrap-ahead) or torn slots. The active window is the
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
  for (uint32_t index = g_status.windowStart; index < g_status.writeCursor;
       ++index) {
    if (!readSlot(part, index, slot) || !auditStoreDecodeRecord(slot, record)) {
      return false;
    }
    if (record.sequence > g_status.seqHigh) g_status.seqHigh = record.sequence;
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
  AuditStoreHeader header;
  bool ok = readHeader(part, header);
  bool needCommit = !ok;
  if (ok) {
    g_status.writeCursor = header.writeCursor;
    g_status.windowStart = header.windowStart;
    g_status.seqHigh = header.seqHigh;
    g_status.generation = header.generation;
    // Extend the cursor over records appended after the last commit (power
    // cut between the body program and the header commit); the scan stops at
    // the first erased or torn slot.
    AuditRecord record;
    uint8_t slot[AUDIT_STORE_RECORD_BYTES];
    while (g_status.writeCursor - g_status.windowStart <
           AUDIT_STORE_RECORD_CAPACITY) {
      if (!readSlot(part, g_status.writeCursor, slot)) {
        ok = false;
        break;
      }
      if (!auditStoreDecodeRecord(slot, record)) break;
      if (record.sequence > g_status.seqHigh) {
        g_status.seqHigh = record.sequence;
      }
      ++g_status.writeCursor;
      needCommit = true;
    }
  } else {
    ok = scanWindow(part);
  }
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
  if (ok && needCommit) ok = commitHeader(part);
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
  if ((offset - AUDIT_STORE_HEADER_BYTES) % AUDIT_STORE_SECTOR_BYTES == 0) {
    uint8_t probe[AUDIT_STORE_RECORD_BYTES];
    ok = esp_partition_read(part, offset, probe, sizeof(probe)) == ESP_OK;
    if (ok && !auditStoreSlotEmpty(probe)) {
      ok = esp_partition_erase_range(part, offset, AUDIT_STORE_SECTOR_BYTES) ==
           ESP_OK;
      if (ok) {
        const uint32_t retiredThrough = g_status.writeCursor -
                                        AUDIT_STORE_RECORD_CAPACITY +
                                        kRecordsPerSector - 1;
        if (g_status.windowStart <= retiredThrough) {
          g_status.windowStart = retiredThrough + 1;
        }
        feedFlashIoWatchdog();
      }
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

bool auditStoreCommitHeader() {
  if (g_status.state != AuditStoreState::READY) return false;
  const esp_partition_t *part = storePartition();
  if (part == nullptr || !tryLockFlashIo()) return false;
  const bool ok = commitHeader(part);
  unlockFlashIo();
  return ok;
}

size_t auditStoreCopyAfter(uint32_t afterSequence, AuditRecord *output,
                           size_t capacity) {
  if (output == nullptr || capacity == 0 ||
      g_status.state != AuditStoreState::READY) {
    return 0;
  }
  const esp_partition_t *part = storePartition();
  if (part == nullptr || !tryLockFlashIo()) return 0;
  size_t copied = 0;
  AuditRecord record;
  uint8_t slot[AUDIT_STORE_RECORD_BYTES];
  for (uint32_t index = g_status.windowStart;
       index < g_status.writeCursor && copied < capacity; ++index) {
    if (!readSlot(part, index, slot)) {
      copied = 0;  // a torn read aborts the page rather than lying partially
      break;
    }
    if (!auditStoreDecodeRecord(slot, record)) continue;
    if (record.sequence > afterSequence) output[copied++] = record;
  }
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

}  // namespace shotstopper
#endif
