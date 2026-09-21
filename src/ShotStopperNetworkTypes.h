#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace shotstopper {

// Network identity and STA configuration types shared by persistence and the
// network service (keeps ShotStopperDomain.h within its line ceiling).

// User-facing device name advertised over mDNS. The stored form allows
// letters, digits, spaces, and hyphens; deviceNameToMdnsHost() derives the
// RFC 1123 host label announced as <host>.local.
constexpr size_t DEVICE_NAME_CAPACITY = 33;
constexpr char DEFAULT_DEVICE_NAME[] = "Open Brew by Weight";
static_assert(sizeof(DEFAULT_DEVICE_NAME) <= DEVICE_NAME_CAPACITY,
              "Default device name must fit its persisted capacity");

inline bool validDeviceName(const char *name) {
  if (name == nullptr) {
    return false;
  }
  const size_t length = strnlen(name, DEVICE_NAME_CAPACITY);
  if (length == 0 || length >= DEVICE_NAME_CAPACITY) {
    return false;
  }
  if (name[0] == ' ' || name[0] == '-' || name[length - 1] == ' ' ||
      name[length - 1] == '-') {
    return false;
  }
  for (size_t index = 0; index < length; ++index) {
    const char c = name[index];
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '-' || c == ' ')) {
      return false;
    }
  }
  return true;
}

// Maps a valid device name onto its mDNS host label: lowercase, spaces to
// hyphens, collapsed and trimmed hyphens. Returns the label length, or 0
// when the arguments are unusable. A valid name always yields length >= 1
// and well under the 63-byte DNS label limit.
inline size_t deviceNameToMdnsHost(char *out, size_t capacity,
                                   const char *name) {
  if (out == nullptr || name == nullptr || capacity == 0) {
    return 0;
  }
  size_t length = 0;
  bool previousHyphen = true;  // Trims a leading hyphen for free.
  for (size_t index = 0; name[index] != '\0' && index < DEVICE_NAME_CAPACITY;
       ++index) {
    char c = name[index];
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
    if (c == ' ') {
      c = '-';
    }
    if (c == '-') {
      if (previousHyphen) {
        continue;
      }
      previousHyphen = true;
    } else {
      previousHyphen = false;
    }
    if (length + 1 >= capacity) {
      return 0;
    }
    out[length++] = c;
  }
  while (length > 0 && out[length - 1] == '-') {
    --length;
  }
  out[length] = '\0';
  return length;
}

enum class StaIpMode : uint8_t { DHCP = 0, STATIC = 1 };

enum class StaConfigState : uint8_t { CONFIRMED = 0, PENDING = 1 };

}  // namespace shotstopper
