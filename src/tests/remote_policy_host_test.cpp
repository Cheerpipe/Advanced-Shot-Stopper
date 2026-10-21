#include "../OpenBrewByWeightDomain.h"

#include <cstdlib>

int main() {
  static_assert(!openbrewbyweight::REMOTE_MACHINE_CONTROL_ENABLED,
                "Remote machine control must remain opt-in by default");
  return openbrewbyweight::REMOTE_MACHINE_CONTROL_ENABLED ? EXIT_FAILURE
                                                 : EXIT_SUCCESS;
}
