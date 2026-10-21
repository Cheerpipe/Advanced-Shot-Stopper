#pragma once

#include "OpenBrewAuditLog.h"
#include <stddef.h>
#include <stdint.h>

namespace shotstopper {

// Durable audit store (phase 2): an append-only ring of fixed-size packed
// records in a dedicated 64 KiB data partition. Sector 0 holds the durable
// header, committed last; the body is written in whole-record programs and
// wraps by erasing the 4 KiB sector ahead of the cursor, so a full circle
// erases each sector exactly once. A power cut can lose at most the records
// not yet flushed from the RAM latch; the boot scan recovers the last
// consistent record without intervention.
constexpr uint32_t AUDIT_STORE_MAGIC = 0x41554C47UL;  // "AULG"
constexpr uint16_t AUDIT_STORE_SCHEMA_VERSION = 0;
constexpr size_t AUDIT_STORE_PARTITION_BYTES = 0x10000;
constexpr size_t AUDIT_STORE_HEADER_BYTES = 4096;
constexpr size_t AUDIT_STORE_SECTOR_BYTES = 4096;
// u8 version, u8 level, u8 category, u8 code, u32 seq, u32 wallSec, u32 atMs,
// i32 arg1, i32 arg2, u8 textLen, text[<24], u16 checksum — padded to a
// 64-byte slot so every record lives inside one 4 KiB erase sector and both
// boot-scan directions stay trivially parseable.
constexpr size_t AUDIT_STORE_RECORD_BYTES = 64;
static_assert(AUDIT_STORE_SECTOR_BYTES % AUDIT_STORE_RECORD_BYTES == 0,
              "audit records must tile the erase sector exactly");
constexpr size_t AUDIT_STORE_RECORD_CAPACITY =
    (AUDIT_STORE_PARTITION_BYTES - AUDIT_STORE_HEADER_BYTES) /
    AUDIT_STORE_RECORD_BYTES;
static_assert(AUDIT_STORE_RECORD_CAPACITY == 960,
              "audit store capacity must match the wear planning");

// Host- and target-pure helpers over packed slots.
inline bool auditStoreSlotEmpty(const uint8_t *slot) {
  for (size_t i = 0; i < AUDIT_STORE_RECORD_BYTES; ++i) {
    if (slot[i] != 0xFF) return false;
  }
  return true;
}

inline uint16_t auditStoreChecksum(const uint8_t *data, size_t length) {
  uint32_t checksum = 2166136261U;
  for (size_t i = 0; i < length; ++i) {
    checksum = (checksum ^ data[i]) * 16777619U;
  }
  return static_cast<uint16_t>(checksum & 0xFFFFU);
}

// Decodes one packed record; the input must hold AUDIT_STORE_RECORD_BYTES.
inline bool auditStoreDecodeRecord(const uint8_t *slot, AuditRecord &record) {
  record = AuditRecord{};
  if (auditStoreSlotEmpty(slot)) return false;
  uint32_t seq = 0, wallSec = 0, atMs = 0, arg1 = 0, arg2 = 0;
  const uint8_t *cursor = slot;
  const uint8_t version = *cursor++;
  const uint8_t level = *cursor++;
  const uint8_t category = *cursor++;
  const uint8_t code = *cursor++;
  for (int shift = 0; shift < 32; shift += 8) seq |= uint32_t(*cursor++) << shift;
  for (int shift = 0; shift < 32; shift += 8) wallSec |= uint32_t(*cursor++) << shift;
  for (int shift = 0; shift < 32; shift += 8) atMs |= uint32_t(*cursor++) << shift;
  for (int shift = 0; shift < 32; shift += 8) arg1 |= uint32_t(*cursor++) << shift;
  for (int shift = 0; shift < 32; shift += 8) arg2 |= uint32_t(*cursor++) << shift;
  const uint8_t textLen = *cursor++;
  if (version != AUDIT_STORE_SCHEMA_VERSION || textLen >= AUDIT_EXCERPT_BYTES) {
    return false;
  }
  for (uint8_t i = 0; i < textLen; ++i) record.text[i] = static_cast<char>(*cursor++);
  const uint8_t *stored = slot + AUDIT_STORE_RECORD_BYTES - 2;
  const uint16_t checksum =
      uint16_t(stored[0]) | (uint16_t(stored[1]) << 8);
  if (checksum != auditStoreChecksum(slot, AUDIT_STORE_RECORD_BYTES - 2)) {
    return false;
  }
  record.sequence = seq;
  record.wallSec = wallSec;
  record.atMs = atMs;
  record.argument1 = static_cast<int32_t>(arg1);
  record.argument2 = static_cast<int32_t>(arg2);
  record.level = level;
  record.category = category;
  record.code = code;
  record.textLen = textLen;
  return seq != 0;
}

// Encodes one record into a zeroed AUDIT_STORE_RECORD_BYTES slot.
inline void auditStoreEncodeRecord(uint8_t *slot, const AuditRecord &record) {
  for (size_t i = 0; i < AUDIT_STORE_RECORD_BYTES; ++i) slot[i] = 0;
  uint8_t *cursor = slot;
  *cursor++ = AUDIT_STORE_SCHEMA_VERSION;
  *cursor++ = record.level;
  *cursor++ = record.category;
  *cursor++ = record.code;
  const uint32_t words[5] = {record.sequence, record.wallSec, record.atMs,
                             static_cast<uint32_t>(record.argument1),
                             static_cast<uint32_t>(record.argument2)};
  for (uint32_t word : words) {
    for (int shift = 0; shift < 32; shift += 8) {
      *cursor++ = static_cast<uint8_t>(word >> shift);
    }
  }
  const uint8_t textLen = record.textLen < AUDIT_EXCERPT_BYTES
                              ? record.textLen
                              : AUDIT_EXCERPT_BYTES - 1;
  *cursor++ = textLen;
  for (uint8_t i = 0; i < textLen; ++i) {
    *cursor++ = static_cast<uint8_t>(record.text[i]);
  }
  const uint16_t checksum =
      auditStoreChecksum(slot, AUDIT_STORE_RECORD_BYTES - 2);
  slot[AUDIT_STORE_RECORD_BYTES - 2] = static_cast<uint8_t>(checksum);
  slot[AUDIT_STORE_RECORD_BYTES - 1] = static_cast<uint8_t>(checksum >> 8);
}

// Durable header in sector 0; committed after each flushed batch.
struct AuditStoreHeader {
  uint32_t magic = 0;
  uint16_t schemaVersion = 0;
  uint16_t headerBytes = 0;
  uint32_t writeCursor = 0;  // absolute record index of the next append
  uint32_t windowStart = 0;  // oldest retained record index
  uint32_t seqHigh = 0;      // highest persisted sequence
  uint32_t generation = 0;   // bumped on every header commit
  uint32_t reserved[3] = {};
  uint32_t checksum = 0;     // FNV-1a (16-bit) over the preceding bytes
};
static_assert(sizeof(AuditStoreHeader) <= AUDIT_STORE_HEADER_BYTES,
              "audit store header must fit its dedicated sector");

inline uint16_t auditStoreHeaderChecksum(const AuditStoreHeader &header) {
  return auditStoreChecksum(reinterpret_cast<const uint8_t *>(&header),
                            offsetof(AuditStoreHeader, checksum));
}

inline bool auditStoreHeaderValid(const AuditStoreHeader &header) {
  return header.magic == AUDIT_STORE_MAGIC &&
         header.schemaVersion == AUDIT_STORE_SCHEMA_VERSION &&
         header.headerBytes == sizeof(AuditStoreHeader) &&
         header.writeCursor >= header.windowStart &&
         header.writeCursor - header.windowStart <=
             AUDIT_STORE_RECORD_CAPACITY &&
         header.checksum == auditStoreHeaderChecksum(header);
}

// Partition-backed store state shared with the persistence worker.
enum class AuditStoreState : uint8_t {
  UNSUPPORTED,  // partition absent: RAM-latch degraded mode
  READY,
  IO_ERROR,
};

struct AuditStoreStatus {
  AuditStoreState state = AuditStoreState::UNSUPPORTED;
  uint32_t writeCursor = 0;
  uint32_t windowStart = 0;
  uint32_t seqHigh = 0;
  uint32_t generation = 0;
  size_t retained = 0;
};

// Opens (or recovers) the store at boot: backfills `latch` with the retained
// window marked as already durable, and positions the append cursor. Must run
// before any flush; safe to call when the partition is absent.
void auditStoreBootOpen(AuditLogLatch &latch, uint32_t &latchPendingAfter);

// Appends one encoded record at the cursor, erasing the wrap-ahead sector when
// the cursor enters a new one. Returns false on I/O failure (record skipped).
bool auditStoreAppend(const AuditRecord &record);

// Commits the durable header after a flushed batch.
bool auditStoreCommitHeader();

// Copies up to `capacity` retained records in sequence order, newest last,
// starting after `afterSequence`; returns how many were copied.
size_t auditStoreCopyAfter(uint32_t afterSequence, AuditRecord *output,
                           size_t capacity);

// Erases the whole store (header and body). Returns false on I/O failure.
bool auditStoreEraseAll();

// Explicit clear (diagnostics endpoint / factory reset): erase the durable
// store, then drop the RAM latch. Defined by the control TU.
bool clearAuditStores();

AuditStoreStatus auditStoreStatus();

}  // namespace shotstopper
