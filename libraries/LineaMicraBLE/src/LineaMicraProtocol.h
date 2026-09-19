#pragma once

#include <cstddef>
#include <cstdint>

namespace lineamicra {

inline constexpr const char *kReadCharacteristicUuid =
    "0a0b7847-e12b-09a8-b04b-8e0922a9abab";
inline constexpr const char *kWriteCharacteristicUuid =
    "0b0b7847-e12b-09a8-b04b-8e0922a9abab";
inline constexpr const char *kAuthCharacteristicUuid =
    "0d0b7847-e12b-09a8-b04b-8e0922a9abab";
inline constexpr size_t kTokenLength = 64;
inline constexpr size_t kMaxResponseBytes = 512;

enum class Query : uint8_t { MACHINE_CAPABILITIES, BOILERS, MACHINE_MODE };
enum class Mode : uint8_t { STANDBY, BREWING, ECO, UNSUPPORTED };
enum class Error : uint8_t {
  NONE,
  INVALID_ARGUMENT,
  BUFFER_TOO_SMALL,
  OVERSIZED,
  MALFORMED,
  TOO_DEEP,
  WRONG_TYPE,
  DUPLICATE_FIELD,
  MISSING_FIELD,
  DUPLICATE_BOILER,
  INVALID_VALUE,
  OUT_OF_RANGE,
  UNSUPPORTED_MODEL
};

struct BrewBoiler {
  uint16_t currentDeciC = 0;
  uint16_t targetDeciC = 0;
  bool enabled = false;
};

struct CommandStatus {
  char id[32] = {};
  char message[64] = {};
  char status[16] = {};
  bool success = false;
};

bool validToken(const char *token, size_t length);
bool buildQuery(Query query, char *output, size_t capacity, size_t &payloadLength);
bool buildSetBrewTarget(uint16_t targetDeciC, char *output, size_t capacity,
                        size_t &payloadLength);
bool parseMachineCapabilities(const char *json, size_t length, Error &error);
bool parseBrewBoiler(const char *json, size_t length, BrewBoiler &boiler,
                     Error &error);
bool parseMachineMode(const char *json, size_t length, Mode &mode, Error &error);
bool parseCommandStatus(const char *json, size_t length, CommandStatus &status,
                        Error &error);

}  // namespace lineamicra
