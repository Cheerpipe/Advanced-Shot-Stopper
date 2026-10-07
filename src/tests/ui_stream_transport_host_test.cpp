#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>

namespace {
using httpd_handle_t = void *;
struct Result {
  int value;
  uint32_t delay = 0;
  int error = 0;
};
uint32_t now = 0;
std::deque<Result> readiness, writes;
char payload[24000]{};
size_t accepted = 0;
unsigned logs = 0;
std::string lastLog;
uint32_t millis() { return now; }

int testSelect(int count, fd_set *, fd_set *writable, fd_set *, timeval *wait) {
  assert(count == 2 && FD_ISSET(1, writable));
  const auto budget = static_cast<uint32_t>(wait->tv_sec * 1000 + wait->tv_usec / 1000);
  assert(budget <= 500);
  if (readiness.empty()) return 1;
  const Result next = readiness.front();
  readiness.pop_front();
  if (next.value == 0 || next.delay > budget) {
    now += budget;
    return 0;
  }
  now += next.delay;
  errno = next.error;
  return next.value;
}

int testSend(int fd, const char *data, size_t length, int flags) {
  assert(fd == 1 && data == payload + accepted && (flags & MSG_DONTWAIT));
  Result next{static_cast<int>(length)};
  if (!writes.empty()) { next = writes.front(); writes.pop_front(); }
  now += next.delay;
  errno = next.error;
  if (next.value > 0) {
    assert(static_cast<size_t>(next.value) <= length);
    accepted += static_cast<size_t>(next.value);
  }
  return next.value;
}

void testLog(const char *format, ...) {
  char line[256];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  lastLog = line;
  ++logs;
  errno = EIO;  // Logging must not overwrite the socket failure returned upstream.
}

#define select testSelect
#define send testSend
#define ESP_LOGW(tag, ...) testLog(__VA_ARGS__)
#include "network/ShotStopperUiStreamTransport.inc"
#undef ESP_LOGW
#undef send
#undef select

void reset() {
  now += 1000;
  readiness.clear(); writes.clear(); accepted = 0; errno = 0;
  uiStreamSendKind = "stats";
}
int transmit(size_t length) { return uiStreamSend(nullptr, 1, payload, length, 0); }
}  // namespace

int main() {
  reset();
  writes = {{2}, {2}};
  assert(transmit(4) == 4 && accepted == 4);

  reset();
  readiness = {{0}};
  uint32_t started = now;
  assert(transmit(4) == -1 && now - started == 100 && uiStreamSendCleanAbort);
  assert(errno == EAGAIN && lastLog.find("stats fd=1 bytes=0/4 ms=100") != std::string::npos);

  reset();
  readiness = {{1}, {0}}; writes = {{2}};
  assert(transmit(4) == -1 && accepted == 2 && !uiStreamSendCleanAbort);

  // A large single curve can cross the TCP buffer size and an ACK sleep gap.
  reset();
  readiness = {{1}, {1, 150}}; writes = {{16380}};
  started = now;
  assert(transmit(20480) == 20480 && accepted == 20480 && now - started == 150);
  assert(!uiStreamSendCleanAbort);

  reset();
  readiness = {{1, 400}};
  assert(transmit(8192) == 8192);

  reset();
  readiness = {{0}};
  started = now;
  assert(transmit(512) == -1 && now - started == 500 && !uiStreamSendCleanAbort);
  assert(errno == EAGAIN);  // Even zero payload progress follows a committed header.

  reset();
  readiness = {{1, 300}, {1, 300}}; writes = {{100}};
  started = now;
  assert(transmit(8192) == -1 && now - started == 500 && accepted == 100);
  assert(!uiStreamSendCleanAbort);  // Progress must not renew the deadline.

  for (int error : {EPIPE, ECONNRESET, ENOTCONN, EBADF, ENOTSOCK}) {
    reset(); writes = {{-1, 0, error}};
    assert(transmit(4) == -1 && errno == error && !uiStreamSendCleanAbort);
  }
  for (int error : {EINVAL, ENOMEM}) {
    reset(); readiness = {{-1, 0, error}};
    assert(transmit(4) == -1 && errno == error && !uiStreamSendCleanAbort);
  }

  reset();
  readiness = {{-1, 30, EINTR}, {1}, {1, 120}};
  writes = {{-1, 0, EAGAIN}};
  started = now;
  assert(transmit(8192) == 8192 && now - started == 150);

  reset(); writes = {{-1, 20, EINTR}, {-1, 20, EWOULDBLOCK}};
  assert(transmit(512) == 512);

  reset(); writes = {{0, 0, EAGAIN}};
  assert(transmit(512) == -1 && errno == ECONNRESET && !uiStreamSendCleanAbort);

  reset(); now = UINT32_MAX - 50; started = now;
  readiness = {{1, 400}};
  assert(transmit(512) == 512 && now - started == 400);

  reset(); readiness = {{0}};
  assert(transmit(512) == -1);
  const unsigned logged = logs;
  readiness = {{0}};
  assert(transmit(4) == -1 && logs == logged);
  now += 1000; readiness = {{0}};
  assert(transmit(4) == -1 && logs == logged + 1);
  assert(transmit(0) == 0 && !uiStreamSendCleanAbort);
  puts("UI stream transport: bounded writes, modem-sleep gaps, partial failures, errno and log throttling passed");
}
