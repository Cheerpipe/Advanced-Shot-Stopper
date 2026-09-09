#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace persistence_host {

inline std::map<std::string, std::vector<uint8_t>> records;
inline bool failNextWrite = false;
inline std::string failNextWriteForKey;
inline int32_t failNextWriteError = -1;
inline int32_t lastOperationError = 0;
inline bool corruptNextWrite = false;
inline uint32_t randomState = 0x13579BDFU;

inline std::string storageKey(const char *nameSpace, const char *key) {
  return std::string(nameSpace == nullptr ? "" : nameSpace) + "/" +
         (key == nullptr ? "" : key);
}

inline void reset() {
  records.clear();
  failNextWrite = false;
  failNextWriteForKey.clear();
  failNextWriteError = -1;
  lastOperationError = 0;
  corruptNextWrite = false;
  randomState = 0x13579BDFU;
}

inline bool corrupt(const char *nameSpace, const char *key, size_t offset) {
  auto found = records.find(storageKey(nameSpace, key));
  if (found == records.end() || offset >= found->second.size()) {
    return false;
  }
  found->second[offset] ^= 0x5AU;
  return true;
}

inline void putRaw(const char *nameSpace, const char *key, const void *data,
                   size_t length) {
  const auto *bytes = static_cast<const uint8_t *>(data);
  records[storageKey(nameSpace, key)] =
      std::vector<uint8_t>(bytes, bytes + length);
}

}  // namespace persistence_host

class PersistenceHostEEPROM {
 public:
  uint8_t read(size_t address) const { return bytes.at(address); }
  void write(size_t address, uint8_t value) { bytes.at(address) = value; }
  bool commit() { return true; }

  std::array<uint8_t, 2> bytes = {0xFF, 0xFF};
};

#if !defined(SHOT_STOPPER_HOST_STUBS_H)
inline PersistenceHostEEPROM EEPROM;
#endif

class Preferences {
 public:
  bool begin(const char *nameSpace, bool readOnly = false) {
    nameSpace_ = nameSpace == nullptr ? "" : nameSpace;
    readOnly_ = readOnly;
    active_ = true;
    return true;
  }

  void end() { active_ = false; }

  size_t getBytesLength(const char *key) const {
    if (!active_) return 0;
    const auto found = persistence_host::records.find(
        persistence_host::storageKey(nameSpace_.c_str(), key));
    return found == persistence_host::records.end() ? 0 : found->second.size();
  }

  size_t getBytes(const char *key, void *output, size_t capacity) const {
    if (!active_ || output == nullptr) return 0;
    const auto found = persistence_host::records.find(
        persistence_host::storageKey(nameSpace_.c_str(), key));
    if (found == persistence_host::records.end() ||
        capacity < found->second.size()) {
      return 0;
    }
    std::memcpy(output, found->second.data(), found->second.size());
    return found->second.size();
  }

  size_t putBytes(const char *key, const void *input, size_t length) {
    if (!active_ || readOnly_ || input == nullptr ||
        persistence_host::failNextWrite) {
      persistence_host::failNextWrite = false;
      persistence_host::lastOperationError =
          persistence_host::failNextWriteError;
      persistence_host::failNextWriteError = -1;
      return 0;
    }
    if (key != nullptr && !persistence_host::failNextWriteForKey.empty() &&
        persistence_host::failNextWriteForKey == key) {
      persistence_host::failNextWriteForKey.clear();
      persistence_host::lastOperationError =
          persistence_host::failNextWriteError;
      persistence_host::failNextWriteError = -1;
      return 0;
    }
    persistence_host::putRaw(nameSpace_.c_str(), key, input, length);
    if (persistence_host::corruptNextWrite && length > 0) {
      persistence_host::corruptNextWrite = false;
      auto &stored = persistence_host::records[
          persistence_host::storageKey(nameSpace_.c_str(), key)];
      stored[length - 1] ^= 0x5AU;
    }
    persistence_host::lastOperationError = 0;
    return length;
  }

  bool isKey(const char *key) const {
    if (!active_ || key == nullptr) {
      return false;
    }
    return persistence_host::records.find(
               persistence_host::storageKey(nameSpace_.c_str(), key)) !=
           persistence_host::records.end();
  }

  bool remove(const char *key) {
    if (!active_ || readOnly_ || key == nullptr) {
      return false;
    }
    return persistence_host::records.erase(
               persistence_host::storageKey(nameSpace_.c_str(), key)) > 0;
  }

  bool clear() {
    if (!active_ || readOnly_) return false;
    const std::string prefix = nameSpace_ + "/";
    for (auto record = persistence_host::records.begin();
         record != persistence_host::records.end();) {
      if (record->first.compare(0, prefix.size(), prefix) == 0) {
        record = persistence_host::records.erase(record);
      } else {
        ++record;
      }
    }
    return true;
  }

 private:
  std::string nameSpace_;
  bool readOnly_ = false;
  bool active_ = false;
};

inline void esp_fill_random(void *output, size_t length) {
  auto *bytes = static_cast<uint8_t *>(output);
  for (size_t index = 0; index < length; ++index) {
    persistence_host::randomState =
        persistence_host::randomState * 1664525U + 1013904223U;
    bytes[index] = static_cast<uint8_t>(persistence_host::randomState >> 24U);
  }
}

using psa_status_t = int32_t;
using psa_algorithm_t = uint32_t;
constexpr psa_status_t PSA_SUCCESS = 0;
constexpr psa_status_t PSA_ERROR_BAD_STATE = -1;
constexpr psa_algorithm_t PSA_ALG_SHA_256 = 0x02000009U;

struct psa_hash_operation_t {
  uint32_t hash = 2166136261U;
  bool active = false;
};
#define PSA_HASH_OPERATION_INIT psa_hash_operation_t{}

inline psa_status_t psa_hash_setup(psa_hash_operation_t *context,
                                   psa_algorithm_t algorithm) {
  if (context == nullptr || context->active || algorithm != PSA_ALG_SHA_256) {
    return PSA_ERROR_BAD_STATE;
  }
  context->hash = 2166136261U;
  context->active = true;
  return PSA_SUCCESS;
}

inline psa_status_t psa_hash_update(psa_hash_operation_t *context,
                                    const uint8_t *input, size_t length) {
  if (context == nullptr || !context->active) return PSA_ERROR_BAD_STATE;
  for (size_t index = 0; index < length; ++index) {
    context->hash = (context->hash ^ input[index]) * 16777619U;
  }
  return PSA_SUCCESS;
}

inline psa_status_t psa_hash_finish(psa_hash_operation_t *context,
                                    uint8_t *output, size_t outputSize,
                                    size_t *outputLength) {
  if (context == nullptr || !context->active || outputSize < 32 ||
      outputLength == nullptr) return PSA_ERROR_BAD_STATE;
  uint32_t value = context->hash;
  for (size_t index = 0; index < 32; ++index) {
    value = value * 1103515245U + 12345U;
    output[index] = static_cast<uint8_t>(value >> 24U);
  }
  *outputLength = 32;
  context->active = false;
  return PSA_SUCCESS;
}

inline psa_status_t psa_hash_abort(psa_hash_operation_t *context) {
  if (context != nullptr) context->active = false;
  return PSA_SUCCESS;
}
