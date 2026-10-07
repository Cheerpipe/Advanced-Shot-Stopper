#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  if (ok && page == StatusPage::Settings) {
    const uint32_t sampleAgeMs = micraStatus.sampleAtMs == 0
                                     ? 0
                                     : static_cast<uint32_t>(millis() -
                                                              micraStatus.sampleAtMs);
    const bool sampleValid = micraStatus.sampleAtMs != 0 &&
        (micraStatus.quality == LineaMicraObservationQuality::CURRENT ||
         micraStatus.quality == LineaMicraObservationQuality::STALE ||
         micraStatus.quality == LineaMicraObservationQuality::UNSUPPORTED);
    ok = statusJsonAppend(
        &used,
        ",\"lineaMicra\":{\"accountConfigured\":%s,"
        "\"email\":\"%s\","
        "\"connectionType\":\"%s\","
        "\"selectedSerial\":\"%s\",\"selectedName\":\"%s\","
        "\"applyTemperature\":%s,\"observeState\":%s,"
        "\"recognizeWakeGesture\":%s,"
        "\"powerOnWithScale\":%s,"
        "\"shutdownWithScale\":%s,\"shutdownGraceSeconds\":%u,"
        "\"scaleOffWithMachine\":%s,"
        "\"boostScaleDetectionWhenOn\":%s,"
        "\"reduceScaleScanningWhenOff\":%s,"
        "\"requestId\":%lu,\"phase\":\"%s\",\"error\":\"%s\","
        "\"powerState\":\"%s\","
        "\"observedMode\":\"%s\",\"quality\":\"%s\",\"effectiveOn\":%s,"
        "\"powerSource\":\"%s\",\"freshnessPolicy\":\"%s\","
        "\"optimisticOn\":%s,\"optimisticOff\":%s,"
        "\"sampleValid\":%s,\"sampleAgeMs\":%lu,\"freshnessMs\":%lu,"
        "\"pollIntervalMs\":%lu,\"targetValid\":%s,\"targetDeciC\":%u,"
        "\"temperatureState\":\"%s\",\"temperatureError\":\"%s\","
        "\"temperatureCommandAccepted\":%s,\"temperatureRetryable\":%s,"
        "\"requestedTargetDeciC\":%u,\"appliedTargetDeciC\":%u,"
        "\"temperatureHttpStatus\":%u,"
        "\"temperatureTransportStatus\":%ld,"
        "\"staConnected\":%s,\"apActive\":%s,\"shotPaused\":%s,"
        "\"scalePaused\":%s,"
        "\"httpStatus\":%u,\"transportStatus\":%ld,\"machines\":[",
        micraAccountConfigured ? "true" : "false",
        safeMicraUsername,
        micraConnectionTypeName(micraConnectionType),
        safeMicraSerial, safeMicraName,
        (micraOptions & LINEA_MICRA_APPLY_TEMPERATURE) != 0
            ? "true" : "false",
        (micraOptions & LINEA_MICRA_OBSERVE_STATE) != 0
            ? "true" : "false",
        (micraOptions & LINEA_MICRA_RECOGNIZE_WAKE) != 0
            ? "true" : "false",
        (micraOptions & LINEA_MICRA_POWER_ON_WITH_SCALE) != 0
            ? "true" : "false",
        (micraOptions & LINEA_MICRA_SHUTDOWN_WITH_SCALE) != 0
            ? "true" : "false",
        static_cast<unsigned>(
            lineaMicraShutdownGraceSeconds(micraOptions)),
        (micraScaleOptions & LINEA_MICRA_SCALE_OFF_WITH_MACHINE) != 0
            ? "true" : "false",
        (micraScaleOptions & LINEA_MICRA_SCALE_SCAN_BOOST_WHEN_ON) != 0
            ? "true" : "false",
        (micraScaleOptions & LINEA_MICRA_SCALE_SCAN_RELAX_WHEN_OFF) != 0
            ? "true" : "false",
        static_cast<unsigned long>(micraStatus.requestId),
        lineaMicraPhaseName(micraStatus.phase),
        lineaMicraErrorName(micraStatus.error),
        lineaMicraPowerStateName(micraStatus.powerState),
        lineaMicraObservedModeName(micraStatus.observedMode),
        lineaMicraObservationQualityName(micraStatus.quality),
        micraStatus.effectiveOn ? "true" : "false",
        micraPowerSourceName(micraStatus.powerSource),
        micraStatus.connectionFreshness ? "connection" : "time",
        micraStatus.optimisticOn ? "true" : "false",
        micraStatus.optimisticOff ? "true" : "false",
        sampleValid ? "true" : "false",
        static_cast<unsigned long>(sampleAgeMs),
        static_cast<unsigned long>(micra_timing::kStateFreshnessMs),
        static_cast<unsigned long>(micra_timing::kStatePollMs),
        micraStatus.targetValid ? "true" : "false",
        static_cast<unsigned>(micraStatus.targetDeciC),
        lineaMicraTemperatureStateName(micraStatus.temperatureState),
        lineaMicraErrorName(micraStatus.temperatureError),
        micraStatus.temperatureCommandAccepted ? "true" : "false",
        micraStatus.temperatureRetryable ? "true" : "false",
        static_cast<unsigned>(micraStatus.requestedTargetDeciC),
        static_cast<unsigned>(micraStatus.appliedTargetDeciC),
        static_cast<unsigned>(micraStatus.temperatureHttpStatus),
        static_cast<long>(micraStatus.temperatureTransportStatus),
        micraStatus.staConnected ? "true" : "false",
        micraStatus.apActive ? "true" : "false",
        micraStatus.shotPaused ? "true" : "false",
        micraStatus.scalePaused ? "true" : "false",
        static_cast<unsigned>(micraStatus.httpStatus),
        static_cast<long>(micraStatus.transportStatus));
    for (uint8_t index = 0; ok && index < micraDiscovery.count; ++index) {
      char safeSerial[LINEA_MICRA_SERIAL_CAPACITY] = {};
      char safeName[LINEA_MICRA_NAME_CAPACITY] = {};
      sanitizeJsonEmbed(micraDiscovery.machines[index].serial, safeSerial,
                        sizeof(safeSerial));
      sanitizeJsonEmbed(micraDiscovery.machines[index].name, safeName,
                        sizeof(safeName));
      ok = statusJsonAppend(&used,
                            "%s{\"serial\":\"%s\",\"name\":\"%s\"}",
                            index == 0 ? "" : ",", safeSerial, safeName);
    }
    if (ok) ok = statusJsonAppend(&used, "]");
    if (ok) ok = statusJsonAppend(&used, "}");
  }
#endif
