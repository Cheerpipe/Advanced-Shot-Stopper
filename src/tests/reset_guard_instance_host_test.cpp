#define OPEN_BREW_BY_WEIGHT_HOST_TEST

#include <stdint.h>

uint32_t hostSafetyResetReasonCode = 1;
bool hostSafetyResetReasonUnsafe = false;
bool hostSafetyResetReasonPowerOn = true;

#include "../OpenBrewByWeightResetGuard.h"

#include <iostream>

const volatile void *resetGuardAddressA();
const volatile void *resetGuardAddressB();
void resetGuardWriteFromA();
bool resetGuardReadFromB();

int main() {
  openbrewbyweight::resetSafetyResetGuardForHost();
  if (resetGuardAddressA() != resetGuardAddressB() ||
      resetGuardAddressA() != &openbrewbyweight::safetyResetRecordForHost()) {
    std::cerr << "reset guard record is not unique across translation units\n";
    return 1;
  }
  resetGuardWriteFromA();
  if (!resetGuardReadFromB()) {
    std::cerr << "reset guard write is not visible across translation units\n";
    return 1;
  }
  std::cout << "reset guard unique-instance host test passed\n";
  return 0;
}
