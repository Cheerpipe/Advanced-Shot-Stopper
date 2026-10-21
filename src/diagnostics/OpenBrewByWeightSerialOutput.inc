#if !defined(SHOT_STOPPER_HOST_TEST)
struct SerialLogLine {
  uint16_t length = 0;
  char text[256] = {};
};

constexpr UBaseType_t SERIAL_LOG_QUEUE_DEPTH = 8;
StaticQueue_t serialLogQueueStorage;
uint8_t *serialLogQueueBytes = nullptr;
uint8_t *serialCliOutputBytes = nullptr;
QueueHandle_t serialLogQueue = nullptr;
TaskHandle_t serialLogTaskHandle = nullptr;
uint32_t serialLogQueueDropped = 0;
uint32_t serialLogQueueTruncated = 0;
enum class SerialCliOutputState : uint8_t { FREE, WRITING, READY, READING };
std::atomic<SerialCliOutputState> serialCliOutputState{
    SerialCliOutputState::FREE};
size_t serialCliOutputLength = 0;
bool serialCliOutputOverflow = false;

void releaseSerialLogStorage() {
  if (serialLogQueue != nullptr) vQueueDelete(serialLogQueue);
  serialLogQueue = nullptr;
  heapCapsFree(serialCliOutputBytes);
  serialCliOutputBytes = nullptr;
  heapCapsFree(serialLogQueueBytes);
  serialLogQueueBytes = nullptr;
}

class SerialCliQueuePrint : public Print {
 public:
  size_t write(uint8_t value) override {
    if (serialCliOutputState.load(std::memory_order_relaxed) !=
            SerialCliOutputState::WRITING ||
        serialCliOutputBytes == nullptr) {
      return 1;
    }
    if (serialCliOutputLength < SERIAL_CLI_OUTPUT_CAPACITY) {
      serialCliOutputBytes[serialCliOutputLength++] = value;
    } else {
      serialCliOutputOverflow = true;
    }
    return 1;
  }

  size_t write(const uint8_t *buffer, size_t size) override {
    if (buffer == nullptr) return 0;
    for (size_t i = 0; i < size; ++i) write(buffer[i]);
    return size;
  }
};

SerialCliQueuePrint serialCliQueuePrint;

namespace shotstopper {
Print &serialCliOutput() { return serialCliQueuePrint; }
}  // namespace shotstopper

bool beginSerialCliOutput() {
  if (serialCliOutputBytes == nullptr) return false;
  SerialCliOutputState expected = SerialCliOutputState::FREE;
  if (!serialCliOutputState.compare_exchange_strong(
          expected, SerialCliOutputState::WRITING,
          std::memory_order_acquire, std::memory_order_relaxed)) {
    (void)__atomic_add_fetch(&serialLogQueueDropped, 1U, __ATOMIC_RELAXED);
    return false;
  }
  serialCliOutputLength = 0;
  serialCliOutputOverflow = false;
  return true;
}

void endSerialCliOutput() {
  if (serialCliOutputState.load(std::memory_order_relaxed) !=
      SerialCliOutputState::WRITING) {
    return;
  }
  if (serialCliOutputOverflow) {
    (void)__atomic_add_fetch(&serialLogQueueTruncated, 1U, __ATOMIC_RELAXED);
  }
  serialCliOutputState.store(serialCliOutputLength == 0
                                 ? SerialCliOutputState::FREE
                                 : SerialCliOutputState::READY,
                             std::memory_order_release);
}

bool drainSerialCliOutput() {
  SerialCliOutputState expected = SerialCliOutputState::READY;
  if (!serialCliOutputState.compare_exchange_strong(
          expected, SerialCliOutputState::READING,
          std::memory_order_acquire, std::memory_order_relaxed)) {
    return false;
  }
  if (serialCliOutputLength > 0) {
    (void)Serial.write(serialCliOutputBytes, serialCliOutputLength);
  }
  serialCliOutputState.store(SerialCliOutputState::FREE,
                             std::memory_order_release);
  return true;
}
#else
bool beginSerialCliOutput() { return true; }
void endSerialCliOutput() {}
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
