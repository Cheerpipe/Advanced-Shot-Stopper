// Audit classification and RAM latch contracts (plan F1.A/F1.B): every ERROR
// is audit-worthy, WARNING codes enter through the curated allowlist, marked
// DEBUG codes only in verbose mode, and the latch ring rate-limits per code
// while common-ring behavior stays untouched.
#include "ShotStopperAuditLog.h"

#include <cassert>
#include <cstdio>
#include <cstring>

using namespace shotstopper;

static void testClassification() {
  // ERROR and CRITICAL always qualify, regardless of the code.
  assert(auditWorthy(LogLevel::ERROR, DebugCode::CONFIG_ACCEPTED));
  assert(auditWorthy(LogLevel::CRITICAL, DebugCode::OTA_ROLLBACK_ARMED));
  // Allowlisted WARNING qualifies; an ordinary WARNING does not.
  assert(auditWorthy(LogLevel::WARNING, DebugCode::MACHINE_MUX_WAIT));
  assert(auditWorthy(LogLevel::WARNING, DebugCode::HEALTH_HEAP_LOW));
  assert(auditWorthy(LogLevel::WARNING, DebugCode::RELAY_GPIO_DESYNC));
  assert(!auditWorthy(LogLevel::WARNING, DebugCode::WEB_COMMAND_REJECTED));
  // INFO never qualifies; DEBUG only when marked and verbose is active.
  assert(!auditWorthy(LogLevel::INFO, DebugCode::MACHINE_MUX_WAIT));
  assert(!auditWorthy(LogLevel::DEBUG, DebugCode::MAINTENANCE_RESERVED));
  assert(auditWorthy(LogLevel::DEBUG, DebugCode::MAINTENANCE_RESERVED, true));
  assert(!auditWorthy(LogLevel::DEBUG, DebugCode::SCALE_SCAN_WAITING, true));
  // NONE is never audited even for allowlisted codes.
  assert(!auditWorthy(LogLevel::NONE, DebugCode::MACHINE_MUX_WAIT, true));
}

static void testLatchCaptureAndPaging() {
  AuditLogLatch latch;
  assert(latch.count() == 0);
  assert(latch.latestSequence() == 0);
  assert(latch.add(100, 1000, LogLevel::WARNING, DebugCategory::NETWORK,
                   DebugCode::MACHINE_MUX_WAIT, 850000, 12000,
                   "micra mutex wait while backflush poll held it"));
  assert(latch.add(400, 1300, LogLevel::ERROR, DebugCategory::CONFIG,
                   DebugCode::CONFIG_REJECTED, 7, 0, nullptr));
  AuditRecord page[8];
  assert(latch.copyAfter(0, page, 8) == 2);
  assert(page[0].code == static_cast<uint8_t>(DebugCode::MACHINE_MUX_WAIT));
  assert(page[0].sequence == 1);
  assert(page[0].argument1 == 850000);
  assert(page[0].textLen > 0 && page[0].textLen < AUDIT_EXCERPT_BYTES);
  assert(strncmp(page[0].text, "micra mutex wait", 16) == 0);
  assert(page[1].sequence == 2);
  assert(page[1].textLen == 0 && page[1].text[0] == '\0');
  // Sequence paging mirrors the common ring: after=1 returns only newer.
  assert(latch.copyAfter(1, page, 8) == 1);
  assert(page[0].sequence == 2);
  assert(latch.latestSequence() == 2);
  // Zero-capacity and null-output reads are safe no-ops.
  assert(latch.copyAfter(0, nullptr, 0) == 0);
  assert(latch.copyAfter(0, page, 0) == 0);
}

static void testExcerptTruncation() {
  AuditLogLatch latch;
  const char *longDetail =
      "0123456789012345678901234567890123456789012345678901234567890123";
  assert(latch.add(10, 10, LogLevel::ERROR, DebugCategory::SYSTEM,
                   DebugCode::INITIALIZATION_FAILED, 0, 0, longDetail));
  AuditRecord record;
  assert(latch.copyAfter(0, &record, 1) == 1);
  assert(record.textLen == AUDIT_EXCERPT_BYTES - 1);
  assert(record.text[AUDIT_EXCERPT_BYTES - 1] == '\0');
}

static void testPerCodeRateLimit() {
  AuditLogLatch latch;
  uint32_t atMs = 5000;
  for (uint8_t i = 0; i < AUDIT_RATE_LIMIT; ++i) {
    assert(latch.add(atMs, 100, LogLevel::WARNING, DebugCategory::NETWORK,
                     DebugCode::MACHINE_MUX_WAIT, i, 0, nullptr));
  }
  assert(latch.count() == AUDIT_RATE_LIMIT);
  // The 11th event of the same code inside the window is suppressed.
  assert(!latch.add(atMs + 100, 100, LogLevel::WARNING, DebugCategory::NETWORK,
                    DebugCode::MACHINE_MUX_WAIT, 10, 0, nullptr));
  assert(latch.count() == AUDIT_RATE_LIMIT);
  assert(latch.rateLimited() == 1);
  // A different code is unaffected by the saturated slot.
  assert(latch.add(atMs + 100, 100, LogLevel::WARNING, DebugCategory::RELAY,
                   DebugCode::RELAY_GPIO_DESYNC, 1, 0, nullptr));
  // After the window rolls over the same code is admitted again.
  atMs += AUDIT_RATE_WINDOW_MS + 1;
  assert(latch.add(atMs, 100, LogLevel::WARNING, DebugCategory::NETWORK,
                   DebugCode::MACHINE_MUX_WAIT, 20, 0, nullptr));
  assert(latch.rateLimited() == 1);
}

static void testRingWrapAndClear() {
  AuditLogLatch latch;
  // Spacing stays under ten events per rate window so every add is admitted.
  for (uint32_t i = 0; i < AUDIT_RECORD_CAPACITY + 10; ++i) {
    assert(latch.add(i * 6010, i, LogLevel::ERROR, DebugCategory::SYSTEM,
                     DebugCode::INITIALIZATION_FAILED, static_cast<int32_t>(i),
                     0, nullptr));
  }
  assert(latch.count() == AUDIT_RECORD_CAPACITY);
  assert(latch.overwritten() == 10);
  AuditRecord page[AUDIT_RECORD_CAPACITY];
  assert(latch.copyAfter(0, page, AUDIT_RECORD_CAPACITY) ==
         AUDIT_RECORD_CAPACITY);
  assert(page[0].argument1 == 10);
  assert(page[AUDIT_RECORD_CAPACITY - 1].argument1 ==
          static_cast<int32_t>(AUDIT_RECORD_CAPACITY + 9));
  latch.clear();
  assert(latch.count() == 0);
  assert(latch.overwritten() == 0);
  assert(latch.rateLimited() == 0);
  assert(latch.latestSequence() == 0);
}

int main() {
  testClassification();
  testLatchCaptureAndPaging();
  testExcerptTruncation();
  testPerCodeRateLimit();
  testRingWrapAndClear();
  printf("audit_log_host_test PASSED\n");
  return 0;
}
