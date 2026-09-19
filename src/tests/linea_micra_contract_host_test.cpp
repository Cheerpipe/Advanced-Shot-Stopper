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
  assert(LineaMicraRequest{}.brewTargetDeciC ==
         LINEA_MICRA_BREW_TARGET_DEFAULT_DECI_C);
  assert(micra_timing::kMaxAttempts == 4);
  assert(micra_timing::kRetryDelaysMs[0] == 3000);
  assert(micra_timing::kRetryDelaysMs[2] == 9000);
  return 0;
}
