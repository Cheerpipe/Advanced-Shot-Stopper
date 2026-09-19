#include "machine/ShotStopperLineaMicraSettings.h"
#include "machine/ShotStopperLineaMicraTypes.h"
#include "machine/ShotStopperMicraTiming.h"

#include <cassert>
#include <cstring>

int main() {
  using namespace shotstopper;
  LineaMicraPersistedSettings settings;
  char token[64];
  std::memset(token, 'T', sizeof(token));
  assert(setLineaMicraToken(settings, token, sizeof(token)));
  assert(validLineaMicraSettings(settings));
  wipeLineaMicraSettings(settings);
  for (uint8_t byte : settings.token) assert(byte == 0);
  assert(sizeof(LineaMicraRequest) <= 16);
  assert(micra_timing::kMaxAttempts == 4);
  assert(micra_timing::kRetryDelaysMs[0] == 3000);
  assert(micra_timing::kRetryDelaysMs[2] == 9000);
  return 0;
}
