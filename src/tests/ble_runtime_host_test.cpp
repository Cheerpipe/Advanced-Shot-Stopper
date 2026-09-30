#include "../../idf/components/ShotStopperBleRuntime/ShotStopperBleRuntime.cpp"

#include <iostream>

int main() {
  // Exercise both a ready runtime and the failed-start cleanup path while a
  // health reader is paused after borrowing the actual host-task handle.
  for (bool sync : {true, false}) {
    runtimeSync = sync;
    assert(shotStopperBleRuntimeStart(sync ? 1000 : 5) == sync);
    std::mutex pauseMutex;
    std::condition_variable pauseChanged;
    bool reading = false, release = false;
    runtimeBeforeStackRead = [&] {
      std::unique_lock<std::mutex> lock(pauseMutex);
      reading = true;
      pauseChanged.notify_all();
      pauseChanged.wait(lock, [&] { return release; });
    };
    ShotStopperBleHealth sample{};
    std::thread reader([&] { sample = shotStopperBleRuntimeHealth(); });
    {
      std::unique_lock<std::mutex> lock(pauseMutex);
      pauseChanged.wait(lock, [&] { return reading; });
    }
    const unsigned deletedBefore = runtimeDeleted;
    const unsigned readsBefore = runtimeReads;
    // Another sampler stays nonblocking and cannot borrow the retained task.
    (void)shotStopperBleRuntimeHealth();
    assert(runtimeReads == readsBefore);
    assert(!shotStopperBleRuntimeStop(1));
    assert(runtimeDeleted == deletedBefore);
    bool stopped = false;
    std::thread stopper([&] { stopped = shotStopperBleRuntimeStop(1000); });
    {
      std::lock_guard<std::mutex> lock(pauseMutex);
      release = true;
      pauseChanged.notify_all();
    }
    reader.join();
    stopper.join();
    runtimeBeforeStackRead = {};
    assert(stopped && runtimeDeleted == deletedBefore + 1);
    assert(sample.hostTaskStackHighWaterBytes == 512);
    const unsigned finalReads = runtimeReads;
    const auto final = shotStopperBleRuntimeHealth();
    assert(final.state == ShotStopperBleRuntimeState::Stopped);
    assert(final.hostTaskStackHighWaterBytes == 512);
    assert(runtimeReads == finalReads);
    assert(shotStopperBleRuntimeStop(0));
  }
  std::cout << "Native BLE runtime health/teardown lifetime tests passed\n";
}
