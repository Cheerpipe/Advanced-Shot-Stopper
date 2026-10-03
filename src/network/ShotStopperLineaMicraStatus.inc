#if SHOT_STOPPER_MACHINE_INTEGRATION == SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD
  if (ok && (page == StatusPage::Home || page == StatusPage::Settings ||
             page == StatusPage::Diagnostic)) {
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
    if (ok && page == StatusPage::Diagnostic) {
      const MicraWebSocketStatus ws = machineIntegrationWebSocketStatus();
      const uint32_t now = millis();
      char label[6 * sizeof(ws.cleaningLabel)] = {};
      escapeJsonString(ws.cleaningLabel, label, sizeof(label));
      const char *cleaning = ws.cleaning == MicraCleaningState::OFF ? "inactive"
          : ws.cleaning == MicraCleaningState::REQUESTED ? "waiting_for_paddle"
          : ws.cleaning == MicraCleaningState::CLEANING ? "cleaning" : "unknown";
      ok = statusJsonAppend(&used,
          ",\"websocket\":{\"nowMs\":%u,\"state\":\"%s\",\"reason\":\"%s\","
          "\"epoch\":%u,\"subscribed\":%s,\"machineConnectedKnown\":%s,"
          "\"machineConnected\":%s,\"connectedAtMs\":%u,\"messageAtMs\":%u,"
          "\"powerAtMs\":%u,\"pongAtMs\":%u,\"pauseAtMs\":%u,\"retryRemainingMs\":%u,"
          "\"rxBytes\":%llu,\"txBytes\":%llu,\"rxBytesPerSecond\":%u,\"txBytesPerSecond\":%u,"
          "\"rxBytesPerMinute\":%u,\"txBytesPerMinute\":%u,\"messages\":%u,"
          "\"reconnects\":%u,\"errors\":%u,\"publishLatencyMs\":%u,\"maxPublishLatencyMs\":%u,"
          "\"plannedConnections\":%u,\"unexpectedConnections\":%u,"
          "\"stoppedLatencyMs\":%u,\"maxStoppedLatencyMs\":%u,\"retainedBytes\":%u,"
          "\"workspaceExternal\":%s,\"allocationFailures\":%u,"
          "\"connectFreeDelta\":%ld,\"connectLargestDelta\":%ld,\"stopFreeDelta\":%ld,\"stopLargestDelta\":%ld,"
          "\"internalMinimum\":%u,\"psramMinimum\":%u,\"psramFree\":%u,\"psramLargest\":%u,"
          "\"cleaning\":\"%s\",\"cleaningLabel\":\"%s\",\"cleaningAvailable\":%s,\"cleaningAtMs\":%u}",
          unsigned(now), micraSocketStateName(ws.state), ws.reason,
          unsigned(ws.epoch), ws.subscribed ? "true" : "false",
          ws.machineConnectedKnown ? "true" : "false", ws.machineConnected ? "true" : "false",
          unsigned(ws.connectedAtMs), unsigned(ws.messageAtMs), unsigned(ws.powerAtMs),
          unsigned(ws.pongAtMs), unsigned(ws.pauseAtMs),
          unsigned(!ws.retryAtMs || micra_timing::deadlineReached(now, ws.retryAtMs) ? 0 : ws.retryAtMs - now),
          static_cast<unsigned long long>(ws.receiveBytes), static_cast<unsigned long long>(ws.transmitBytes), unsigned(ws.receivePerSecond),
          unsigned(ws.transmitPerSecond), unsigned(ws.receivePerMinute), unsigned(ws.transmitPerMinute),
          unsigned(ws.messages), unsigned(ws.reconnects), unsigned(ws.errors),
          unsigned(ws.publishLatencyMs), unsigned(ws.maxPublishLatencyMs),
          unsigned(ws.plannedConnections), unsigned(ws.unexpectedConnections),
          unsigned(ws.stoppedLatencyMs), unsigned(ws.maxStoppedLatencyMs), unsigned(ws.retainedBytes),
          ws.workspaceExternal ? "true" : "false", unsigned(ws.allocationFailures),
          long(ws.heap.lastDelta.freeBytes), long(ws.heap.lastDelta.largestBlock),
          long(ws.stopHeap.lastDelta.freeBytes), long(ws.stopHeap.lastDelta.largestBlock),
          unsigned(ws.internalMinimum), unsigned(ws.psramMinimum), unsigned(ws.psramFree), unsigned(ws.psramLargest),
          cleaning, label, ws.cleaningAvailable ? "true" : "false", unsigned(ws.cleaningAtMs));
      const LineaMicraCloudCall call = machineIntegrationCloudCall();
      ok = ok && (call.api == nullptr
          ? statusJsonAppend(&used, ",\"cloudCall\":null")
          : statusJsonAppend(
                &used, ",\"cloudCall\":{\"api\":\"%s\",\"method\":\"%s\","
                "\"result\":\"%s\",\"startedAtUtcSec\":%lu,"
                "\"durationMs\":%lu,\"httpStatus\":%u,\"transportStatus\":%ld}",
                call.api, call.method, call.result,
                static_cast<unsigned long>(call.startedAtUtcSec),
                static_cast<unsigned long>(call.durationMs),
                static_cast<unsigned>(call.httpStatus),
                static_cast<long>(call.transportStatus)));
    }
    if (ok) ok = statusJsonAppend(&used, "}");
  }
#endif
