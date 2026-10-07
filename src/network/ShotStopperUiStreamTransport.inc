// httpd is the sole caller/owner, including automatic WS control replies.
constexpr size_t kUiStreamHeaderMax = 14;
constexpr uint32_t kUiStreamHeaderWaitMs = 100, kUiStreamPayloadWaitMs = 500;
constexpr uint32_t kStatsStreamPaceMs = 100;
bool uiStreamSendCleanAbort = false;
const char *uiStreamSendKind = "control";

int uiStreamSendFailed(int fd, size_t done, size_t length, uint32_t started) {
  const int error = errno;
  uiStreamSendCleanAbort = done == 0 && length <= kUiStreamHeaderMax &&
      (error == EAGAIN || error == EWOULDBLOCK);
  static uint32_t failures = 0, loggedAt = 0;
  const uint32_t now = millis();
  if (++failures == 1 || now - loggedAt >= 1000) {
    loggedAt = now;
    ESP_LOGW("ui_stream", "%s fd=%d bytes=%u/%u ms=%lu errno=%d failures=%lu",
             uiStreamSendKind, fd, static_cast<unsigned>(done),
             static_cast<unsigned>(length), static_cast<unsigned long>(now - started),
             error, static_cast<unsigned long>(failures));
  }
  errno = error;
  return -1;
}

// IDF calls send_fn separately for header/payload and accepts short writes.
// Keep untouched headers deferrable; allow modem-sleep ACK gaps once committed.
int uiStreamSend(httpd_handle_t, int fd, const char *data, size_t length, int flags) {
  uiStreamSendCleanAbort = false;
  const uint32_t started = millis();
  const uint32_t budget = length <= kUiStreamHeaderMax
                              ? kUiStreamHeaderWaitMs : kUiStreamPayloadWaitMs;
  size_t done = 0;
  while (done < length) {
    const uint32_t age = millis() - started;
    if (age >= budget) {
      errno = EAGAIN;
      break;
    }
    fd_set writable;
    FD_ZERO(&writable);
    FD_SET(fd, &writable);
    timeval wait = {0, static_cast<suseconds_t>((budget - age) * 1000)};
    const int ready = select(fd + 1, nullptr, &writable, nullptr, &wait);
    if (ready < 0) {
      if (errno == EINTR) continue;
      break;
    }
    if (ready == 0) {
      errno = EAGAIN;
      break;
    }
    const int sent = send(fd, data + done, length - done, flags | MSG_DONTWAIT);
    if (sent > 0) done += static_cast<size_t>(sent);
    else if (sent == 0) {
      errno = ECONNRESET;
      break;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
  }
  return done == length ? static_cast<int>(done)
                        : uiStreamSendFailed(fd, done, length, started);
}
