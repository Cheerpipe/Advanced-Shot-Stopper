// Serial/ESP-log sink and debug-event pipeline: ESP-log tag and level
// mapping, the queue-backed serial writer, ring retention policy, and the
// logEmit/addDebugEvent entry points. Part of the shotStopper control TU;
// included at the position of the original definitions.

// Retains one text line in the WebUI ring when its level passes retention.
void retainLogText(LogLevel level, DebugCategory category, const char *message) {
  if (!logLevelAtMost(level, currentRingRetainLogLevel())) return;
  const uint32_t atMs = millis();
  const uint32_t wallSec = g_wallClock.nowUtcSec(atMs);
  TaskLockGuard lock(debugLogMutex);
  debugLog.add(atMs, wallSec, level, category, DebugCode::LOG_TEXT, 0, 0,
               message);
  __atomic_store_n(&debugLogDroppedSnapshot, debugLog.overwritten(),
                   __ATOMIC_RELAXED);
}

#if !defined(SHOT_STOPPER_HOST_TEST)
esp_log_level_t espLogLevel(LogLevel level) {
  switch (level) {
    // ESP-IDF has no critical/fatal level. Keep that distinction in the
    // application record and mark it in the emitted message.
    case LogLevel::CRITICAL:
    case LogLevel::ERROR: return ESP_LOG_ERROR;
    case LogLevel::WARNING: return ESP_LOG_WARN;
    case LogLevel::INFO: return ESP_LOG_INFO;
    case LogLevel::DEBUG: return ESP_LOG_DEBUG;
    case LogLevel::NONE: return ESP_LOG_NONE;
  }
  return ESP_LOG_NONE;
}

// ESP-IDF component lines (esp-tls, HTTP_CLIENT, ...) reach only this hook and
// production has no USB console, so the WebUI ring retains them too. ss.*
// lines are already there. Log v1 format: "E (1234) tag: text\n".
void retainEspLogLine(const char *text) {
  const char *body = strstr(text, ") ");
  if (text[1] != ' ' || text[2] != '(' || body == nullptr ||
      strncmp(body + 2, "ss.", 3) == 0 || !xPortCanYield()) {
    return;
  }
  char message[DEBUG_EVENT_TEXT_CAPACITY];
  copyCString(message, sizeof(message), body + 2);
  message[strcspn(message, "\r\n")] = '\0';
  retainLogText(text[0] == 'E'   ? LogLevel::ERROR
                : text[0] == 'W' ? LogLevel::WARNING
                : text[0] == 'I' ? LogLevel::INFO
                                 : LogLevel::DEBUG,
                DebugCategory::SYSTEM, message);
}

int shotStopperEspLogVprintf(const char *format, va_list args) {
  if (format == nullptr) {
    return 0;
  }
  // esp_log may invoke this callback concurrently. Never perform USB I/O in
  // the caller: CDC can block indefinitely when the host stops draining.
  SerialLogLine line;
  va_list copy;
  va_copy(copy, args);
  const int formatted = vsnprintf(line.text, sizeof(line.text), format, copy);
  va_end(copy);
  if (formatted <= 0) {
    return formatted;
  }
  if (strstr(line.text,
             "HTTP_CLIENT: Connection timed out before data was ready!") !=
      nullptr) {
    return 0;
  }
  retainEspLogLine(line.text);
  // Without the USB sink, keep IDF's default console output.
  if (serialLogQueue == nullptr) {
    return vprintf(format, args);
  }
  if (!__atomic_load_n(&serialLogSinkEnabled, __ATOMIC_ACQUIRE)) {
    return 0;
  }
  size_t length = static_cast<size_t>(formatted);
  if (length >= sizeof(line.text)) {
    length = sizeof(line.text) - 1;
    (void)__atomic_add_fetch(&serialLogQueueTruncated, 1U, __ATOMIC_RELAXED);
  }
  line.length = static_cast<uint16_t>(length);
  if (xQueueSend(serialLogQueue, &line, 0) != pdTRUE) {
    (void)__atomic_add_fetch(&serialLogQueueDropped, 1U, __ATOMIC_RELAXED);
    return 0;
  }
  return static_cast<int>(length);
}

