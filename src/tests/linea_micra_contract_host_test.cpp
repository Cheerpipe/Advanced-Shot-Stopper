#include "machine/ShotStopperLineaMicraSettings.h"
#include "machine/ShotStopperLineaMicraTypes.h"
#include "machine/ShotStopperMicraPowerState.h"
#include "machine/ShotStopperMicraScaleShutdown.h"
#include "machine/ShotStopperMicraTiming.h"

#include <cassert>
#include <cstring>

int main() {
  using namespace shotstopper;
  LineaMicraPersistedSettings settings;
  assert(settings.options == LINEA_MICRA_DEFAULT_OPTIONS);
  assert((LINEA_MICRA_DEFAULT_OPTIONS &
          LINEA_MICRA_SHUTDOWN_WITH_SCALE) == 0);
  assert(LINEA_MICRA_KNOWN_OPTIONS ==
         (LINEA_MICRA_DEFAULT_OPTIONS | LINEA_MICRA_SHUTDOWN_WITH_SCALE |
          LINEA_MICRA_SHUTDOWN_GRACE_MASK));
  std::strcpy(settings.username, "barista@example.com");
  std::strcpy(settings.password, "correct horse battery staple");
  std::memset(settings.installationPrivateKey, 0x5a,
              sizeof(settings.installationPrivateKey));
  std::strcpy(settings.selectedSerial, "MR123456");
  std::strcpy(settings.selectedName, "Kitchen Micra");
  settings.accountConfigured = true;
  setLineaMicraOptions(settings, true, true, true, false, 0);
  assert(validLineaMicraSettings(settings));
  assert((settings.options & LINEA_MICRA_SHUTDOWN_WITH_SCALE) == 0);
  assert(lineaMicraShutdownGraceSeconds(settings.options) == 0);
  setLineaMicraOptions(settings, true, true, true, true, 4);
  assert((settings.options & LINEA_MICRA_SHUTDOWN_WITH_SCALE) != 0);
  assert(lineaMicraShutdownGraceSeconds(settings.options) == 60);
  assert(lineaMicraShutdownGraceCode(settings.options) == 4);
  assert(validLineaMicraSettings(settings));
  setLineaMicraOptions(settings, true, true, true, true, 5);
  assert(lineaMicraShutdownGraceCode(settings.options) == 0);
  settings.options |= static_cast<uint8_t>(5U)
                      << LINEA_MICRA_SHUTDOWN_GRACE_SHIFT;
  assert(!validLineaMicraSettings(settings));
  settings.options &= ~LINEA_MICRA_SHUTDOWN_GRACE_MASK;
  settings.options |= static_cast<uint8_t>(1U << 7);
  assert(!validLineaMicraSettings(settings));
  setLineaMicraOptions(settings, true, true, true, true, 2);
  assert(validLineaMicraSettings(settings));
  disconnectLineaMicra(settings);
  assert(validLineaMicraSettings(settings));
  assert(settings.options ==
         (LINEA_MICRA_DEFAULT_OPTIONS | LINEA_MICRA_SHUTDOWN_WITH_SCALE |
          (2U << LINEA_MICRA_SHUTDOWN_GRACE_SHIFT)));
  assert(settings.username[0] == '\0');
  assert(settings.password[0] == '\0');
  assert(settings.selectedSerial[0] == '\0');
  wipeLineaMicraSettings(settings);
  for (uint8_t byte : settings.installationPrivateKey) assert(byte == 0);
  assert(sizeof(LineaMicraRequest) <= 16);
  assert(sizeof(LineaMicraPersistedSettings) == 310);
  LineaMicraRequest temperatureRequest;
  temperatureRequest.type = LineaMicraRequestType::APPLY_TEMPERATURE;
  temperatureRequest.configGeneration = 7;
  temperatureRequest.presetId = 2;
  temperatureRequest.targetDeciC = 935;
  assert(temperatureRequest.type == LineaMicraRequestType::APPLY_TEMPERATURE);
  assert(temperatureRequest.targetDeciC == 935);
  LineaMicraRequest standbyRequest;
  standbyRequest.type = LineaMicraRequestType::SET_STANDBY;
  assert(standbyRequest.type == LineaMicraRequestType::SET_STANDBY);
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
  assert(micra_timing::kStateFreshnessMs == micra_timing::kStatePollMs);
  assert(micra_timing::kOptimisticOverlayMs ==
         2U * micra_timing::kStatePollMs);
  assert(micra_timing::kExhaustedCooldownMs ==
         micra_timing::kOptimisticOverlayMs);
  assert(micra_timing::kPostWakeObservationDelayMs == 15000);
  assert(micra_timing::kPostWakeObservationDelayMs <
         micra_timing::kOptimisticOverlayMs);
  assert(!micra_timing::accessTokenRefreshDue(50U * 60U * 1000U - 1U));
  assert(micra_timing::accessTokenRefreshDue(50U * 60U * 1000U));
  assert(micra_timing::kAccessTokenLifetimeMs == 60U * 60U * 1000U);

  micra_timing::ObservationSchedule schedule;
  schedule.dueNow(100);
  assert(schedule.automaticDue(100));
  schedule.armPostEvent(200);
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
  assert(effective.powerState == LineaMicraPowerState::OFF);
  assert(effective.optimisticOn);
  assert(effective.effectiveOn);
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
  effective = power.effectiveStatus(authoritative, true, 1100);
  assert(effective.powerState == LineaMicraPowerState::OFF);
  assert(!effective.optimisticOn);
  assert(!effective.effectiveOn);

  const uint32_t freshness = micra_timing::kStateFreshnessMs;
  effective = power.effectiveStatus(authoritative, true, 1000 + freshness - 1);
  assert(effective.powerState == LineaMicraPowerState::OFF);
  assert(effective.quality == LineaMicraObservationQuality::CURRENT);
  effective = power.effectiveStatus(authoritative, true, 1000 + freshness);
  assert(effective.powerState == LineaMicraPowerState::OFF);
  assert(effective.quality == LineaMicraObservationQuality::STALE);
  assert(!effective.effectiveOn);
  // A stale sample keeps the last confirmed state: stale OFF still qualifies
  // the wake gesture and starts the optimistic ON overlay.
  assert(power.notePhysicalStart(authoritative, true, true,
                                 1000 + freshness + 1));
  assert(power.generation() != disabledWakeGeneration);
  effective = power.effectiveStatus(authoritative, true, 1000 + freshness + 1);
  assert(effective.powerState == LineaMicraPowerState::OFF);
  assert(effective.optimisticOn);
  assert(effective.effectiveOn);
  assert(effective.quality == LineaMicraObservationQuality::OPTIMISTIC);
  assert(!power.notePhysicalStart(authoritative, true, true,
                                  1000 + freshness + 2));
  power.reset();

  authoritative.powerState = LineaMicraPowerState::ON;
  authoritative.effectiveOn = true;
  effective = power.effectiveStatus(authoritative, true, 1000 + freshness + 3);
  assert(effective.powerState == LineaMicraPowerState::ON);
  assert(effective.quality == LineaMicraObservationQuality::STALE);
  // Stale ON qualifies the opposite overlay too: an accepted scale shutdown
  // over a stale confirmed ON asserts optimistic OFF.
  assert(power.noteStandbyCommandAccepted(authoritative, true,
                                          1000 + freshness + 4));
  effective = power.effectiveStatus(authoritative, true, 1000 + freshness + 4);
  assert(effective.powerState == LineaMicraPowerState::ON);
  assert(effective.optimisticOff);
  assert(!effective.effectiveOn);
  assert(effective.quality == LineaMicraObservationQuality::OPTIMISTIC);
  power.reset();
  effective = power.effectiveStatus(authoritative, true, 1000 + freshness + 4);
  assert(effective.powerState == LineaMicraPowerState::ON);
  assert(effective.quality == LineaMicraObservationQuality::STALE);
  assert(effective.effectiveOn);

  // Optimistic OFF mirrors the wake overlay: an accepted scale-shutdown
  // command over a fresh confirmed ON asserts effective OFF until the first
  // authoritative read accepted by generation replaces it.
  authoritative.sampleAtMs = 2000;
  const uint32_t preShutdownGeneration = power.generation();
  assert(power.noteStandbyCommandAccepted(authoritative, true, 2100));
  const uint32_t shutdownGeneration = power.generation();
  assert(shutdownGeneration != preShutdownGeneration);
  effective = power.effectiveStatus(authoritative, true, 2100);
  assert(effective.powerState == LineaMicraPowerState::ON);
  assert(effective.optimisticOff);
  assert(!effective.optimisticOn);
  assert(!effective.effectiveOn);
  assert(effective.quality == LineaMicraObservationQuality::OPTIMISTIC);
  assert(!power.noteStandbyCommandAccepted(authoritative, true, 2101));
  assert(power.generation() == shutdownGeneration);
  assert(!power.acceptAuthoritative(preShutdownGeneration));
  assert(power.effectiveStatus(authoritative, true, 2102).optimisticOff);
  assert(power.acceptAuthoritative(shutdownGeneration));
  effective = power.effectiveStatus(authoritative, true, 2103);
  assert(effective.powerState == LineaMicraPowerState::ON);
  assert(!effective.optimisticOff);
  assert(effective.effectiveOn);
  const uint32_t overlayMs = micra_timing::kOptimisticOverlayMs;
  authoritative.sampleAtMs = 2104;
  assert(power.noteStandbyCommandAccepted(authoritative, true, 2200));
  effective = power.effectiveStatus(authoritative, true, 2200 + overlayMs);
  assert(!effective.optimisticOff);
  assert(effective.effectiveOn);
  // The overlay expires without a successful read: the confirmed ON shows
  // again, already stale because no observation ran during the overlay.
  assert(effective.quality == LineaMicraObservationQuality::STALE);
  // Clear the expired overlay so later preconditions see a clean tracker.
  power.reset();

  authoritative.sampleAtMs = 0;
  effective = power.effectiveStatus(authoritative, true, 1000 + freshness);
  assert(effective.powerState == LineaMicraPowerState::UNKNOWN);
  assert(effective.effectiveOn);
  authoritative.quality = LineaMicraObservationQuality::UNCONFIGURED;
  effective = power.effectiveStatus(authoritative, false, 1000 + freshness);
  assert(effective.powerState == LineaMicraPowerState::UNKNOWN);
  assert(effective.quality == LineaMicraObservationQuality::UNCONFIGURED);
  authoritative.sampleAtMs = 1000;
  authoritative.powerState = LineaMicraPowerState::UNKNOWN;
  authoritative.quality = LineaMicraObservationQuality::UNSUPPORTED;
  effective = power.effectiveStatus(authoritative, true, 1000 + freshness);
  assert(effective.powerState == LineaMicraPowerState::UNKNOWN);
  assert(effective.quality == LineaMicraObservationQuality::UNSUPPORTED);
  assert(!power.notePhysicalStart(authoritative, false, true, 1101));

  const uint8_t shutdownOptions15s =
      LINEA_MICRA_DEFAULT_OPTIONS | LINEA_MICRA_SHUTDOWN_WITH_SCALE |
      static_cast<uint8_t>(2U << LINEA_MICRA_SHUTDOWN_GRACE_SHIFT);
  const uint8_t shutdownOptionsImmediate =
      LINEA_MICRA_DEFAULT_OPTIONS | LINEA_MICRA_SHUTDOWN_WITH_SCALE;
  MicraScaleShutdownTracker::Snapshot scale;
  MicraScaleShutdownTracker shutdown;

  // Option off: the explicit power-off is ignored.
  scale.disconnectSequence = 1;
  scale.disconnectReason = LINEA_MICRA_SCALE_EXPLICIT_DISCONNECT;
  assert(!shutdown.service(1000, scale, LINEA_MICRA_DEFAULT_OPTIONS, true));
  assert(!shutdown.service(60000, scale, LINEA_MICRA_DEFAULT_OPTIONS, true));
  // No cloud account: ignored even with the option on.
  assert(!shutdown.service(1000, scale, shutdownOptions15s, false));
  assert(!shutdown.service(60000, scale, shutdownOptions15s, false));
  // Radio silence (supervision timeout) never triggers the shutdown.
  scale.disconnectSequence = 2;
  scale.disconnectReason = 14;
  assert(!shutdown.service(2000, scale, shutdownOptions15s, true));
  assert(!shutdown.service(60000, scale, shutdownOptions15s, true));
  // Grace 15 s: armed, held before the deadline, fires once after it.
  scale.disconnectSequence = 3;
  scale.disconnectReason = LINEA_MICRA_SCALE_EXPLICIT_DISCONNECT;
  assert(shutdown.pending() == false);
  assert(!shutdown.service(10000, scale, shutdownOptions15s, true));
  assert(shutdown.pending());
  assert(!shutdown.service(24999, scale, shutdownOptions15s, true));
  assert(shutdown.pending());
  assert(shutdown.service(25000, scale, shutdownOptions15s, true));
  assert(!shutdown.pending());
  assert(!shutdown.service(25001, scale, shutdownOptions15s, true));
  // Grace OFF: fires immediately on the observed event.
  scale.disconnectSequence = 4;
  assert(shutdown.service(30000, scale, shutdownOptionsImmediate, true));
  assert(!shutdown.service(30001, scale, shutdownOptionsImmediate, true));
  // Scale back online inside the window cancels the shutdown.
  scale.disconnectSequence = 5;
  assert(!shutdown.service(40000, scale, shutdownOptions15s, true));
  scale.linkUp = true;
  assert(!shutdown.service(45000, scale, shutdownOptions15s, true));
  assert(!shutdown.pending());
  scale.linkUp = false;
  assert(!shutdown.service(60000, scale, shutdownOptions15s, true));
  assert(!shutdown.pending());
  // Relay closed at the event: ignored completely, never deferred.
  scale.disconnectSequence = 6;
  scale.relayClosed = true;
  assert(!shutdown.service(70000, scale, shutdownOptions15s, true));
  scale.relayClosed = false;
  assert(!shutdown.service(90000, scale, shutdownOptions15s, true));
  assert(!shutdown.pending());
  // Relay closing during the grace window cancels at fire time.
  scale.disconnectSequence = 7;
  assert(!shutdown.service(100000, scale, shutdownOptions15s, true));
  scale.relayClosed = true;
  assert(!shutdown.service(115000, scale, shutdownOptions15s, true));
  assert(!shutdown.pending());
  scale.relayClosed = false;
  // Option disabled mid-grace cancels.
  scale.disconnectSequence = 8;
  assert(!shutdown.service(120000, scale, shutdownOptions15s, true));
  assert(!shutdown.service(121000, scale, LINEA_MICRA_DEFAULT_OPTIONS, true));
  assert(!shutdown.pending());
  assert(!shutdown.service(140000, scale, shutdownOptions15s, true));
  return 0;
}
