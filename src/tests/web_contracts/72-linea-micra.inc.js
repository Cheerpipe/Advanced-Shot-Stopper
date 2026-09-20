const micraWeb = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperLineaMicraWeb.inc'), 'utf8');
const micraStatus = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperStatus.inc'), 'utf8');
const micraService = fs.readFileSync(
    path.join(sketchDir, 'machine/ShotStopperMicraService.cpp'), 'utf8');
const micraTiming = fs.readFileSync(
    path.join(sketchDir, 'machine/ShotStopperMicraTiming.h'), 'utf8');
const micraTypes = fs.readFileSync(
    path.join(sketchDir, 'machine/ShotStopperLineaMicraTypes.h'), 'utf8');
const machineIntegration = fs.readFileSync(
    path.join(sketchDir, 'machine/ShotStopperLineaMicraIntegration.cpp'), 'utf8');
const controlCommands = fs.readFileSync(
    path.join(sketchDir, 'control/ShotStopperCommands.inc'), 'utf8');
const commandPersistence = fs.readFileSync(
    path.join(sketchDir, 'persistence/ShotStopperCommandPersistence.inc'), 'utf8');
const scaleEvents = fs.readFileSync(
    path.join(sketchDir, 'scale/ShotStopperScaleEvents.inc'), 'utf8');
const networkService = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperNetworkService.inc'), 'utf8');
const micraSettingsHtml = rawPartialHtml.settings;
const micraDiagnosticHtml = rawPartialHtml.diagnostic;

