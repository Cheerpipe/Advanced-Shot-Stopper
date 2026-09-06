#include "../ShotStopperWebhook.h"

#include <assert.h>
#include <stdint.h>

using shotstopper::configureWebhookHttpRequest;
using shotstopper::webhookClientMustRecreate;

namespace {

void testSuccessRunsEverySetter() {
  uint8_t calls = 0;
  const int32_t result = configureWebhookHttpRequest(
      [&]() { ++calls; return 0; }, [&]() { ++calls; return 0; },
      [&]() { ++calls; return 0; }, [&]() { ++calls; return 0; });
  assert(result == 0);
  assert(calls == 4);
}

void testFirstFailureStopsSetup() {
  for (uint8_t failureIndex = 0; failureIndex < 4; ++failureIndex) {
    uint8_t calls = 0;
    auto setter = [&]() {
      const uint8_t index = calls++;
      return index == failureIndex ? -100 - static_cast<int32_t>(index) : 0;
    };
    const int32_t result = configureWebhookHttpRequest(
        setter, setter, setter, setter);
    assert(result == -100 - static_cast<int32_t>(failureIndex));
    assert(calls == failureIndex + 1);
  }
}

void testPersistentClientRecreationPolicy() {
  assert(webhookClientMustRecreate(nullptr, "http://example.test/hook"));
  assert(webhookClientMustRecreate("", "http://example.test/hook"));
  assert(!webhookClientMustRecreate("http://example.test/hook",
                                    "http://example.test/hook"));
  assert(webhookClientMustRecreate("http://example.test/hook",
                                   "http://other.test/hook"));
}

}  // namespace

int main() {
  testSuccessRunsEverySetter();
  testFirstFailureStopsSetup();
  testPersistentClientRecreationPolicy();
  return 0;
}
