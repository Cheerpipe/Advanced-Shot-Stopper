#pragma once

#include <stddef.h>
#include <string.h>

namespace shotstopper {

constexpr size_t DEVICE_NAME_CAPACITY = 64;
constexpr const char *DEFAULT_DEVICE_NAME = "shotstopper";

// A single DNS label; safe to publish in JSON and use as an HTTP hostname.
inline bool validDeviceName(const char *name) {
  if (name == nullptr) return false;
  const size_t length = strnlen(name, DEVICE_NAME_CAPACITY);
  if (length == 0 || length == DEVICE_NAME_CAPACITY ||
      name[0] == '-' || name[length - 1] == '-') return false;
  for (size_t i = 0; i < length; ++i) {
    const char c = name[i];
    if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-')
      return false;
  }
  return true;
}

}  // namespace shotstopper