void serialLogTask(void *) {
  SerialLogLine line;
  for (;;) {
    if (drainSerialCliOutput()) continue;
    if (xQueueReceive(serialLogQueue, &line, pdMS_TO_TICKS(10)) == pdTRUE &&
        line.length > 0) {
      (void)Serial.write(reinterpret_cast<const uint8_t *>(line.text),
                         line.length);
    }
  }
}

bool initializeSerialLogSink() {
  if (serialLogQueue != nullptr && serialLogTaskHandle != nullptr) {
    return true;
  }
  constexpr size_t queueBytes = SERIAL_LOG_QUEUE_DEPTH * sizeof(SerialLogLine);
  serialLogQueueBytes = static_cast<uint8_t *>(
      allocInternal(queueBytes, AllocationOwner::SERIAL_LOG));
  if (serialLogQueueBytes == nullptr || !esp_ptr_internal(serialLogQueueBytes)) {
    heapCapsFree(serialLogQueueBytes);
    serialLogQueueBytes = nullptr;
    return false;
  }
  serialLogQueue = xQueueCreateStatic(
      SERIAL_LOG_QUEUE_DEPTH, sizeof(SerialLogLine), serialLogQueueBytes,
      &serialLogQueueStorage);
  if (serialLogQueue == nullptr) {
    releaseSerialLogStorage();
    return false;
  }
  serialCliOutputBytes = static_cast<uint8_t *>(
      allocExternal(SERIAL_CLI_OUTPUT_CAPACITY, AllocationOwner::SERIAL_LOG));
  if (serialCliOutputBytes == nullptr) {
    releaseSerialLogStorage();
    return false;
  }
  if (xTaskCreatePinnedToCore(serialLogTask, "serial_log", 3072, nullptr,
                             tskIDLE_PRIORITY, &serialLogTaskHandle, 0) !=
      pdPASS) {
    releaseSerialLogStorage();
    serialLogTaskHandle = nullptr;
    return false;
  }
  return true;
}

// Installed in every build so the WebUI ring sees IDF lines; the USB queue
// exists only when the console is enabled.
void installEspLogSink(bool usbSerial) {
  if (usbSerial) (void)initializeSerialLogSink();
  if (!serialLogSinkInstalled) {
    (void)esp_log_set_vprintf(shotStopperEspLogVprintf);
    serialLogSinkInstalled = true;
  }
}

void configureEspLogRuntime() {
  const LogLevel applicationLevel = currentSerialLogLevel();
  const bool enabled = applicationLevel != LogLevel::NONE;
  // Close the sink first when disabling so another task cannot emit under the
  // old tag level while this update is in progress.
  if (!enabled) {
    __atomic_store_n(&serialLogSinkEnabled, false, __ATOMIC_RELEASE);
  }
  const esp_log_level_t level = espLogLevel(applicationLevel);
  static const char *const tags[] = {
      "ss.activator", "ss.relay", "ss.state", "ss.scale", "ss.config",
      "ss.network", "ss.security", "ss.web", "ss.boot", "ss.system"};
  for (const char *tag : tags) {
    esp_log_level_set(tag, level);
  }
  if (enabled) {
    __atomic_store_n(&serialLogSinkEnabled, true, __ATOMIC_RELEASE);
  }
}
#else
void installEspLogSink(bool) {}
void configureEspLogRuntime() {}
#endif

void latchControlCriticalLogging() {
  controlCriticalForLogging.store(true, std::memory_order_release);
}

void publishControlCriticalLoggingState() {
  const bool critical = session.active || getRelaySafetySnapshot().closed;
  controlCriticalForLogging.store(critical, std::memory_order_release);
}

