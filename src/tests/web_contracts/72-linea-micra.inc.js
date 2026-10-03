const micraWeb = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperLineaMicraWeb.inc'), 'utf8');
const micraStatus = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperStatus.inc'), 'utf8') + fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperLineaMicraStatus.inc'), 'utf8');
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
{
  const assert = require('assert'), vm = require('vm');
  assert(rawPartialHtml.home.includes('<div class="metric micraOnly"><strong>{{webui:home.machine_power_state}}</strong><div id="homeMicraPower">'));
  const power = {textContent: ''};
  const context = {R: {applyHomeStatus() {}, $: id => id === 'homeMicraPower' ? power : null},
    document: {querySelector: () => null},
    __WEBUI_TEXT__: key => key === 'home.optimistic' ? 'Optimistic' : 'Unknown'};
  vm.runInNewContext(viewJs.home.replace(/import\*as R from'[^']+';/, '').replace(/export /g, ''), context);
  for (const [lineaMicra, expected] of [
    [{powerState: 'ON', quality: 'current'}, 'ON'],
    [{powerState: 'OFF', quality: 'current'}, 'OFF'],
    [{powerState: 'ON', quality: 'stale', optimisticOff: true}, 'ON'],
    [{powerState: 'OFF', quality: 'stale', optimisticOn: true}, 'OFF'],
    [{powerState: 'OFF', quality: 'optimistic', optimisticOn: true}, 'ON - Optimistic'],
    [{powerState: 'ON', quality: 'optimistic', optimisticOff: true}, 'OFF - Optimistic'],
    [{powerState: 'ON', quality: 'current', optimisticOff: true}, 'ON'],
    [{powerState: 'OFF', quality: 'current', optimisticOn: true}, 'OFF'],
    [{powerState: 'UNKNOWN'}, '—'],
    [undefined, '—'],
  ]) {
    context.applyStatus({lineaMicra});
    assert.strictEqual(power.textContent, expected);
  }
  assert(micraStatus.replace(/\s+/g, ' ').includes(
      'page == StatusPage::Home || page == StatusPage::Settings || page == StatusPage::Diagnostic'),
      'Home status must project the lineaMicra block for the power summary');
}
{
  const assert = require('assert');
  const vm = require('vm');
  const panelAt = micraDiagnosticHtml.indexOf('id="micraCloudDiagnostics"');
  assert(panelAt >= 0 && panelAt < micraDiagnosticHtml.indexOf('{{webui:diagnostic.misc}}'));
  assert(micraDiagnosticHtml.includes('id="micraCloudDiagnostics" class="statusColumn micraOnly"'));
  const ids = ['dMicraCloudEmail', 'dMicraCloudMachine', 'dMicraCloudTime',
    'dMicraCloudApi', 'dMicraCloudResult', 'dMicraCloudDuration'];
  const nodes = Object.fromEntries(ids.map(id => [id, {textContent: ''}]));
  const cloudUi = fs.readFileSync(path.join(sketchDir, 'web/js/diagnostic.js'), 'utf8');
  assert(cloudUi.includes('function ensureMicraRows()'));
  // Exercise row creation with a minimal DOM, including repeated status updates.
  const dom = {};
  const element = () => ({children: [], textContent: '',
    set id(value) { this._id = value; dom[value] = this; }, get id() { return this._id; },
    append(...children) { for (const child of children) { child.parentElement = this; this.children.push(child); } },
    insertBefore(child, anchor) { child.parentElement = this; const at = this.children.indexOf(anchor); this.children.splice(at < 0 ? this.children.length : at, 0, child); }});
  const state = element(), modeRow = element(), mode = element(), cloud = element(), heapRow = element(), heap = element();
  mode.id = 'dMicraMode'; cloud.id = 'micraCloudDiagnostics'; heap.id = 'hHeapLargest';
  state.append(modeRow); modeRow.append(mode); heapRow.append(heap);
  const labels = {'diagnostic.cloud_titles': 'Email|Machine|Time|API|Result|Duration|Connection|Traffic|Cleaning',
    'diagnostic.cleaning_states': 'Inactive|Waiting for paddle|Cleaning'};
  const domContext = vm.createContext({$: id => dom[id], document: {createElement: element},
    __WEBUI_TEXT__: key => labels[key] || key, R: {formatWallTime: String}});
  vm.runInContext(cloudUi.slice(0, cloudUi.indexOf('function formatScaleDisconnect(')), domContext);
  const show = lm => { domContext.lm = lm; vm.runInContext('renderMicraCloudDiagnostic(lm)', domContext); };
  const socket = {state: 'streaming', nowMs: 5000, cleaningAvailable: true, cleaning: 'waiting_for_paddle', cleaningAtMs: 3000,
    rxBytes: 120, rxBytesPerSecond: 20, rxBytesPerMinute: 100, messages: 2};
  const linked = {accountConfigured: true, observeState: true, connectionType: 'websocket', websocket: socket};
  show(linked); show(linked);
  assert.strictEqual(cloud.children.length, 8, 'Status refresh must not duplicate dynamic rows');
  assert.strictEqual(state.children[0], dom.dMicraCleaning.parentElement, 'Cleaning appears before observed mode');
  assert.strictEqual(dom.dMicraCleaning.textContent, 'Waiting for paddle');
  assert(dom.dMicraCleaningHint.textContent.includes('2.0 s'));
  assert(!dom.dMicraCleaningHint.textContent.includes('diagnostic.stale'));
  assert(dom.dMicraWsTraffic.textContent.includes('RX 20 B/s · 100 B/60s · 120 B'));
  show({...linked, websocket: {...socket, state: 'paused', cleaning: 'unknown', cleaningLabel: '<b>Future</b>'}});
  assert.strictEqual(dom.dMicraCleaning.textContent, 'runtime.unknown_4 · <b>Future</b>');
  assert(dom.dMicraCleaningHint.textContent.includes('diagnostic.stale'));
  show({...linked, connectionType: 'api'});
  assert.strictEqual(dom.dMicraCleaning.textContent, 'diagnostic.cleaning_api');
  show({...linked, observeState: false});
  assert.strictEqual(dom.dMicraCleaning.textContent, 'runtime.disabled');
  show({...linked, accountConfigured: false});
  assert.strictEqual(dom.dMicraCleaning.textContent, 'runtime.not_connected');
  show({...linked, websocket: {}});
  assert.strictEqual(dom.dMicraCleaning.textContent, 'diagnostic.cleaning_no_update');
  const context = vm.createContext({$: id => nodes[id],
    __WEBUI_TEXT__: key => key === 'diagnostic.cloud_results' ? ['success','canceled','http_error','transport_error','invalid_response','response_too_large','setup_error'].map(k=>'diagnostic.cloud_'+k).join('|') : key,
    R: {formatWallTime: (sec, offset) => {assert.strictEqual(offset, 0); return String(sec);}}});
  vm.runInContext(cloudUi.slice(cloudUi.indexOf('function renderMicraCloudDiagnostic('),
      cloudUi.indexOf('function formatScaleDisconnect(')), context);
  const render = data => {
    context.lm = data;
    vm.runInContext('renderMicraCloudDiagnostic(lm)', context);
  };
  render({});
  assert.strictEqual(nodes.dMicraCloudEmail.textContent, 'diagnostic.cloud_no_account');
  assert.strictEqual(nodes.dMicraCloudMachine.textContent, 'diagnostic.cloud_no_machine');
  assert.strictEqual(nodes.dMicraCloudTime.textContent, 'diagnostic.cloud_no_call');
  const selected = {accountConfigured: true, email: 'barista@example.test',
    selectedName: '<b>Kitchen Micra</b>', selectedSerial: 'ABC'};
  const call = {api: 'read_dashboard', method: 'GET', startedAtUtcSec: 1790726400,
    result: 'success', durationMs: 0, httpStatus: 200, transportStatus: 0};
  render({...selected, cloudCall: call});
  assert.strictEqual(nodes.dMicraCloudEmail.textContent, selected.email);
  assert.strictEqual(nodes.dMicraCloudMachine.textContent, selected.selectedName,
      'Account and machine text must never be interpreted as HTML');
  assert.strictEqual(nodes.dMicraCloudTime.textContent, '1790726400 UTC');
  assert.strictEqual(nodes.dMicraCloudApi.textContent, 'GET read_dashboard');
  assert.strictEqual(nodes.dMicraCloudResult.textContent, 'diagnostic.cloud_success · HTTP 200');
  assert.strictEqual(nodes.dMicraCloudDuration.textContent, '0 ms');
  for (const result of ['canceled', 'http_error', 'transport_error', 'invalid_response',
    'response_too_large', 'setup_error']) {
    render({...selected, cloudCall: {...call, result, httpStatus: 0, transportStatus: -1}});
    assert.strictEqual(nodes.dMicraCloudResult.textContent, `diagnostic.cloud_${result} · -1`);
  }
  for (const status of [401, 403, 429, 503]) {
    render({...selected, cloudCall: {...call, result: 'http_error', httpStatus: status}});
    assert(nodes.dMicraCloudResult.textContent.endsWith(`HTTP ${status}`));
  }
  render({cloudCall: call});
  assert.strictEqual(nodes.dMicraCloudMachine.textContent, 'diagnostic.cloud_no_machine',
      'Disconnect must clear the selected identity while retaining the last call');
  render({cloudCall: null});
  assert.strictEqual(nodes.dMicraCloudApi.textContent, 'diagnostic.cloud_no_call');
  assert.strictEqual(nodes.dMicraCloudDuration.textContent, 'diagnostic.cloud_no_call');
  assert(!micraStatus.includes('page == StatusPage::Settings ? safeMicraUsername : ""'));
  assert(micraStatus.includes('machineIntegrationCloudCall()'));
  assert(cloudUi.includes('renderMicraCloudDiagnostic(s.lineaMicra)'));
  const complete = micraService.slice(micraService.indexOf('void ShotStopperMicraService::releaseIoBuffer('),
      micraService.indexOf('void ShotStopperMicraService::releaseWorkBuffer('));
  assert(complete.includes('serialTraceCategoryf(success ? LogLevel::INFO'));
  assert(complete.includes('transient ? LogLevel::WARNING : LogLevel::ERROR'));
  assert(complete.includes('lineaMicraHttpRetryable(call.httpStatus)'));
  assert(complete.includes('publishedCloudCall_ = call;'));
  assert(complete.includes('call.result = "invalid_response"'));
  assert.strictEqual((micraService.match(/releaseIoBuffer\(ok\)/g) || []).length, 2,
      'Both token responses must be validated before reporting success');
  assert.strictEqual((micraService.match(/releaseIoBuffer\(false\)/g) || []).length, 1,
      'Invalid machine lists and dashboards must be reported as errors');
  assert(micraService.includes('releaseIoBuffer(decoded)'));
  assert(micraService.includes('work_->responseOverflow ? "response_too_large"'));
  assert(micraService.includes('work_->transportFailure ? "transport_error"'));
}
const deferObservation = micraService.slice(
  micraService.indexOf('void ShotStopperMicraService::deferObservation('),
  micraService.indexOf('void ShotStopperMicraService::fail('));
const terminalFailure = micraService.slice(
  micraService.indexOf('void ShotStopperMicraService::fail('),
  micraService.indexOf('void ShotStopperMicraService::scheduleAutomatic('));

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
const micraPublishAt = networkService.indexOf(
    'publishMachineIntegrationNetworkState(');
const micraNetworkState = micraService.slice(
    micraService.indexOf('void ShotStopperMicraService::publishNetworkState('),
    micraService.indexOf('bool ShotStopperMicraService::queueConnect('));
if (!micraService.includes('config.is_async = true;') ||
    !micraService.includes('while (performed == ESP_ERR_HTTP_EAGAIN)') ||
    !micraService.includes('(void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1));') ||
    !micraService.includes('millis() - requestStartedAtMs') ||
    micraPublishAt < 0 ||
    !micraNetworkState.includes('abortRequested_.store(true,') ||
    !micraNetworkState.includes('xTaskNotifyGive(task_)') ||
    micraNetworkState.includes('esp_http_client_') ||
    networkService.includes('serviceMachineIntegrationAbort') ||
    micraService.includes('esp_http_client_cancel_request') ||
    micraService.includes('activeClient_') || micraService.includes('clientMux_')) {
  throw new Error(
      'Linea Micra HTTPS cancellation must wake its sole owner without mutating live TLS from another task');
}
if (!micraTiming.includes('kStatePollMs = 30000') ||
    !micraTiming.includes('kStateFreshnessMs = kStatePollMs') ||
    !micraTiming.includes('kOptimisticOverlayMs = 2U * kStatePollMs') ||
    !micraTiming.includes('kExhaustedCooldownMs = kOptimisticOverlayMs') ||
    !micraTiming.includes('kPostWakeObservationDelayMs = 15000') ||
    !micraService.includes('const bool networkReady = networkEligible(observationGate)') ||
    !micraService.includes('pending_.present && (!pendingObservation || observationReady)') ||
    !micraService.includes('observationSchedule_.armPostEvent(now)') ||
    !micraService.includes('? powerState_.noteStandbyCommandAccepted(published_,') ||
    !micraService.includes(': powerState_.notePowerOnCommandAccepted(published_,') ||
    !micraService.includes('observationSchedule_.armPostEvent(millis())') ||
    !micraService.includes('deferObservation(pending, status, gateError)') ||
    !micraService.includes('observationActive_.load(std::memory_order_acquire)') ||
    !micraService.includes('bool ShotStopperMicraService::observationCurrent(') ||
    !micraService.includes('pending_.request.type == LineaMicraRequestType::OBSERVE_STATE) {') ||
    !micraService.includes('!config_.accountConfigured ||\n      (config_.options & LINEA_MICRA_OBSERVE_STATE) == 0 ||') ||
    !micraService.includes('scheduleAutomatic(millis(), connecting)')) {
  throw new Error('Linea Micra observations must wait for readiness and the post-wake convergence deadline');
}
if (!micraStatus.includes('\\\"optimisticOn\\\":%s,\\\"optimisticOff\\\":%s') ||
    !deferObservation.includes('powerState_.hold(published_, true, millis());') ||
    deferObservation.includes('published_.powerState =') ||
    !micraService.includes('status.phase = LineaMicraPhase::BACKOFF;\n    publish(status);') ||
    !terminalFailure.includes('status.powerState = LineaMicraPowerState::UNKNOWN;') ||
    !terminalFailure.includes('status.effectiveOn = true;') ||
    !terminalFailure.includes('status.quality = LineaMicraObservationQuality::COMMUNICATION_ERROR;')) {
  throw new Error('Pending, retry, and canceled observations must preserve power until terminal failure publishes UNKNOWN');
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
    !micraService.includes('lineaMicraHttpRetryable') ||
    !micraService.includes('lineaMicraTemperatureCycleRetryable') ||
    !micraService.includes('observationFence_.merge(published_, powerState_, update') ||
    !micraService.includes('desiredTemperature_.machineConfigGeneration = configGeneration;') ||
    !micraService.includes('abortRequested_.load(std::memory_order_acquire)') ||
    !micraService.includes('refreshToken(settings) || signIn(settings)') ||
    !micraService.includes('char authorization[kTokenCapacity + 8] = {};') ||
    !micraService.includes('snprintf(authorization, kTokenCapacity + 8,') ||
    !micraService.includes('esp_http_client_set_header(work_->client, "Authorization",\n                                               authorization) == ESP_OK') ||
    !micraService.includes('secureWipe(authorization, kTokenCapacity + 8);') ||
    micraService.includes('Bearer %s", work_->accessToken);\n    const bool ok = length > 0 &&\n                    static_cast<size_t>(length) < sizeof(io_->response)') ||
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
  'lineaMicraConnectionType', 'lineaMicraApplyTemperature', 'lineaMicraObserveState',
  'lineaMicraRecognizeWake', 'lineaMicraShutdownWithScale',
  'lineaMicraShutdownGraceWrap', 'lineaMicraShutdownGrace',
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
{
  const assert = require('assert');
  const vm = require('vm');
  const binding = viewJs.settings.match(/\$\('lineaMicraDisconnectButton'\)\.onclick=(.*?);if\(\$\('lineaMicraShutdownWithScale'\)/);
  assert(binding, 'Micra Disconnect click binding is missing');
  let calls = 0, confirmed = false;
  const click = vm.runInNewContext(`(${binding[1]})`, {
    confirm: () => confirmed,
    __WEBUI_TEXT__: key => key,
    R: {disconnectLineaMicra: () => { calls++; }},
  });
  click();
  assert.strictEqual(calls, 0, 'Cancel must leave the account connected');
  confirmed = true;
  click();
  assert.strictEqual(calls, 1, 'Confirmed Disconnect must invoke the action');

  const statusUi = rawRuntimeJs.slice(rawRuntimeJs.indexOf('const MICRA_SWITCHES='),
      rawRuntimeJs.indexOf('function renderLineaMicraDiagnostic('));
  for (const theme of ['theme-light', 'theme-dark']) {
    const nodes = new Map();
    const get = id => {
      if (!nodes.has(id)) nodes.set(id, {disabled: false, checked: false, dataset: {},
        busy: false,
        classList: {toggle(_, on) { if (id === 'lineaMicraConnectButton') nodes.get(id).busy = on; }},
        parentElement: {nextSibling: null}});
      return nodes.get(id);
    };
    const select = get('lineaMicraMachine');
    select.add = option => { select.options.push(option); select.value = option.value; };
    Object.defineProperty(select, 'textContent', {set() { select.options = []; select.value = ''; }});
    let label = get('lineaMicraUsername').parentElement;
    for (let i = 0; i < 5; i++) { label.nextSibling = {}; label = label.nextSibling; }
    const classes = new Set([theme]);
    const context = vm.createContext({$: get, controlsMutable: true, micraDirty: false,
      document: {documentElement: {classList: {toggle(name, on) {
        if (on) classes.add(name); else classes.delete(name);
      }}}},
      Option: function(text, value) { this.text = text; this.value = value; },
      __WEBUI_TEXT__: key => key});
    vm.runInContext(statusUi, context);
    const commands = [];
    context.command = (url, body) => { commands.push({url, body: JSON.parse(JSON.stringify(body))}); return true; };
    get('lineaMicraConnectionType').value = 'api';
    get('lineaMicraObserveState').checked = true;
    get('lineaMicraShutdownGrace').value = '12';
    vm.runInContext("saveLineaMicraSettings('select','SYNTHETIC')", context);
    assert.deepStrictEqual(commands[0], {url: '/api/v1/machine/linea-micra', body: {
      action: 'select', serial: 'SYNTHETIC', applyTemperature: false, observeState: true,
      recognizeWakeGesture: false, powerOnWithScale: false, shutdownWithScale: false,
      scaleOffWithMachine: false, connectionType: 'api', shutdownGraceSeconds: 12}});
    get('lineaMicraConnectionType').value = 'websocket';
    vm.runInContext('saveLineaMicraSettings()', context);
    assert.strictEqual(commands[1].body.connectionType, 'websocket');
    assert.strictEqual(commands[1].body.action, 'save');
    assert(!('serial' in commands[1].body));
    const connected = {accountConfigured: true, email: 'user@example.test',
      selectedName: 'Micra', selectedSerial: 'ABC', observeState: true,
      machines: [], phase: 'idle', error: 'none', staConnected: true};
    vm.runInContext(`applyLineaMicraStatus(${JSON.stringify({lineaMicra: connected})})`, context);
    assert.strictEqual(get('lineaMicraConnectionType').value,'websocket');
    context.micraDirty=true;
    get('lineaMicraConnectionType').value='api';
    get('lineaMicraApplyTemperature').checked=false;
    vm.runInContext(`applyLineaMicraStatus(${JSON.stringify({lineaMicra: connected})})`,context);
    assert.strictEqual(get('lineaMicraConnectionType').value,'api');
    assert.strictEqual(get('lineaMicraApplyTemperature').checked,false);
    context.micraDirty=false;
    vm.runInContext(`applyLineaMicraStatus(${JSON.stringify({lineaMicra: {...connected,connectionType:'api',applyTemperature:true}})})`,context);
    assert.strictEqual(get('lineaMicraConnectionType').value,'api');
    assert.strictEqual(get('lineaMicraApplyTemperature').checked,true);
    assert.strictEqual(get('lineaMicraSaveButton').disabled, false,
        `${theme}: Save must be available after selection`);
    vm.runInContext(`applyLineaMicraStatus(${JSON.stringify({lineaMicra: {
      ...connected, accountConfigured: false, email: '', selectedName: '',
      selectedSerial: '', phase: 'disabled'}})})`, context);
    assert.strictEqual(get('lineaMicraSaveButton').disabled, true);
    assert.strictEqual(get('lineaMicraApplyTemperature').disabled, true);
    assert.strictEqual(get('lineaMicraIdentity').innerText, 'runtime.not_connected');
    assert.strictEqual(get('lineaMicraStatus').textContent, 'runtime.unauthenticated');
    assert.strictEqual(get('lineaMicraUsername').parentElement.hidden, false);
    vm.runInContext(`applyLineaMicraStatus(${JSON.stringify({lineaMicra: {
      ...connected, accountConfigured: false, email: '', selectedName: '',
      selectedSerial: '', phase: 'authenticating'}})})`, context);
    assert.strictEqual(get('lineaMicraConnectButton').busy, true,
        'Connect must spin while the cloud sign-in is in flight');
    assert.strictEqual(get('lineaMicraIdentity').innerText,
        'runtime.not_connected',
        'No signed-in claim before the machine list lands');
    assert.strictEqual(get('lineaMicraStatus').textContent, 'authenticating',
        'The status hint must narrate the in-flight sign-in phase');
    vm.runInContext(`applyLineaMicraStatus(${JSON.stringify({lineaMicra: {
      ...connected, accountConfigured: false, email: '', selectedName: '',
      selectedSerial: '', machines: [{serial: 'ABC', name: 'Micra'}],
      phase: 'confirmed'}})})`, context);
    assert.strictEqual(get('lineaMicraIdentity').innerText,
        'runtime.signed_in_select_machine',
        'A loaded machine list must read as signed in');
    assert.strictEqual(get('lineaMicraStatus').textContent, 'confirmed',
        'The status hint must show the session phase after Connect');
    assert.strictEqual(get('lineaMicraUsername').parentElement.hidden, false,
        'Credentials must stay visible until a machine is saved');
    assert.strictEqual(get('lineaMicraConnectButton').busy, false,
        'Connect must stop spinning once machines are listed');
    vm.runInContext(`applyLineaMicraStatus(${JSON.stringify({lineaMicra: {
      ...connected, accountConfigured: false, email: '', selectedName: '',
      selectedSerial: '', phase: 'failed', error: 'invalid_auth'}})})`, context);
    assert.strictEqual(get('lineaMicraConnectButton').busy, false,
        'Connect must stop spinning when authentication fails');
    assert.strictEqual(get('lineaMicraStatus').textContent,
        'failed · invalid_auth',
        'A failed sign-in must surface the phase and error');
    vm.runInContext(`applyLineaMicraStatus(${JSON.stringify({lineaMicra: {
      ...connected, accountConfigured: false, email: '', selectedName: '',
      selectedSerial: '', machines: [{serial: 'ABC', name: 'Micra'}],
      phase: 'failed', error: 'invalid_auth'}})})`, context);
    assert.strictEqual(get('lineaMicraIdentity').innerText,
        'runtime.signed_in_select_machine',
        'A listed machine stays selectable while a re-auth attempt fails');
  }
}
if (!rawCss.includes('html:not(.lineaMicraIntegration) .micraOnly') ||
    !rawCss.includes('[type=email]') ||
    !micraDiagnosticHtml.includes('<span id="dMicraPowerValue">{{webui:diagnostic.unknown}}</span> - <a id="lineaMicraRefreshLink" href="#" aria-disabled="true" tabindex="-1">({{webui:diagnostic.refresh_state}})</a>') ||
    micraDiagnosticHtml.includes('id="lineaMicraRefreshButton"') ||
    !viewJs.diagnostic.includes("e.preventDefault();R.lineaMicraAction('refresh')") ||
    !rawRuntimeJs.includes("s.machineIntegration==='linea_micra_cloud'") ||
    !rawRuntimeJs.includes("{action:'connect',username,password}") ||
    !rawRuntimeJs.includes("lineaMicraAction('disconnect')") ||
    rawRuntimeJs.includes("['queued','authenticating','listing','running','backoff'].includes(m.phase)") ||
    !rawRuntimeJs.includes("m.email+'\\n'+m.selectedName+' - '+m.selectedSerial") ||
    !rawRuntimeJs.includes("$('lineaMicraIdentity').innerText=connected?") ||
    !rawRuntimeJs.includes("for(let e=$('lineaMicraUsername').parentElement,n=5;n--;e=e.nextSibling)e.hidden=connected") ||
    !rawRuntimeJs.includes("$('lineaMicraConnectButton').disabled=!canEdit||!m.staConnected||m.apActive") ||
    !micraSettingsHtml.includes('<button id="lineaMicraConnectButton" class="btnGlyph mutable btnInvert" type="button"><span class="g">{{webui:settings.symbol_3}}</span><span class="t">{{webui:settings.connect_account}}</span></button>') ||
    !rawRuntimeJs.includes("$('lineaMicraConnectButton').classList.add('busy');const ok=await command('/api/v1/machine/linea-micra',{action:'connect'") ||
    !rawRuntimeJs.includes("$('lineaMicraConnectButton').classList.toggle('busy',!connected&&['queued','authenticating','listing'].includes(m.phase))") ||
    !rawRuntimeJs.includes('select.disabled=!canEdit||!machines.length') ||
    !rawRuntimeJs.includes("updateMicraShutdownControls(canEdit,connected)") ||
    !rawRuntimeJs.includes("$('lineaMicraShutdownGrace').disabled=!canEdit||!connected||!on") ||
    !rawRuntimeJs.includes("$('lineaMicraShutdownGraceWrap').classList.toggle('hidden',!on)") ||
    !rawRuntimeJs.includes("$('lineaMicraShutdownGrace').value=String(m.shutdownGraceSeconds||0)") ||
    !viewJs.settings.includes("$('lineaMicraShutdownWithScale').onchange=()=>R.updateMicraShutdownControls()") ||
    !micraSettingsHtml.includes('id="lineaMicraShutdownWithScale" type="checkbox">') ||
    !micraSettingsHtml.includes('id="lineaMicraShutdownGraceWrap" class="hidden"') ||
    !micraSettingsHtml.includes('<option value="60">') ||
    micraSettingsHtml.includes('id="lineaMicraShutdownWithScale" type="checkbox" checked') ||
    !rawRuntimeJs.includes("$('lineaMicraDisconnectButton').disabled=!canEdit||(!connected&&!machines.length)") ||
    !rawRuntimeJs.includes("$('dMicraPowerValue').textContent=power") ||
    !rawRuntimeJs.includes("m.temperatureState&&m.temperatureState!=='disabled'") ||
    !rawRuntimeJs.includes("$('dMicraAge').textContent=lm.temperatureState+'/'+lm.temperatureError+' · '") ||
    !rawRuntimeJs.includes("refresh.setAttribute('aria-disabled',String(disabled))") ||
    rawRuntimeJs.includes("expired?'UNKNOWN':lm.powerState") ||
    !rawRuntimeJs.includes('power=lm.powerState') ||
    !rawRuntimeJs.includes("stale=lm.quality==='current'&&age>=lm.freshnessMs") ||
    !micraSettingsHtml.includes('id="lineaMicraApplyTemperature" type="checkbox" checked') ||
    !micraSettingsHtml.includes('id="lineaMicraObserveState" type="checkbox" checked') ||
    !micraSettingsHtml.includes('id="lineaMicraRecognizeWake" type="checkbox" checked')) {
  throw new Error('Linea Micra UI must implement account connection, selection, and quality-only freshness expiry');
}
if (micraService.includes('keep_alive_enable = true') ||
    micraService.includes('esp_http_client_close(work_->client)') ||
    micraService.includes('char authorization[kTokenCapacity + 8];') ||
    (micraService.match(/"Accept",\s*"application\/json"/g) || []).length !== 1 ||
    (micraService.match(/"User-Agent",\s*\n?\s*"OpenBrewByWeight\/1"/g) || []).length !== 1 ||
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

const micraScaleShutdown = fs.readFileSync(
    path.join(sketchDir, 'machine/ShotStopperMicraScaleShutdown.h'), 'utf8');
const micraScalePowerOn = fs.readFileSync(
    path.join(sketchDir, 'machine/ShotStopperMicraScalePowerOn.h'), 'utf8');
const micraMachinePower = fs.readFileSync(
    path.join(sketchDir, 'machine/ShotStopperMicraMachinePower.h'), 'utf8');
const entrypoints = fs.readFileSync(
    path.join(sketchDir, 'platform/ShotStopperEntrypoints.inc'), 'utf8');
const protocolGeneric = fs.readFileSync(
    path.join(sketchDir, '..', 'libraries', 'EspressoScaleBLE', 'src',
              'protocols', 'GenericFf11.cpp'),
    'utf8');
if (!micraScaleShutdown.includes('LINEA_MICRA_SCALE_EXPLICIT_DISCONNECT = 9') ||
    !micraScaleShutdown.includes('!scale.relayClosed') ||
    !micraScaleShutdown.includes('LINEA_MICRA_SHUTDOWN_WITH_SCALE') ||
    !micraScaleShutdown.includes('lineaMicraShutdownGraceSeconds') ||
    !micraService.includes('CoffeeMachineChangeMode') ||
    !micraService.includes('{\\"mode\\":\\"StandBy\\"}') ||
    !micraService.includes('bool ShotStopperMicraService::executePowerApplication(') ||
    !micraService.includes('void ShotStopperMicraService::deferPower(') ||
    !micraService.includes('deferPower(request, LineaMicraError::CANCELED, 0, false)') ||
    !micraService.includes('uint8_t powerOptionBit(LineaMicraRequestType type)') ||
    !micraService.includes('? LINEA_MICRA_POWER_ON_WITH_SCALE') ||
    !machineIntegration.includes('void serviceMachineIntegrationScaleLink(') ||
    !machineIntegration.includes('MicraScaleShutdownTracker') ||
    !entrypoints.includes('serviceMachineIntegrationScaleLink(') ||
    !micraWeb.includes('"shutdownWithScale"') ||
    !micraWeb.includes('"shutdownGraceSeconds"') ||
    !micraStatus.includes('\\"shutdownWithScale\\"') ||
    !micraStatus.includes('\\"shutdownGraceSeconds\\"')) {
  throw new Error('Scale power-off shutdown must be machine-side, explicit-disconnect only, relay-guarded, grace-cancelled, and cloud-gated');
}
if (!micraScalePowerOn.includes('LINEA_MICRA_POWER_ON_WITH_SCALE') ||
    !micraScalePowerOn.includes('LINEA_MICRA_SCALE_EXPLICIT_DISCONNECT') ||
    !micraScalePowerOn.includes('scale.relayClosed') ||
    !micraService.includes('SET_POWER_ON') ||
    !micraService.includes('{\\"mode\\":\\"BrewingMode\\"}') ||
    !machineIntegration.includes('MicraScalePowerOnTracker') ||
    !machineIntegration.includes('LineaMicraRequestType::SET_POWER_ON') ||
    !micraWeb.includes('"powerOnWithScale"') ||
    !micraStatus.includes('\\"powerOnWithScale\\"')) {
  throw new Error('Scale power-on must fire only after the scale\'s own power-off, stay relay-guarded, and send BrewingMode through the cloud');
}
if (!micraMachinePower.includes('LINEA_MICRA_SCALE_OFF_WITH_MACHINE') ||
    !micraMachinePower.includes('LineaMicraObservationQuality::CURRENT') ||
    !machineIntegration.includes('MicraMachinePowerTracker') ||
    !machineIntegration.includes('requestScalePowerOff()') ||
    !machineIntegration.includes('machineEffectivelyOff()') ||
    !machineIntegration.includes('Scale power-off skipped: connected scale does not support power-off') ||
    !entrypoints.includes('serviceMachineIntegrationMachinePower(') ||
    !entrypoints.includes('loopScaleLink.features.has(ScaleFeaturePowerOff)') ||
    !scaleWorker.includes('void requestScalePowerOff()') ||
    !scaleWorker.includes('executeScalePowerOffCommand') ||
    !protocolGeneric.includes('ScaleFeaturePowerOff') ||
    !micraWeb.includes('"scaleOffWithMachine"') ||
    !micraStatus.includes('\\"scaleOffWithMachine\\"')) {
  throw new Error('Scale-off-with-machine must fire once per confirmed ON→OFF edge, warn instead of writing to unsupported scales, and never re-trigger the shutdown cycle');
}
