#include "../ShotStopperDeviceName.h"
#include <atomic>
#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <string>

using namespace shotstopper;
static constexpr int ESP_OK = 0;
static int calls = 0, inits = 0, frees = 0, failAt = 0;
static bool allocated = false;
static std::string hostname, instance;
static std::function<void(int)> duringCall;
static int sdkCall() {
  ++calls;
  if (duringCall) duringCall(calls);
  return calls == failAt ? -1 : ESP_OK;
}
static int mdns_init() {
  ++inits;
  const int result = sdkCall();
  allocated = result == ESP_OK;
  return result;
}
static int mdns_hostname_set(const char *name) {
  hostname = name;
  return sdkCall();
}
static int mdns_instance_name_set(const char *name) {
  instance = name;
  return sdkCall();
}
static int mdns_service_add(const char *, const char *service,
                            const char *proto, int port, void *, int) {
  assert(std::string(service) == "_http" && std::string(proto) == "_tcp");
  assert(port == 80);
  return sdkCall();
}
static void mdns_free() {
  assert(allocated);
  allocated = false;
  ++frees;
}
struct TaskLockGuard { explicit TaskLockGuard(int &) {} };
static void copyCString(char *out, size_t size, const char *name) {
  assert(strlen(name) < size);
  strcpy(out, name);
}
class ShotStopperNetwork {
 public:
  std::atomic<uint32_t> rfGateGeneration_{0};
  std::atomic<bool> scaleConnecting_{false};
  bool brew = false, restartPending_ = false, mdnsStarted_ = false;
  void *server_ = this;
  uint32_t mdnsRetryAtMs_ = 0;
  int dataMux_ = 0;
  struct { char deviceName[DEVICE_NAME_CAPACITY] = "coffee-bar"; } status_;
  bool brewRfActive() const { return brew; }
  void lifecycleLog(const char *) {}
  void serviceMdns(uint32_t now, bool connected);
  void stopMdns();
};
#include "../network/ShotStopperMdns.inc"

static void resetSdk() {
  assert(!allocated);
  calls = inits = frees = failAt = 0;
  duringCall = {};
}
int main() {
  // Missing prerequisites, one initialization, pause/resume, and idempotent stop.
  ShotStopperNetwork n;
  n.serviceMdns(0, false);
  assert(inits == 0);
  n.server_ = nullptr;
  n.serviceMdns(0, true);
  assert(inits == 0);
  n.server_ = &n;
  n.serviceMdns(0, true);
  n.serviceMdns(100, true);
  assert(inits == 1 && hostname == "coffee-bar" && instance == hostname);
  n.brew = true;
  n.serviceMdns(200, true);
  assert(!allocated && frees == 1);
  n.brew = false;
  n.scaleConnecting_ = true;
  n.serviceMdns(6000, true);
  assert(inits == 1);
  n.scaleConnecting_ = false;
  n.serviceMdns(6000, true);
  n.restartPending_ = true;
  n.serviceMdns(6001, true);
  assert(!allocated && frees == 2);
  n.stopMdns();
  assert(frees == 2);

  // Every failed SDK step rolls back only resources successfully acquired.
  for (int failure = 1; failure <= 4; ++failure) {
    resetSdk();
    ShotStopperNetwork failed;
    failAt = failure;
    failed.serviceMdns(100, true);
    assert(!allocated && frees == (failure == 1 ? 0 : 1));
    failed.serviceMdns(5099, true);
    assert(inits == 1);
    failAt = 0;
    failed.serviceMdns(5100, true);
    assert(inits == 2 && allocated);
    failed.serviceMdns(5101, false);
    assert(!allocated);
  }
  // A short critical interval during ANY SDK step must invalidate startup.
  for (int step = 1; step <= 4; ++step) {
    resetSdk();
    ShotStopperNetwork raced;
    duringCall = [&](int call) { if (call == step) ++raced.rfGateGeneration_; };
    raced.serviceMdns(100, true);
    assert(!allocated && !raced.mdnsStarted_);
  }
  resetSdk();
  ShotStopperNetwork wrapped;
  failAt = 1;
  wrapped.serviceMdns(UINT32_MAX - 1000, true);
  wrapped.serviceMdns(3000, true);
  assert(inits == 1);
  failAt = 0;
  wrapped.serviceMdns(4000, true);
  assert(allocated);
  wrapped.stopMdns();
}
