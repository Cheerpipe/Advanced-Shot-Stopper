#include "machine/ShotStopperLineaMicraSettings.h"
#include "machine/ShotStopperLineaMicraTypes.h"
#include "machine/ShotStopperMicraTiming.h"
#include "machine/ShotStopperMicraPowerState.h"

#include <cassert>
#include <cstring>

int main() {
  using namespace shotstopper;
  LineaMicraPersistedSettings settings;
  assert(settings.options == LINEA_MICRA_DEFAULT_OPTIONS);
  std::strcpy(settings.username, "barista@example.com");
  std::strcpy(settings.password, "correct horse battery staple");
  std::memset(settings.installationPrivateKey, 0x5a,
              sizeof(settings.installationPrivateKey));
  std::strcpy(settings.selectedSerial, "MR123456");
  std::strcpy(settings.selectedName, "Kitchen Micra");
  settings.accountConfigured = true;
  setLineaMicraOptions(settings, true, true, true);
  assert(validLineaMicraSettings(settings));
  disconnectLineaMicra(settings);
  assert(validLineaMicraSettings(settings));
  assert(settings.options == LINEA_MICRA_DEFAULT_OPTIONS);
  assert(settings.username[0] == '\0');
  assert(settings.password[0] == '\0');
  assert(settings.selectedSerial[0] == '\0');
  wipeLineaMicraSettings(settings);
  for (uint8_t byte : settings.installationPrivateKey) assert(byte == 0);
  assert(sizeof(LineaMicraRequest) <= 16);
  LineaMicraRequest temperatureRequest;
  temperatureRequest.type = LineaMicraRequestType::APPLY_TEMPERATURE;
  temperatureRequest.configGeneration = 7;
  temperatureRequest.presetId = 2;
  temperatureRequest.targetDeciC = 935;
  assert(temperatureRequest.type == LineaMicraRequestType::APPLY_TEMPERATURE);
  assert(temperatureRequest.targetDeciC == 935);
  assert(std::strcmp(lineaMicraTemperatureStateName(
                         LineaMicraTemperatureState::PENDING),
                     "pending") == 0);
  assert(std::strcmp(lineaMicraErrorName(LineaMicraError::REJECTED),
                     "rejected") == 0);
  assert(std::strcmp(lineaMicraErrorName(LineaMicraError::UNCONFIRMED),
                     "unconfirmed") == 0);
  assert(lineaMicraTemperatureHttpRetryable(0));
  assert(lineaMicraTemperatureHttpRetryable(401));
  assert(lineaMicraTemperatureHttpRetryable(408));
  assert(lineaMicraTemperatureHttpRetryable(425));
  assert(lineaMicraTemperatureHttpRetryable(429));
  assert(lineaMicraTemperatureHttpRetryable(500));
  assert(!lineaMicraTemperatureHttpRetryable(200));
  assert(!lineaMicraTemperatureHttpRetryable(302));
  assert(!lineaMicraTemperatureHttpRetryable(400));
  assert(!lineaMicraTemperatureHttpRetryable(403));
  assert(!lineaMicraTemperatureHttpRetryable(422));
  assert(lineaMicraTemperatureCycleRetryable(LineaMicraError::HTTP_ERROR));
  assert(lineaMicraTemperatureCycleRetryable(LineaMicraError::UNCONFIRMED));
  assert(!lineaMicraTemperatureCycleRetryable(LineaMicraError::INVALID_AUTH));
  assert(!lineaMicraTemperatureCycleRetryable(LineaMicraError::REJECTED));
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
  assert(micra_timing::kRetryDelaysMs[1] == 6000);
  assert(micra_timing::kRetryDelaysMs[2] == 9000);
  assert(micra_timing::kStatePollMs == 30000);
  assert(micra_timing::kOptimisticOnMs ==
         2U * micra_timing::kStatePollMs);
  assert(micra_timing::kPostWakeObservationDelayMs == 15000);
  assert(micra_timing::kPostWakeObservationDelayMs <
         micra_timing::kOptimisticOnMs);
  assert(!micra_timing::accessTokenRefreshDue(50U * 60U * 1000U - 1U));
  assert(micra_timing::accessTokenRefreshDue(50U * 60U * 1000U));
  assert(micra_timing::kAccessTokenLifetimeMs == 60U * 60U * 1000U);

  micra_timing::ObservationSchedule schedule;
  schedule.dueNow(100);
  assert(schedule.automaticDue(100));
  schedule.armPostWake(200);
  assert(!schedule.observationAllowed(15199));
  schedule.scheduleNext(300);  // A pre-edge completion preserves the deadline.
  assert(schedule.observationAllowed(15200));
  assert(schedule.automaticDue(15200));
  schedule.observationStarted(15200);
  schedule.scheduleNext(15200);
  assert(!schedule.automaticDue(45199));
  assert(schedule.automaticDue(45200));

  LineaMicraPowerStateTracker power;
  LineaMicraStatus authoritative;
  authoritative.sampleAtMs = 100;
  authoritative.powerState = LineaMicraPowerState::OFF;
  authoritative.quality = LineaMicraObservationQuality::CURRENT;
  authoritative.effectiveOn = false;
  const uint32_t preEdgeGeneration = power.generation();
  assert(power.notePhysicalStart(authoritative, true, true, 200));
  const uint32_t edgeGeneration = power.generation();
  assert(edgeGeneration != preEdgeGeneration);
  LineaMicraStatus effective = power.effectiveStatus(authoritative, true, 200);
  assert(effective.powerState == LineaMicraPowerState::ON);
  assert(effective.optimisticOn);
  assert(effective.quality == LineaMicraObservationQuality::OPTIMISTIC);
  assert(!power.notePhysicalStart(authoritative, true, true, 201));
  assert(power.generation() == edgeGeneration);
  assert(!power.acceptAuthoritative(preEdgeGeneration));
  assert(power.effectiveStatus(authoritative, true, 202).optimisticOn);
  assert(power.acceptAuthoritative(edgeGeneration));
  assert(power.effectiveStatus(authoritative, true, 203).powerState ==
         LineaMicraPowerState::OFF);

  authoritative.sampleAtMs = 1000;
  assert(!power.notePhysicalStart(authoritative, true, false, 1100));
  const uint32_t disabledWakeGeneration = power.generation();
  effective = power.effectiveStatus(
      authoritative, true, 1100 + micra_timing::kOptimisticOnMs);
  assert(effective.powerState == LineaMicraPowerState::UNKNOWN);
  assert(!effective.optimisticOn);
  assert(!power.notePhysicalStart(
      authoritative, true, true, 1100 + micra_timing::kOptimisticOnMs));
  assert(power.generation() == disabledWakeGeneration);
  assert(!power.notePhysicalStart(authoritative, false, true, 1101));
  return 0;
}