if (!network.includes('/api/v1/machine/linea-micra') ||
    !micraWeb.includes('UNSUPPORTED_MACHINE') ||
    !micraWeb.includes('jsonHasOnlyUniqueFields') ||
    !micraWeb.includes('MICRA_STA_REQUIRED') ||
    !micraWeb.includes('queueMachineIntegrationConnect') ||
    !micraWeb.includes('selectMachineIntegrationDevice') ||
    !micraWeb.includes('disconnectLineaMicra(candidate)') ||
    !micraWeb.includes('stagedLineaMicra_')) {
  throw new Error('Linea Micra API must be profile-gated, strict, STA-only, staged, and asynchronous');
}
const micraStatusFailures = [
  micraStatus.includes('\"password\"') && 'password exposed',
  micraStatus.includes('\"username\"') && 'username exposed',
  !micraStatus.includes('\\\"accountConfigured\\\"') && 'account status missing',
  !micraStatus.includes('\\\"email\\\"') && 'account email missing',
  !micraStatus.includes('\\\"machines\\\"') && 'machine list missing',
  !micraStatus.includes('SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD') && 'compile gate missing',
  !micraStatus.includes('\\\"machineIntegration\\\"') && 'integration capability missing',
].filter(Boolean);
if (micraStatusFailures.length) {
  throw new Error('Linea Micra status contract: ' + micraStatusFailures.join(', '));
}
if (!micraService.includes('config.save_client_session = true')) {
  throw new Error('Linea Micra cloud client must save TLS sessions for reuse');
}
if (!micraTiming.includes('kStatePollMs = 30000') ||
    !micraTiming.includes('kOptimisticOnMs = 2U * kStatePollMs') ||
    !micraTiming.includes('kPostWakeObservationDelayMs = 15000') ||
    !micraService.includes('const bool networkReady = networkEligible(observationGate)') ||
    !micraService.includes('pending_.present && (!pendingObservation || observationReady)') ||
    !micraService.includes('observationSchedule_.armPostWake(now)') ||
    !micraService.includes('deferObservation(pending, status, gateError)') ||
    !micraService.includes('observationActive_.load(std::memory_order_acquire)') ||
    !micraService.includes('bool ShotStopperMicraService::observationCurrent(') ||
    !micraService.includes('pending_.request.type == LineaMicraRequestType::OBSERVE_STATE) {') ||
    !micraService.includes('!config_.accountConfigured ||\n      (config_.options & LINEA_MICRA_OBSERVE_STATE) == 0 ||') ||
    (micraService.match(/\(config_\.options & LINEA_MICRA_OBSERVE_STATE\) == 0/g) || []).length < 3 ||
    !micraService.includes('scheduleAutomatic(millis(), connecting)')) {
  throw new Error('Linea Micra observations must wait for readiness and the post-wake convergence deadline');
}
if (!micraTypes.includes('APPLY_TEMPERATURE') ||
    !machineIntegration.includes('requestMachineIntegrationPresetTemperature') ||
    !commandPersistence.includes('requestActivePresetMachineTemperature()') ||
    !controlCommands.includes('applyMachineTemperature') ||
    !scaleEvents.includes('requestActivePresetMachineTemperature()') ||
    !networkService.includes('scaleConnecting_.load(std::memory_order_acquire)') ||
    !micraService.includes('CoffeeMachineSettingCoffeeBoilerTargetTemperature') ||
    !micraService.includes('{\\\"boilerIndex\\\":1,\\\"targetTemperature\\\":%u.%u}') ||
    !micraService.includes('verification.targetDeciC == request.targetDeciC') ||
    !micraService.includes('commandAccepted = desiredTemperature_.commandAccepted;') ||
    !micraService.includes('desiredTemperature_.commandAccepted = true;') ||
    !micraService.includes('lineaMicraTemperatureHttpRetryable') ||
    !micraService.includes('lineaMicraTemperatureCycleRetryable') ||
    !micraService.includes('preserveTemperatureStatus(published_, next)') ||
    !micraService.includes('desiredTemperature_.machineConfigGeneration = configGeneration;') ||
    !micraService.includes('abortRequested_.load(std::memory_order_acquire)') ||
    !micraService.includes('refreshToken(settings) || signIn(settings)') ||
    !micraStatus.includes('\\\"temperatureState\\\"') ||
    !micraStatus.includes('\\\"temperatureCommandAccepted\\\"') ||
    !micraStatus.includes('\\\"temperatureRetryable\\\"') ||
    !micraStatus.includes('\\\"requestedTargetDeciC\\\"') ||
    !micraStatus.includes('\\\"appliedTargetDeciC\\\"') ||
    !micraStatus.includes('\\\"temperatureHttpStatus\\\"') ||
    !micraStatus.includes('\\\"scalePaused\\\"')) {
  throw new Error('Linea Micra preset temperature must coalesce, gate on scale/shot activity, reuse authentication, and confirm dashboard readback');
}
for (const id of ['lineaMicraUsername', 'lineaMicraPassword',
  'lineaMicraConnectButton', 'lineaMicraMachine', 'lineaMicraSelectButton',
  'lineaMicraApplyTemperature', 'lineaMicraObserveState',
  'lineaMicraRecognizeWake',
  'lineaMicraSaveButton', 'lineaMicraDisconnectButton',
  'lineaMicraBrewTargetC']) {
  if (!micraSettingsHtml.includes(`id="${id}"`)) {
    throw new Error(`Linea Micra settings control is missing: ${id}`);
  }
}
for (const id of ['dMicraPower', 'dMicraPowerValue', 'dMicraMode',
  'dMicraQuality', 'dMicraAge', 'lineaMicraRefreshLink']) {
  if (!micraDiagnosticHtml.includes(`id="${id}"`)) {
    throw new Error(`Linea Micra diagnostic control is missing: ${id}`);
  }
}
if (!rawCss.includes('html:not(.lineaMicraIntegration) .micraOnly') ||
    !rawCss.includes('[type=email]') ||
    !micraDiagnosticHtml.includes('<span id="dMicraPowerValue">{{webui:diagnostic.unknown}}</span> - <a id="lineaMicraRefreshLink" href="#" aria-disabled="true" tabindex="-1">({{webui:diagnostic.refresh_state}})</a>') ||
    micraDiagnosticHtml.includes('id="lineaMicraRefreshButton"') ||
    !viewJs.diagnostic.includes("e.preventDefault();R.lineaMicraAction('refresh')") ||
    !rawRuntimeJs.includes("s.machineIntegration==='linea_micra_cloud'") ||
    !rawRuntimeJs.includes("{action:'connect',username,password}") ||
    !rawRuntimeJs.includes("{action:'select',serial") ||
    !rawRuntimeJs.includes("observeState:$('lineaMicraObserveState').checked,recognizeWakeGesture:$('lineaMicraRecognizeWake').checked}") ||
    !rawRuntimeJs.includes("lineaMicraAction('disconnect')") ||
    rawRuntimeJs.includes("['queued','authenticating','listing','running','backoff'].includes(m.phase)") ||
    !rawRuntimeJs.includes("m.email+'\\n'+m.selectedName+' - '+m.selectedSerial") ||
    !rawRuntimeJs.includes("$('lineaMicraIdentity').innerText=connected?") ||
    !rawRuntimeJs.includes("for(let e=$('lineaMicraUsername').parentElement,n=5;n--;e=e.nextSibling)e.hidden=connected") ||
    !rawRuntimeJs.includes("$('lineaMicraConnectButton').disabled=!canEdit||!m.staConnected||m.apActive") ||
    !rawRuntimeJs.includes('select.disabled=!canEdit||!machines.length') ||
    !rawRuntimeJs.includes("$('lineaMicraApplyTemperature').disabled=!canEdit||!connected") ||
    !rawRuntimeJs.includes("$('lineaMicraObserveState').disabled=!canEdit||!connected") ||
    !rawRuntimeJs.includes("$('lineaMicraRecognizeWake').disabled=!canEdit||!connected") ||
    !rawRuntimeJs.includes("$('lineaMicraDisconnectButton').disabled=!canEdit||(!connected&&!machines.length)") ||
    !rawRuntimeJs.includes("$('dMicraPowerValue').textContent=power") ||
    !rawRuntimeJs.includes("m.temperatureState&&m.temperatureState!=='disabled'") ||
    !rawRuntimeJs.includes("$('dMicraAge').textContent=lm.temperatureState+'/'+lm.temperatureError+' · '") ||
    !rawRuntimeJs.includes("refresh.setAttribute('aria-disabled',String(disabled))") ||
    !rawRuntimeJs.includes("expired?'UNKNOWN':lm.powerState") ||
    !rawRuntimeJs.includes('expired=!lm.optimisticOn&&lm.sampleValid&&age>=lm.freshnessMs') ||
    !micraSettingsHtml.includes('id="lineaMicraApplyTemperature" type="checkbox" checked') ||
    !micraSettingsHtml.includes('id="lineaMicraObserveState" type="checkbox" checked') ||
    !micraSettingsHtml.includes('id="lineaMicraRecognizeWake" type="checkbox" checked')) {
  throw new Error('Linea Micra UI must implement account connection, selection, and freshness expiry');
}
if (micraService.includes('keep_alive_enable = true') ||
    micraService.includes('esp_http_client_close(work_->client)') ||
    micraService.includes('char authorization[kTokenCapacity + 8]') ||
    (micraService.match(/"Accept",\s*"application\/json"/g) || []).length !== 1 ||
    (micraService.match(/"User-Agent",\s*\n?\s*"AdvancedShotStopper\/1"/g) || []).length !== 1 ||
    !micraService.includes('RequestStateGuard requestState{*this};') ||
    !micraService.includes('~RequestStateGuard() { service.clearRequestState(); }') ||
    !micraService.includes('esp_http_client_delete_header(work_->client, "Authorization")') ||
    !micraService.includes('esp_http_client_delete_header(work_->client, "X-Request-Proof")') ||
    !micraService.includes('esp_http_client_set_post_field(work_->client, nullptr, 0)') ||
    !micraService.includes('config.buffer_size = 1024;') ||
    !micraService.includes('config.buffer_size_tx = 1024;') ||
    !micraService.includes('struct ShotStopperMicraService::IoBuffer') ||
    !micraService.includes('union {') ||
    !micraService.includes('allocExternal(sizeof(WorkBuffer), AllocationOwner::NETWORK)') ||
    !micraService.includes('allocExternal(sizeof(IoBuffer), AllocationOwner::NETWORK)') ||
    !micraService.includes('if (connecting) releaseWorkBuffer();') ||
    !micraService.includes('if (!staConnected || apActive) {') ||
    !micraService.includes('releaseIoBuffer();') ||
    !micraService.includes('const bool staEligible =') ||
    !micraService.includes('} else if (observationReady && config_.accountConfigured &&') ||
    !micraService.includes('const bool initialSample = status.sampleAtMs == 0;') ||
    !micraService.includes('(sessionRenewed && !initialSample) ||')) {
  throw new Error('Linea Micra polling must bound transient PSRAM/stack use, release idle sessions, wait for STA, reuse active HTTP sessions, and avoid continuous probes');
}
for (const file of ['ShotStopperMachinePaddleControl.h',
  'ShotStopperMachinePaddleInput.h', 'ShotStopperMachinePaddlePolicy.h',
  'ShotStopperMachinePaddleState.h']) {
  if (fs.readFileSync(path.join(sketchDir, file), 'utf8').includes('LineaMicra')) {
    throw new Error(`Paddle behavior must remain independent of Linea Micra: ${file}`);
  }
}