bool serialApplicationLogAllowed() {
#if defined(SHOT_STOPPER_HOST_TEST)
  // Portable tests directly manipulate the control-owned fixtures.
  return !session.active && !circuitClosed;
#else
  return !controlCriticalForLogging.load(std::memory_order_acquire);
#endif
}

void emitEspLog(LogLevel level, DebugCategory category, const char *message) {
  if (message == nullptr || level == LogLevel::NONE) {
    return;
  }
#if !defined(SHOT_STOPPER_HOST_TEST)
  // espLogTag was removed: the tag is debugCategoryName() prefixed with
  // "ss.". Derive it here so the category names have a single source.
  char tag[24];
  copyCString(tag, sizeof(tag), "ss.");
  copyCString(tag + 3, sizeof(tag) - 3, debugCategoryName(category));
  const char *criticalPrefix =
      level == LogLevel::CRITICAL ? "[CRITICAL] " : "";
  // ESP_LOG_LEVEL accepts a runtime level without the ESP_LOGx local-level
  // compile-out and adds the standard severity, timestamp, tag and newline.
  ESP_LOG_LEVEL(espLogLevel(level), tag, "%s%s", criticalPrefix, message);
#else
  (void)category;
  Serial.println(message);
#endif
}

void formatDebugEventMessage(const DebugEvent &event, char *message,
                             size_t capacity) {
  if (message == nullptr || capacity == 0) {
    return;
  }
  if (event.text[0] != '\0') {
    copyCString(message, capacity, event.text);
    return;
  }
  if (event.code == DebugCode::BOOT_BANNER) {
    snprintf(message, capacity, "Open Brew by Weight %s (bootId=%ld)",
             FW_VERSION, static_cast<long>(event.argument1));
    return;
  }
  if (event.code == DebugCode::STATE_TRANSITION &&
      event.argument1 >= static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
      event.argument1 <= static_cast<int32_t>(StopperState::MANUAL_NO_SCALE) &&
      event.argument2 >= static_cast<int32_t>(StopperState::REQUIRES_OFF) &&
      event.argument2 <= static_cast<int32_t>(StopperState::MANUAL_NO_SCALE)) {
    snprintf(message, capacity, "%s -> %s",
             stopperStateName(static_cast<StopperState>(event.argument1)),
             stopperStateName(static_cast<StopperState>(event.argument2)));
    return;
  }
  if (formatScaleSampleDebugMessage(event, message, capacity)) {
    return;
  }
  if (formatPersistDebugMessage(event, message, capacity)) {
    return;
  }
  if (formatLifecycleDebugMessage(event, message, capacity)) {
    return;
  }
  if ((event.code == DebugCode::WEB_COMMAND_ACCEPTED ||
       event.code == DebugCode::WEB_COMMAND_REJECTED) &&
      event.argument1 >= static_cast<int32_t>(WebCommandType::REMOTE_ON) &&
      event.argument1 <=
          static_cast<int32_t>(WebCommandType::MAINTENANCE_COMPLETE)) {
    snprintf(message, capacity, "%s: %s", debugCodeName(event.code),
             webCommandTypeName(
                 static_cast<WebCommandType>(event.argument1)));
    return;
  }
  copyCString(message, capacity, debugCodeName(event.code));
}

void writeSerialLogLine(const DebugEvent &event) {
  char message[128] = {};
  formatDebugEventMessage(event, message, sizeof(message));
  emitEspLog(event.level, event.category, message);
}

void logText(LogLevel level, DebugCategory category, const char *message) {
  if (message == nullptr || level == LogLevel::NONE) {
    return;
  }
  retainLogText(level, category, message);
  if (logLevelAtMost(level, currentSerialLogLevel()) &&
      serialApplicationLogAllowed()) {
    emitEspLog(level, category, message);
  }
}

// Ad-hoc diagnostics are application logs too: retain them in the WebUI ring
// and forward them to esp_log. Keep this compatibility wrapper for existing
// call sites while they are gradually made category-specific.
void serialTrace(LogLevel level, const char *message) {
  logText(level, DebugCategory::SYSTEM, message);
}

