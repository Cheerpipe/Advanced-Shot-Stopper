#define SHOT_STOPPER_HOST_TEST
#define SHOT_STOPPER_PERSISTENCE_HOST_TEST
#define SHOT_STOPPER_OTA_HOST_TEST

#include "../ShotStopperOta.cpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

int main() {
  using namespace shotstopper;
  persistence_host::reset();
  ShotStopperOta &ota = ShotStopperOta::instance();
  ota.begin();

  std::atomic<bool> start{false};
  std::atomic<int> failures{0};
  auto waitForStart = [&]() {
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  };

  std::thread writer([&]() {
    waitForStart();
    OtaSessionIdentity identity;
    identity.size = 65536;
    for (int iteration = 0; iteration < 2000; ++iteration) {
      // Host builds deliberately carry arch=unknown, so this command must be
      // rejected without changing the published/session state.
      if (ota.createSession(identity, static_cast<uint32_t>(iteration)) !=
          OtaResult::NO_IDENTITY) {
        failures.fetch_add(1, std::memory_order_relaxed);
      }
      ota.discard();
      (void)ota.confirmRunningImage();
    }
  });

  std::thread observer([&]() {
    waitForStart();
    for (int iteration = 0; iteration < 10000; ++iteration) {
      const OtaPublishedState published = ota.publishedState();
      const OtaStatusSnapshot snapshot = ota.snapshot();
      if (!published.available || !published.confirmed || published.busy ||
          published.pendingVerify || published.rejected ||
          !snapshot.available || !snapshot.confirmed || snapshot.busy ||
          snapshot.sessionActive || snapshot.state != OtaState::IDLE) {
        failures.fetch_add(1, std::memory_order_relaxed);
      }
    }
  });

  start.store(true, std::memory_order_release);
  writer.join();
  observer.join();

  if (failures.load(std::memory_order_relaxed) != 0) {
    std::cerr << "OTA state concurrency failures: " << failures.load() << "\n";
    return EXIT_FAILURE;
  }
  std::cout << "OTA state publication and transitions are race-free\n";
  return EXIT_SUCCESS;
}
