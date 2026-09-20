#if !defined(SHOT_STOPPER_HOST_TEST)
struct SerialLogLine {
  uint16_t length = 0;
  char text[256] = {};
};

constexpr UBaseType_t SERIAL_LOG_QUEUE_DEPTH = 64;
StaticQueue_t serialLogQueueStorage;
uint8_t *serialLogQueueBytes = nullptr;
QueueHandle_t serialLogQueue = nullptr;
TaskHandle_t serialLogTaskHandle = nullptr;
uint32_t serialLogQueueDropped = 0;
uint32_t serialLogQueueTruncated = 0;

class SerialCliQueuePrint : public Print {
 public:
  size_t write(uint8_t value) override {
    if (line_.length == sizeof(line_.text)) flushLine();
    line_.text[line_.length++] = static_cast<char>(value);
    if (value == '\n') flushLine();
    return 1;
  }

  size_t write(const uint8_t *buffer, size_t size) override {
    if (buffer == nullptr) return 0;
    for (size_t i = 0; i < size; ++i) write(buffer[i]);
    return size;
  }

 private:
  void flushLine() {
    if (line_.length == 0) return;
    if (serialLogQueue == nullptr ||
        xQueueSend(serialLogQueue, &line_, 0) != pdTRUE) {
      (void)__atomic_add_fetch(&serialLogQueueDropped, 1U, __ATOMIC_RELAXED);
    }
    line_ = SerialLogLine{};
  }

  SerialLogLine line_;
};

SerialCliQueuePrint serialCliQueuePrint;

namespace shotstopper {
Print &serialCliOutput() { return serialCliQueuePrint; }
}  // namespace shotstopper
#endif

uint32_t serialLogDroppedCount() {
#if !defined(SHOT_STOPPER_HOST_TEST)
  return __atomic_load_n(&serialLogQueueDropped, __ATOMIC_RELAXED);
#else
  return 0;
#endif
}

uint32_t serialLogTruncatedCount() {
#if !defined(SHOT_STOPPER_HOST_TEST)
  return __atomic_load_n(&serialLogQueueTruncated, __ATOMIC_RELAXED);
#else
  return 0;
#endif
}