void serialTraceCategory(LogLevel level, DebugCategory category,
                         const char *message) {
  logText(level, category, message);
}

extern "C" void shotStopperScaleLog(uint8_t severity, const char *message) {
  LogLevel level = LogLevel::DEBUG;
  if (severity == 4) {
    level = LogLevel::INFO;
  } else if (severity == 1) {
    level = LogLevel::WARNING;
  } else if (severity >= 2) {
    level = LogLevel::ERROR;
  }
  serialTraceCategory(level, DebugCategory::SCALE, message);
}

void serialTracef(LogLevel level, const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[DEBUG_EVENT_TEXT_CAPACITY] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  logText(level, DebugCategory::SYSTEM, line);
}

void serialTraceCategoryf(LogLevel level, DebugCategory category,
                          const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[DEBUG_EVENT_TEXT_CAPACITY] = {};
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  logText(level, category, line);
}

// Verbose audit hunt session (Diagnostics toggle): marked DEBUG codes join
// the latch while the wall-clock deadline holds. The session lives in RAM
// like a profiler run — a reboot ends the hunt, the records it captured stay.
constexpr uint32_t AUDIT_VERBOSE_SESSION_SEC = 30 * 60;
uint32_t auditVerboseUntilUtcSec = 0;
bool auditVerboseActive() {
  return auditVerboseUntilUtcSec != 0 &&
         g_wallClock.nowUtcSec(millis()) < auditVerboseUntilUtcSec;
}
bool setAuditVerbose(bool on) {
  if (!on) {
    auditVerboseUntilUtcSec = 0;
    return true;
  }
  const uint32_t now = g_wallClock.nowUtcSec(millis());
  if (now == 0) return false;  // a hunt deadline needs a synced wall clock
  auditVerboseUntilUtcSec = now + AUDIT_VERBOSE_SESSION_SEC;
  serialTraceCategoryf(LogLevel::INFO, DebugCategory::SYSTEM,
                       "audit verbose session armed for %u s", AUDIT_VERBOSE_SESSION_SEC);
  return true;
}
uint32_t auditVerboseDeadline() {
  return auditVerboseActive() ? auditVerboseUntilUtcSec : 0;
}

void logEmitDetail(LogLevel level, DebugCategory category, DebugCode code,
                   int32_t argument1, int32_t argument2,
                   const char *detail) {
  if (level == LogLevel::NONE) {
    return;
  }
  const uint32_t atMs = millis();
  const uint32_t wallSec = g_wallClock.nowUtcSec(atMs);
  DebugEvent event;
  event.atMs = atMs;
  event.wallSec = wallSec;
  event.level = level;
  event.category = category;
  event.code = code;
  event.argument1 = argument1;
  event.argument2 = argument2;
  if (detail != nullptr) {
    copyCString(event.text, sizeof(event.text), detail);
  }

  const bool toSerial =
      logLevelAtMost(level, currentSerialLogLevel());
  const bool toRing =
      logLevelAtMost(level, currentRingRetainLogLevel());
  // Audit capture is level-independent: curated events enter the latch even
  // when neither serial nor ring retains them.
  const bool toAudit = auditWorthy(level, code, auditVerboseActive());
  if (!toSerial && !toRing && !toAudit) {
    return;
  }

  {
    TaskLockGuard lock(debugLogMutex);
    if (toRing) {
      debugLog.add(atMs, wallSec, level, category, code, argument1, argument2,
                   detail);
      __atomic_store_n(&debugLogDroppedSnapshot, debugLog.overwritten(),
                       __ATOMIC_RELAXED);
    }
    if (toAudit) {
      auditLog.add(atMs, wallSec, level, category, code, argument1, argument2,
                   detail);
    }
  }

  // Target builds enqueue in the bounded serial sink; host builds emit
  // synchronously into the test stub.
  if (toSerial && serialApplicationLogAllowed()) {
    writeSerialLogLine(event);
  }
}

