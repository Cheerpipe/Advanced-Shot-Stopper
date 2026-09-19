#pragma once

#include "LineaMicraProtocol.h"

#include <cstddef>
#include <cstdint>

namespace lineamicra {

struct PeerAddress {
  uint8_t type = 0;
  uint8_t value[6] = {};
};

struct ClientConfig {
  uint32_t connectTimeoutMs = 3000;
  uint32_t operationTimeoutMs = 1000;
  uint32_t sessionTimeoutMs = 6000;
};

enum class ClientState : uint8_t {
  IDLE,
  CONNECTING,
  DISCOVERING,
  READY,
  OPERATING,
  DISCONNECTING,
  FAILED
};

enum class EventType : uint8_t {
  READY,
  AUTHENTICATED,
  RESPONSE,
  WRITE_COMPLETE,
  DISCONNECTED,
  ERROR
};

struct ClientEvent {
  EventType type = EventType::ERROR;
  uint32_t requestGeneration = 0;
  uint32_t sessionGeneration = 0;
  uint32_t operationGeneration = 0;
  int32_t status = 0;
  uint16_t payloadLength = 0;
  char payload[kMaxResponseBytes + 1] = {};
};

struct ClientHealth {
  uint32_t staleCallbacks = 0;
  uint32_t droppedEvents = 0;
  uint32_t rejectedResponses = 0;
  uint32_t mbufFailures = 0;
  uint16_t negotiatedMtu = 23;
};

class LineaMicraBLE {
 public:
  LineaMicraBLE();
  ~LineaMicraBLE();
  LineaMicraBLE(const LineaMicraBLE &) = delete;
  LineaMicraBLE &operator=(const LineaMicraBLE &) = delete;

  bool connect(const PeerAddress &peer, const ClientConfig &config,
               uint32_t requestGeneration);
  bool authenticate(const char *token, size_t length,
                    uint32_t requestGeneration);
  bool query(Query query, uint32_t requestGeneration);
  bool setBrewTarget(uint16_t targetDeciC, uint32_t requestGeneration);
  bool readCommandResult(uint32_t requestGeneration);
  void service();
  bool takeEvent(ClientEvent &event);
  void abort();
  void disconnect();

  ClientState state() const;
  bool connected() const;
  ClientHealth health() const;

 private:
  static constexpr size_t kStorageBytes = 1536;
  alignas(std::max_align_t) uint8_t storage_[kStorageBytes] = {};
};

}  // namespace lineamicra
