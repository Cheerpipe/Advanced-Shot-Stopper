#define SHOT_STOPPER_HOST_TEST

#include "../ShotStopperJsonArena.h"

#include <atomic>
#include <cJSON.h>
#include <iostream>
#include <string>
#include <thread>

namespace {

int failures = 0;

#define CHECK(condition)                                              \
  do {                                                                \
    if (!(condition)) {                                               \
      std::cerr << __func__ << ":" << __LINE__                        \
                << ": check failed: " << #condition << "\n";          \
      ++failures;                                                     \
      return;                                                         \
    }                                                                 \
  } while (false)

void testValidAndInvalidDocumentsAreIndependent() {
  cJSON *const prior = shotstopper::parseJsonDocument("{\"value\":17}");
  CHECK(prior != nullptr);
  CHECK(shotstopper::parseJsonDocument("{not-json}") == nullptr);
  const cJSON *const value = cJSON_GetObjectItemCaseSensitive(prior, "value");
  CHECK(cJSON_IsNumber(value));
  CHECK(value->valueint == 17);
  cJSON_Delete(prior);
}

void testConcurrentParsesDoNotShareStorage() {
  std::atomic<int> ready{0};
  std::atomic<bool> release{false};
  std::atomic<int> threadErrors{0};
  std::atomic<cJSON *> roots[2] = {};
  std::atomic<cJSON *> values[2] = {};

  auto parse = [&](int index) {
    const char *body = index == 0 ? "{\"worker\":0}" : "{\"worker\":1}";
    cJSON *const root = shotstopper::parseJsonDocument(body);
    roots[index].store(root, std::memory_order_release);
    values[index].store(
        root == nullptr ? nullptr
                        : cJSON_GetObjectItemCaseSensitive(root, "worker"),
        std::memory_order_release);
    ready.fetch_add(1, std::memory_order_release);
    while (!release.load(std::memory_order_acquire)) std::this_thread::yield();
    if (root != nullptr) {
      const cJSON *const value = values[index].load(std::memory_order_acquire);
      if (!cJSON_IsNumber(value) || value->valueint != index)
        threadErrors.fetch_add(1, std::memory_order_relaxed);
      cJSON_Delete(root);
    }
  };

  std::thread first(parse, 0);
  std::thread second(parse, 1);
  while (ready.load(std::memory_order_acquire) != 2) std::this_thread::yield();
  const bool bothParsed =
      roots[0].load(std::memory_order_acquire) != nullptr &&
      roots[1].load(std::memory_order_acquire) != nullptr;
  const bool distinctRoots =
      roots[0].load(std::memory_order_acquire) !=
      roots[1].load(std::memory_order_acquire);
  const bool distinctValues =
      values[0].load(std::memory_order_acquire) !=
      values[1].load(std::memory_order_acquire);
  release.store(true, std::memory_order_release);
  first.join();
  second.join();
  CHECK(bothParsed);
  CHECK(distinctRoots);
  CHECK(distinctValues);
  CHECK(threadErrors.load(std::memory_order_relaxed) == 0);
}

void testDepthLimitRejectsWithoutDamagingPriorDocument() {
  cJSON *const prior = shotstopper::parseJsonDocument("{\"safe\":true}");
  CHECK(prior != nullptr);
  std::string deep;
  for (size_t i = 0; i <= shotstopper::JSON_DOCUMENT_MAX_DEPTH; ++i)
    deep.push_back('[');
  deep += '0';
  for (size_t i = 0; i <= shotstopper::JSON_DOCUMENT_MAX_DEPTH; ++i)
    deep.push_back(']');
  const uint32_t before = shotstopper::jsonDocumentLimitRejections();
  CHECK(shotstopper::parseJsonDocument(deep.c_str()) == nullptr);
  CHECK(shotstopper::jsonDocumentLimitRejections() == before + 1);
  CHECK(shotstopper::jsonDocumentLimitRejectedRecently());
  CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(prior, "safe")));
  cJSON_Delete(prior);
}

void testSizeLimitAndNullRejectCleanly() {
  std::string oversized(shotstopper::JSON_DOCUMENT_MAX_BYTES + 1, ' ');
  CHECK(shotstopper::parseJsonDocument(oversized.c_str()) == nullptr);
  CHECK(shotstopper::jsonDocumentLimitRejectedRecently());
  CHECK(shotstopper::parseJsonDocument(nullptr) == nullptr);
  CHECK(!shotstopper::jsonDocumentLimitRejectedRecently());
}

void testEscapedBracketsDoNotCountAsNesting() {
  cJSON *const root = shotstopper::parseJsonDocument(
      "{\"text\":\"[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[[\"}");
  CHECK(root != nullptr);
  cJSON_Delete(root);
}

}  // namespace

int main() {
  shotstopper::initJsonParser();
  testValidAndInvalidDocumentsAreIndependent();
  testConcurrentParsesDoNotShareStorage();
  testDepthLimitRejectsWithoutDamagingPriorDocument();
  testSizeLimitAndNullRejectCleanly();
  testEscapedBracketsDoNotCountAsNesting();
  if (failures != 0) {
    std::cerr << failures << " JSON parser host test(s) failed\n";
    return 1;
  }
  std::cout << "JSON parser host tests passed\n";
  return 0;
}
