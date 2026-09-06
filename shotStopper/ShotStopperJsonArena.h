#pragma once

#include <cJSON.h>
#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace shotstopper {

// JSON bodies are already bounded by ShotStopperNetwork::REQUEST_BODY_CAPACITY.
// Keep an independent limit here so callers outside HTTP cannot accidentally
// hand an unbounded C string to cJSON.
constexpr size_t JSON_DOCUMENT_MAX_BYTES = 2047;
constexpr size_t JSON_DOCUMENT_MAX_DEPTH = 32;

namespace detail {

inline std::atomic<uint32_t> g_jsonLimitRejections{0};
inline thread_local bool g_jsonLimitRejectedRecently = false;

inline bool jsonNestingWithinLimit(const char *body, size_t length) {
  size_t depth = 0;
  bool inString = false;
  bool escaped = false;
  for (size_t i = 0; i < length; ++i) {
    const char c = body[i];
    if (inString) {
      if (escaped) {
        escaped = false;
      } else if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }
    if (c == '"') {
      inString = true;
    } else if (c == '{' || c == '[') {
      if (++depth > JSON_DOCUMENT_MAX_DEPTH) return false;
    } else if ((c == '}' || c == ']') && depth > 0) {
      --depth;
    }
  }
  return true;
}

inline void noteJsonLimitRejection() {
  g_jsonLimitRejectedRecently = true;
  g_jsonLimitRejections.fetch_add(1, std::memory_order_relaxed);
}

}  // namespace detail

// cJSON's allocator hooks are process-global and cannot safely select a parser
// arena. Deliberately leave its default allocator installed: every parse owns
// independent storage and concurrent callers cannot reset one another's data.
inline void initJsonParser() {}

inline cJSON *parseJsonDocument(const char *body) {
  detail::g_jsonLimitRejectedRecently = false;
  if (body == nullptr) return nullptr;

  const size_t length = strnlen(body, JSON_DOCUMENT_MAX_BYTES + 1);
  if (length > JSON_DOCUMENT_MAX_BYTES ||
      !detail::jsonNestingWithinLimit(body, length)) {
    detail::noteJsonLimitRejection();
    return nullptr;
  }
  return cJSON_ParseWithLengthOpts(body, length + 1, nullptr, 1);
}

inline uint32_t jsonDocumentLimitRejections() {
  return detail::g_jsonLimitRejections.load(std::memory_order_relaxed);
}

inline bool jsonDocumentLimitRejectedRecently() {
  return detail::g_jsonLimitRejectedRecently;
}

// Transitional diagnostic API. There is intentionally no JSON arena anymore.
inline bool jsonArenaIsExternal() { return false; }
inline bool jsonArenaExhaustedRecently() {
  return jsonDocumentLimitRejectedRecently();
}

}  // namespace shotstopper