void logEmit(LogLevel level, DebugCategory category, DebugCode code,
             int32_t argument1 = 0, int32_t argument2 = 0) {
  logEmitDetail(level, category, code, argument1, argument2, nullptr);
}

void addDebugEvent(DebugCategory category, DebugCode code,
                   int32_t argument1 = 0, int32_t argument2 = 0) {
  logEmit(debugCodeDefaultLevel(code), category, code, argument1, argument2);
}

void addDebugEventDetail(DebugCategory category, DebugCode code,
                         int32_t argument1, int32_t argument2,
                         const char *detail) {
  logEmitDetail(debugCodeDefaultLevel(code), category, code, argument1,
                argument2, detail);
}

uint32_t debugLogLatestSequence() {
  TaskLockGuard lock(debugLogMutex);
  return debugLog.latestSequence();
}

size_t copyDebugEvents(uint32_t afterSequence, DebugEvent *output,
                       size_t capacity,
                       DebugLogReadMetadata *metadata = nullptr) {
  TaskLockGuard lock(debugLogMutex);
  const size_t copied =
      debugLog.copyAfter(afterSequence, output, capacity, metadata);
  if (metadata != nullptr) {
    metadata->serialDropped = serialLogDroppedCount();
    metadata->serialTruncated = serialLogTruncatedCount();
  }
  return copied;
}

size_t copyAuditEvents(uint32_t afterSequence, AuditRecord *output,
                       size_t capacity, uint32_t *dropped = nullptr,
                       uint32_t *rateLimited = nullptr) {
  {
    TaskLockGuard lock(debugLogMutex);
    if (dropped != nullptr) *dropped = auditLog.overwritten();
    if (rateLimited != nullptr) *rateLimited = auditLog.rateLimited();
  }
  // Boot adoption mirrors every flushed record in the latch, so the durable
  // window only contributes records older than the latch's oldest page item;
  // both sources come back in ascending sequence order.
  AuditRecord probe[1];
  uint32_t latchOldest = 0;
  {
    TaskLockGuard lock(debugLogMutex);
    if (auditLog.copyAfter(afterSequence, probe, 1) > 0) {
      latchOldest = probe[0].sequence;
    }
  }
  size_t filled = 0;
  if (latchOldest == 0 || latchOldest > afterSequence + 1) {
    AuditRecord durablePage[8];
    uint32_t durableAfter = afterSequence;
    while (filled < capacity) {
      const size_t count =
          auditStoreCopyAfter(durableAfter, durablePage, 8);
      if (count == 0) break;
      for (size_t index = 0; index < count && filled < capacity; ++index) {
        if (latchOldest != 0 && durablePage[index].sequence >= latchOldest) {
          continue;  // already mirrored in the latch page
        }
        output[filled++] = durablePage[index];
      }
      durableAfter = durablePage[count - 1].sequence;
      if (count < 8) break;
    }
  }
  {
    TaskLockGuard lock(debugLogMutex);
    if (filled < capacity) {
      filled += auditLog.copyAfter(afterSequence, output + filled,
                                   capacity - filled);
    }
  }
  return filled;
}

// Explicit clear (endpoint / factory reset): erase the durable store first,
// then drop the latch so both surfaces agree the history is empty.
uint32_t auditLogLatestSequence() {
  TaskLockGuard lock(debugLogMutex);
  return auditLog.latestSequence();
}

void copyAuditStoreStatus(AuditStoreStatus &output) { output = auditStoreStatus(); }

void clearAuditLatch() {
  TaskLockGuard lock(debugLogMutex);
  auditLog.clear();
}

// Explicit clear (endpoint / factory reset): erase the durable store first,
// then drop the latch so both surfaces agree the history is empty. Declared
// in ShotStopperAuditStore.h; this TU fragment is its single definition.
namespace shotstopper {
bool clearAuditStores() {
  if (!auditStoreEraseAll()) return false;
  clearAuditLatch();
  return true;
}
}  // namespace shotstopper
