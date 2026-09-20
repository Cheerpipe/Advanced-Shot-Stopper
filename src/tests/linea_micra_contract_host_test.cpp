#include "machine/ShotStopperLineaMicraSettings.h"
#include "machine/ShotStopperLineaMicraTypes.h"
#include "machine/ShotStopperMicraTiming.h"

#include <cassert>
#include <cstring>

int main() {
  using namespace shotstopper;
  LineaMicraPersistedSettings settings;
  std::strcpy(settings.username, "barista@example.com");
  std::strcpy(settings.password, "correct horse battery staple");
  std::memset(settings.installationPrivateKey, 0x5a,
              sizeof(settings.installationPrivateKey));
  std::strcpy(settings.selectedSerial, "MR123456");
  std::strcpy(settings.selectedName, "Kitchen Micra");
  settings.accountConfigured = true;
  setLineaMicraOptions(settings, true, true);
  assert(validLineaMicraSettings(settings));
  disconnectLineaMicra(settings);
  assert(validLineaMicraSettings(settings));
  assert(settings.options ==
         (LINEA_MICRA_APPLY_TEMPERATURE | LINEA_MICRA_OBSERVE_STATE));
  assert(settings.username[0] == '\0');
  assert(settings.password[0] == '\0');
  assert(settings.selectedSerial[0] == '\0');
  wipeLineaMicraSettings(settings);
  for (uint8_t byte : settings.installationPrivateKey) assert(byte == 0);
  assert(sizeof(LineaMicraRequest) <= 16);
  assert(lineaMicraPowerStateForMode(LineaMicraObservedMode::STANDBY) ==
         LineaMicraPowerState::OFF);
  assert(lineaMicraPowerStateForMode(LineaMicraObservedMode::BREWING) ==
         LineaMicraPowerState::ON);
  assert(lineaMicraPowerStateForMode(LineaMicraObservedMode::ECO) ==
         LineaMicraPowerState::UNKNOWN);
  assert(lineaMicraPowerStateForMode(LineaMicraObservedMode::UNSUPPORTED) ==
         LineaMicraPowerState::UNKNOWN);
  assert(micra_timing::kMaxAttempts == 4);
  assert(micra_timing::kRetryDelaysMs[0] == 3000);
  assert(micra_timing::kRetryDelaysMs[2] == 9000);
  return 0;
}
