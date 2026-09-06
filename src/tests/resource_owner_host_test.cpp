#include "../ShotStopperResourceOwner.h"

#include <cstdlib>
#include <iostream>
#include <utility>

namespace {

int cleanupCount = 0;
int lastCleaned = 0;

struct CountingDeleter {
  void operator()(int handle) const {
    ++cleanupCount;
    lastCleaned = handle;
  }
};

using Owner = shotstopper::UniqueResource<int, CountingDeleter>;

bool expect(bool condition, const char *message) {
  if (condition) return true;
  std::cerr << message << '\n';
  return false;
}

}  // namespace

int main() {
  static_assert(sizeof(Owner) == sizeof(int),
                "empty deleter must not increase owner size");
  bool ok = true;
  {
    Owner first(11);
    Owner second(std::move(first));
    ok &= expect(!first && second.get() == 11,
                 "move must transfer unique ownership");
    second.reset(22);
    ok &= expect(cleanupCount == 1 && lastCleaned == 11,
                 "reset must clean the replaced handle exactly once");
    const int released = second.release();
    ok &= expect(released == 22 && !second && cleanupCount == 1,
                 "release must transfer without cleanup");
    Owner rollback(released);
  }
  ok &= expect(cleanupCount == 2 && lastCleaned == 22,
               "scope rollback must clean the final owned handle");

  Owner left(31);
  Owner right(41);
  right = std::move(left);
  ok &= expect(!left && right.get() == 31 && cleanupCount == 3 &&
                   lastCleaned == 41,
               "move assignment must first clean the previous owner");
  right.reset();
  ok &= expect(cleanupCount == 4 && lastCleaned == 31,
               "explicit reset must be idempotent");
  right.reset();
  ok &= expect(cleanupCount == 4, "empty reset must not invoke deleter");

  if (!ok) return EXIT_FAILURE;
  std::cout << "UniqueResource move/release/rollback tests passed\n";
  return EXIT_SUCCESS;
}
