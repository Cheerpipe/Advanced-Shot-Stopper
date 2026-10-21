// Audit surface on the diagnostic stream: the merged latch + durable window
// on its own fingerprinted frame, so the Diagnostics page sees the newest
// records and the hunt-session state while an unchanged window costs nothing.
// Split out of ShotStopperHomeStream.inc to keep that fragment inside the
// service line cap.

bool ShotStopperNetwork::sendAuditDiagFrame(UiStreamSession &session,
                                            bool initial) {
  if (callbacks_.copyAuditEvents == nullptr ||
      callbacks_.auditLogLatestSequence == nullptr) {
    return true;
  }
  AuditStoreStatus store{};
  if (callbacks_.copyAuditStoreStatus != nullptr) {
    callbacks_.copyAuditStoreStatus(store);
  }
  uint32_t dropped = 0, rateLimited = 0;
  const uint32_t latest = callbacks_.auditLogLatestSequence();
  const uint32_t after = latest > 24 ? latest - 24 : 0;
  const size_t records =
      callbacks_.copyAuditEvents(after, g_work->auditBatch,
                                 NetworkWorkBuf::kAuditBatchSize, &dropped,
                                 &rateLimited);
  size_t used = 0;
  if (!statusJsonAppend(&used,
          "{\"type\":\"diagnostic\","
          "\"changes\":{\"audit\":{\"state\":\"%s\",\"retained\":%lu,"
          "\"dropped\":%lu,\"rateLimited\":%lu,\"verboseUntilUtcSec\":%lu,"
          "\"items\":[",
          store.state == AuditStoreState::READY
              ? "ready"
              : store.state == AuditStoreState::UNSUPPORTED ? "unsupported"
                                                            : "io_error",
          static_cast<unsigned long>(store.retained),
          static_cast<unsigned long>(dropped),
          static_cast<unsigned long>(rateLimited),
          static_cast<unsigned long>(callbacks_.auditVerboseDeadline != nullptr
                                         ? callbacks_.auditVerboseDeadline()
                                         : 0))) {
    return false;
  }
  for (size_t index = 0; index < records; ++index) {
    const AuditRecord &record = g_work->auditBatch[index];
    // Full detail rebuilt from the persisted args plus the emit-time excerpt;
    // the field stays `text` so the frame shape and its closer are unchanged.
    char detail[128] = {};
    char safeDetail[sizeof(detail) * 6] = {};
    formatAuditRecordDetail(record, detail, sizeof(detail));
    escapeJsonString(detail, safeDetail, sizeof(safeDetail));
    if (!statusJsonAppend(&used,
            "%s{\"sequence\":%lu,\"wallSec\":%lu,\"atMs\":%lu,"
            "\"level\":\"%s\",\"category\":\"%s\",\"code\":\"%s\","
            "\"argument1\":%ld,\"argument2\":%ld,\"text\":\"%s\"}",
            index == 0 ? "" : ",",
            static_cast<unsigned long>(record.sequence),
            static_cast<unsigned long>(record.wallSec),
            static_cast<unsigned long>(record.atMs),
            logLevelName(static_cast<LogLevel>(record.level)),
            debugCategoryName(static_cast<DebugCategory>(record.category)),
            debugCodeName(static_cast<DebugCode>(record.code)),
            static_cast<long>(record.argument1),
            static_cast<long>(record.argument2), safeDetail)) {
      return false;
    }
  }
  // Closes items[], audit{}, changes{} and the frame root; one brace short
  // ships invalid JSON and forces the client into its resync-churn path.
  if (!statusJsonAppend(&used, "]}}}")) {
    return false;
  }
  const uint32_t fingerprint =
      crc32Update(0xFFFFFFFFU, reinterpret_cast<const uint8_t *>(g_work->statusJson),
                  used);
  if (initial || fingerprint != session.diagAuditFingerprint) {
    if (!sendUiStreamFrame(session, used, session.diagResync)) return false;
    if (session.diagResync) return true;
    session.diagAuditFingerprint = fingerprint;
  }
  return true;
}
