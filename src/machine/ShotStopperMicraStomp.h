#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace shotstopper {

// One PSRAM-owned accumulator. Network spans never escape the receive callback.
struct MicraStompFrame {
  char command[16] = {};
  char destination[128] = {};
  char subscription[40] = {};
  char version[8] = {};
  char heartbeat[32] = {};
  char *body = nullptr;
  size_t bodyLength = 0;
  uint32_t ingressAttempt = 0;
};

class MicraStompDecoder {
 public:
  static constexpr size_t kHeaderLimit = 1024;
  static constexpr size_t kBodyLimit = 16384;
  static constexpr size_t kCapacity = kHeaderLimit + kBodyLimit + 2;
  using Consumer = bool (*)(void *, const MicraStompFrame &);

  void reset() { used_ = 0; startedAtMs_ = 0; }
  uint32_t pendingSince() const { return used_ ? startedAtMs_ : 0; }
  bool pending() const { return used_ != 0; }
  bool incompleteExpired(uint32_t now) const {
    return used_ != 0 && now - startedAtMs_ >= 10000;
  }
  bool feed(const char *data, size_t length, uint32_t now,
            Consumer consume, void *context, uint32_t ingressAttempt = 0) {
    if (incompleteExpired(now)) return false;
    size_t frames = 0;
    while (length != 0) {
      if (used_ == 0) {
        while (length && (*data == '\n' || *data == '\r')) { ++data; --length; }
        if (!length) return true;
        startedAtMs_ = now;
        ingressAttempt_ = ingressAttempt;
      }
      if (used_ == kCapacity - 1) return false;
      bytes_[used_++] = *data++;
      --length;
      bytes_[used_] = '\0';
      // Header parsing is performed only at a possible delimiter/terminator.
      if (bytes_[used_ - 1] != '\n' && bytes_[used_ - 1] != '\0' &&
          used_ != kHeaderLimit + 1) continue;
      MicraStompFrame frame;
      size_t total = 0;
      const int result = parse(frame, total);
      if (result < 0) return false;
      if (result == 0) continue;
      frame.ingressAttempt = ingressAttempt_;
      if (++frames > 32 || !consume(context, frame)) return false;
      reset();
    }
    return !incompleteExpired(now);
  }

 private:
  static bool unescape(const char *begin, size_t length, char *out, size_t capacity) {
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
      char c = begin[i];
      if (c == '\0' || c == '\r') return false;
      if (c == '\\') {
        if (++i == length) return false;
        switch (begin[i]) {
          case 'n': c = '\n'; break;
          case 'r': c = '\r'; break;
          case 'c': c = ':'; break;
          case '\\': c = '\\'; break;
          default: return false;
        }
      }
      if (used + 1 >= capacity) return false;
      out[used++] = c;
    }
    out[used] = '\0';
    return true;
  }

  int parse(MicraStompFrame &frame, size_t &total) {
    size_t cursor = 0;
    const char *newline = static_cast<const char *>(memchr(bytes_, '\n', used_));
    if (!newline) return used_ > kHeaderLimit ? -1 : 0;
    size_t size = static_cast<size_t>(newline - bytes_);
    if (size && bytes_[size - 1] == '\r') --size;
    if (!size || size >= sizeof(frame.command)) return -1;
    if (memchr(bytes_, '\0', size) || memchr(bytes_, '\r', size)) return -1;
    memcpy(frame.command, bytes_, size);
    cursor = static_cast<size_t>(newline - bytes_) + 1;
    bool hasLength = false;
    size_t bodyLength = 0;
    for (;;) {
      newline = static_cast<const char *>(memchr(bytes_ + cursor, '\n', used_ - cursor));
      if (!newline) return used_ > kHeaderLimit ? -1 : 0;
      size = static_cast<size_t>(newline - (bytes_ + cursor));
      if (size && bytes_[cursor + size - 1] == '\r') --size;
      const size_t next = static_cast<size_t>(newline - bytes_) + 1;
      if (next > kHeaderLimit) return -1;
      if (!size) { cursor = next; break; }
      const char *colon = static_cast<const char *>(memchr(bytes_ + cursor, ':', size));
      if (!colon) return -1;
      char *key = key_, *value = value_;
      const size_t keySize = static_cast<size_t>(colon - (bytes_ + cursor));
      if (!keySize || !unescape(bytes_ + cursor, keySize, key, sizeof(key_)) ||
          !unescape(colon + 1, size - keySize - 1, value, sizeof(value_))) return -1;
      char *target = nullptr;
      size_t capacity = 0;
      if (strcmp(key, "destination") == 0) { target = frame.destination; capacity = sizeof(frame.destination); }
      else if (strcmp(key, "subscription") == 0) { target = frame.subscription; capacity = sizeof(frame.subscription); }
      else if (strcmp(key, "version") == 0) { target = frame.version; capacity = sizeof(frame.version); }
      else if (strcmp(key, "heart-beat") == 0) { target = frame.heartbeat; capacity = sizeof(frame.heartbeat); }
      else if (strcmp(key, "content-length") == 0) {
        if (hasLength || !value[0]) return -1;
        hasLength = true;
        for (const char *p = value; *p; ++p) {
          if (*p < '0' || *p > '9' || bodyLength > (kBodyLimit - (*p - '0')) / 10) return -1;
          bodyLength = bodyLength * 10 + (*p - '0');
        }
      }
      if (target) {
        if (!value[0] || target[0] || strlen(value) >= capacity) return -1;
        memcpy(target, value, strlen(value) + 1);
      }
      cursor = next;
    }
    if (hasLength) {
      total = cursor + bodyLength + 1;
      if (used_ < total) return 0;
      if (used_ != total || bytes_[total - 1] != '\0') return -1;
    } else {
      const char *end = static_cast<const char *>(memchr(bytes_ + cursor, '\0', used_ - cursor));
      if (!end) return used_ - cursor > kBodyLimit ? -1 : 0;
      bodyLength = static_cast<size_t>(end - (bytes_ + cursor));
      total = cursor + bodyLength + 1;
      if (bodyLength > kBodyLimit) return -1;
    }
    frame.body = bytes_ + cursor;
    frame.bodyLength = bodyLength;
    return 1;
  }

  size_t used_ = 0;
  uint32_t startedAtMs_ = 0;
  uint32_t ingressAttempt_ = 0;
  char bytes_[kCapacity] = {};
  char key_[kHeaderLimit + 1] = {}, value_[kHeaderLimit + 1] = {};
};
}  // namespace shotstopper
