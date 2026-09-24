// Serial/ESP-log sink and debug-event pipeline: ESP-log tag and level
// mapping, the queue-backed serial writer, ring retention policy, and the
// logEmit/addDebugEvent entry points. Part of the shotStopper control TU;
// included at the position of the original definitions.
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

int shotStopperEspLogVprintf(const char *format, va_list args) {
  if (!__atomic_load_n(&serialLogSinkEnabled, __ATOMIC_ACQUIRE) ||
      format == nullptr) {
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
  size_t length = static_cast<size_t>(formatted);
  if (length >= sizeof(line.text)) {
    length = sizeof(line.text) - 1;
    (void)__atomic_add_fetch(&serialLogQueueTruncated, 1U, __ATOMIC_RELAXED);
  }
  line.length = static_cast<uint16_t>(length);
  if (serialLogQueue == nullptr ||
      xQueueSend(serialLogQueue, &line, 0) != pdTRUE) {
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

void installEspLogSink() {
  if (!serialLogSinkInstalled && initializeSerialLogSink()) {
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
void installEspLogSink() {}
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
  // cppcheck-suppress syntaxError ; older Cppcheck cannot parse this ESP-IDF macro.
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
  if (event.code == DebugCode::LOG_TEXT) {
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
  const LogLevel serialThreshold = currentSerialLogLevel();
  const LogLevel ringThreshold = currentRingRetainLogLevel();
  const bool toSerial = logLevelAtMost(level, serialThreshold);
  const bool toRing = logLevelAtMost(level, ringThreshold);
  if (!toSerial && !toRing) {
    return;
  }
  const uint32_t atMs = millis();
  const uint32_t wallSec = g_wallClock.nowUtcSec(atMs);
  if (toRing) {
    {
      TaskLockGuard lock(debugLogMutex);
      debugLog.add(atMs, wallSec, level, category, DebugCode::LOG_TEXT, 0, 0,
                   message);
      __atomic_store_n(&debugLogDroppedSnapshot, debugLog.overwritten(),
                       __ATOMIC_RELAXED);
    }
  }
  if (toSerial && serialApplicationLogAllowed()) {
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

void logEmit(LogLevel level, DebugCategory category, DebugCode code,
             int32_t argument1 = 0, int32_t argument2 = 0) {
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

  const bool toSerial =
      logLevelAtMost(level, currentSerialLogLevel());
  const bool toRing =
      logLevelAtMost(level, currentRingRetainLogLevel());
  if (!toSerial && !toRing) {
    return;
  }

  {
    TaskLockGuard lock(debugLogMutex);
    if (toRing) {
      debugLog.add(atMs, wallSec, level, category, code, argument1, argument2);
      __atomic_store_n(&debugLogDroppedSnapshot, debugLog.overwritten(),
                       __ATOMIC_RELAXED);
    }
  }

  // Target builds enqueue in the bounded serial sink; host builds emit
  // synchronously into the test stub.
  if (toSerial && serialApplicationLogAllowed()) {
    writeSerialLogLine(event);
  }
}

void addDebugEvent(DebugCategory category, DebugCode code,
                   int32_t argument1 = 0, int32_t argument2 = 0) {
  logEmit(debugCodeDefaultLevel(code), category, code, argument1, argument2);
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
