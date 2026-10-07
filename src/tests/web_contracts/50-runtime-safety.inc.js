    !ui.includes('function applyHomeStatus(') ||
    !ui.includes('function applySettingsStatus(') ||
    !ui.includes('function applyAdminStatus(') ||
        ui.includes("api('/api/v1/status')") ||
    !ui.includes('DEVICE_MAX_INFLIGHT') ||
    !ui.includes('acquireDeviceSlot') ||
    !ui.includes('releaseDeviceSlot') ||
    ui.includes('/api/v1/heartbeat') ||
    ui.includes('function heartbeat(') ||
    ui.includes('setInterval(()=>refreshStatus(),2500)')) {
  throw new Error('Web UI must adapt/pause status polls, serialize commands, time out hung fetches, and use DEVICE_MAX_INFLIGHT without POST heartbeat');
}
if (!runtimeJs.includes("function statusIntervalMs(){return document.hidden?12e3:statusLiveShot?2500:4e3}") ||
    !runtimeJs.includes("s=v==='home'?await loadHomeStatus():v==='diagnostic'?await loadDiagnosticStatus():await api(") ||
    !runtimeJs.includes("activeView!=='settings'&&activeView!=='admin'")) {
  throw new Error('Home and Diagnostic must use their socket; other views retain live/idle/hidden REST intervals');
}
if (!ui.includes('async function loadStatus(){') ||
    !ui.includes('function startStatsStream(){') ||
    !ui.includes('function startLogStream(){') ||
    !ui.includes('function applyLogFrame(') ||
    !ui.includes('LOG_EVENTS_CAPACITY') ||
    !ui.includes('logEvents.splice(0,logEvents.length-LOG_EVENTS_CAPACITY)') ||
    !ui.includes("function refreshStatus(){return withPollGate(activeView==='stats'?refreshShots:activeView==='history'?refreshHistory:loadStatus)}") ||
    !ui.includes("function refreshShots(){return shotStatsViewActive()?startStatsStream():Promise.resolve(false)}") ||
    !(ui.includes("name==='home'||name==='settings'||name==='admin'||name==='diagnostic'") ||
      ui.includes("name === 'home' || name === 'settings' || name === 'admin' ||") ||
      ui.includes("name === 'diagnostic'")) ||
    ui.includes("name==='presets'") ||
    !(ui.includes("name==='stats'") || ui.includes("name === 'stats'")) ||
    !ui.includes('renderRoute(location.pathname)') ||
    !ui.includes('ensureView') ||
    ui.includes('Promise.all([loadShots(),loadLog()])')) {
  throw new Error('Web UI must lazy-load status/shots/log per active SPA view; background polls stay gated');
}
{
  const statsStart = (() => {
    const spaced = appJsSource.indexOf("name === 'stats'");
    const i = spaced >= 0 ? spaced : appJsSource.indexOf("name==='stats'");
    if (i < 0) return '';
    const j = appJsSource.indexOf('}', i);
    return j > i ? appJsSource.slice(i, j) : '';
  })();
  if (statsStart.includes('R.loadStatus()') || !statsStart.includes('R.startStatsStream()') ||
      !runtimeJs.includes("was!==canEdit&&activeView==='stats'") ||
      !runtimeJs.includes("fillStarRate(rateHost,r.rating||0,!controlsMutable,")) {
    throw new Error(
        'Stats must load its own state with records, and re-render rating stars when mutable flips');
  }
}
if (!ui.includes('id="view-home"') ||
    !ui.includes('id="view-stats"') ||
    !ui.includes('id="view-settings"') ||
    ui.includes('id="view-presets"') ||
    !ui.includes('id="view-admin"') ||
    ui.includes('id="view-debug"') ||
    !ui.includes('id="view-diagnostic"') ||
    ui.includes('id="view-log"') ||
    !ui.includes('data-route="/settings"') ||
    ui.includes('data-route="/presets"') ||
    !ui.includes('data-route="/admin"') ||
    ui.includes('data-route="/debug"') ||
    !ui.includes('data-route="/diagnostic"') ||
    !ui.includes('history.pushState')) {
  throw new Error('Web UI must expose Home/Stats/Admin/Diagnostic/Settings routes as an SPA');
}
const maxHandlersMatch = network.match(/max_uri_handlers\s*=\s*(\d+)/);
if (!maxHandlersMatch) {
  throw new Error('HTTP server max_uri_handlers not found');
}
const maxUriHandlers = Number(maxHandlersMatch[1]);
const registeredRouteCount = (network.match(/registerHandler\(server_/g) || []).length;
if (registeredRouteCount > 40 || maxUriHandlers <= registeredRouteCount ||
    maxUriHandlers - registeredRouteCount < 8) {
  throw new Error(
    `HTTP wildcard routing must retain at least 8 spare slots and at most 40 registrations ` +
    `(routes=${registeredRouteCount}, limit=${maxUriHandlers})`
  );
}
const ownedWildcardAt = network.indexOf(
    'registerHandler(server_, "/api/v1/*", HTTP_GET, ownedApiHandler)');
const integrationWildcardAt = network.indexOf(
    'registerHandler(server_, "/api/v1/integration*", HTTP_GET,');
const claimAt = network.indexOf(
    'registerHandler(server_, "/api/v1/ui/claim", HTTP_POST, claimHandler)');
const otaAt = network.indexOf(
    'registerHandler(server_, "/api/v1/ota", HTTP_GET, otaStatusHandler)');
if (!network.includes('config.uri_match_fn = httpd_uri_match_wildcard') ||
    ownedWildcardAt < 0 || integrationWildcardAt < 0 || claimAt < 0 || otaAt < 0 ||
    claimAt > integrationWildcardAt || integrationWildcardAt > ownedWildcardAt ||
    otaAt > ownedWildcardAt) {
  throw new Error(
      'Exact claim/OTA and integration wildcard routes must precede the owned API wildcard');
}
for (const method of ['GET', 'POST', 'PUT']) {
  if (!network.includes(
      `registerHandler(server_, "/api/v1/integration*", HTTP_${method},`) ||
      !network.includes(
      `registerHandler(server_, "/api/v1/*", HTTP_${method}, ownedApiHandler)`)) {
    throw new Error(`Missing ordered API wildcard registration for ${method}`);
  }
}

const diagnosticButtons = new Map();
const diagnosticProfilerBindings = ui.slice(
    ui.indexOf('for(const[id,path,field]'), ui.indexOf("$('scaleProfileDeleteButton').onclick"));
const diagnosticResetStart = ui.indexOf("for(const[id,path]of[['loopMax'");
const diagnosticResetBindings = ui.slice(
    diagnosticResetStart, ui.indexOf('}export function activate()', diagnosticResetStart));
new Function('$', 'R', diagnosticProfilerBindings + diagnosticResetBindings)(
    id => {
      const button = {};
      diagnosticButtons.set(id, button);
      return button;
    }, {command: (uri, payload) => [uri, payload]});
const diagnosticBoundRoutes = new Set();
for (const [id, uri, payload] of [
  ['taskProfilerStartButton', '/api/v1/diagnostic/profiler', {enabled: true}],
  ['taskProfilerStopButton', '/api/v1/diagnostic/profiler', {enabled: false}],
  ['scaleProfileStartButton', '/api/v1/diagnostic/scale-profile', {action: 'start'}],
  ['scaleProfileStopButton', '/api/v1/diagnostic/scale-profile', {action: 'stop'}],
  ['loopMaxResetButton', '/api/v1/diagnostic/loop-max/reset', {}],
  ['scaleGapMaxResetButton', '/api/v1/diagnostic/scale-gap-max/reset', {}],
]) {
  const result = diagnosticButtons.get(id)?.onclick();
  if (JSON.stringify(result) !== JSON.stringify([uri, payload])) {
    throw new Error(`Diagnostic button sends the wrong command: ${id}`);
  }
  diagnosticBoundRoutes.add(result[0]);
}

for (const [route, handler] of expected) {
  const [method, uri] = route.split(' ');
  const escapedUri = uri.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
  if (handler === 'ownedApiHandler') {
    const tableRoute = new RegExp(
        `\\{"${escapedUri}",\\s*HTTP_${method},\\s*[A-Za-z0-9_]+Handler`);
    if (!tableRoute.test(network)) {
      throw new Error(`Missing owned API dispatch entry: ${route}`);
    }
  } else {
    const registration = new RegExp(
      `registerHandler\\(server_,\\s*"${escapedUri}",\\s*HTTP_${method},\\s*${handler}\\)`
    );
    if (!registration.test(network)) {
      throw new Error(`Missing HTTP registration: ${route} -> ${handler}`);
    }
  }
  if (uri !== '/' && !ui.includes(uri.split('?')[0]) && !diagnosticBoundRoutes.has(uri)) {
    const statusPage = uri.match(/^\/api\/v1\/status\/(home|settings|admin|diagnostic)$/);
    const lazyAsset = uri.match(/^\/(partials|js)\//);
    const browserIcon = uri === '/favicon.ico' ||
        uri.startsWith('/apple-touch-icon');
    const rawLastShotApi = uri === '/api/v1/last-shot/clear';
    const directScaleSelectionApi = uri === '/api/v1/scale/preferred/select';
    if (!(statusPage && ui.includes('async function loadStatus(') && ui.includes('/api/v1/status/')) &&
        !(lazyAsset && (ui.includes('/partials/') || ui.includes('/js/'))) &&
        !browserIcon && !rawLastShotApi && !directScaleSelectionApi) {
      throw new Error(`Registered API is not referenced by the UI: ${uri}`);
    }
  }
}

const forbiddenResponseFields = ['staPassword', 'devicePassword', 'authHash', 'authSalt'];
const statusHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::statusHandler');
const statusHandlerEnd = network.indexOf('\nnamespace {', statusHandlerStart);
if (statusHandlerStart < 0 || statusHandlerEnd < 0) {
  throw new Error('Status handler not found');
}
const statusFormat = network.slice(statusHandlerStart, statusHandlerEnd);
for (const field of forbiddenResponseFields) {
  if (statusFormat.includes('\\"' + field + '\\"') ||
      statusFormat.includes('"' + field + '"')) {
    throw new Error(`Secret field exposed by status JSON: ${field}`);
  }
}
const soundAlertStatusFields =
    statusFormat.match(/\\"soundAlertsEnabled\\":%s/g) || [];
if (soundAlertStatusFields.length !== 1 ||
    !network.includes('delta.field("config.soundAlertsEnabled"') ||
    !statusFormat.includes('page == StatusPage::Settings')) {
  throw new Error(
      'soundAlertsEnabled must be projected by the Home stream and status/settings');
}
const alertChannelStatusFields =
    statusFormat.match(/\\"alertOutputChannel\\":\\"%s\\"/g) || [];
if (alertChannelStatusFields.length !== 1 ||
    !statusFormat.includes('page == StatusPage::Settings')) {
  throw new Error(
      'alertOutputChannel must remain available in status/settings');
}
// Shared status envelope: firmware/bootId/mutable/liveShot/ringRetain only.
// NTP → admin; serialDebug/diagnostics → diagnostic; buzzerSupported → settings.
if (!statusFormat.includes('{\\"firmwareVersion\\":\\"%s\\",\\"bootId\\":%lu,\\"configMutable\\":%s,') ||
    !statusFormat.includes('\\"webUiOverrideActive\\":%s,\\"webUiOverrideRemainingMs\\":%lu,') ||
    !statusFormat.includes('\\"configLockReason\\":\\"%s\\",\\"liveShot\\":%s"') ||
    !ui.includes("typeof s.bootId==='number'") ||
    !ui.includes('updateFirmwareFooter()')) {
  throw new Error(
      'Status shared envelope must open with firmwareVersion/bootId/configMutable/webUiOverride/liveShot');
}
if (/buzzerSupported.*liveShot|liveShot.*buzzerSupported/.test(
        statusFormat.slice(
            statusFormat.indexOf('{\\"firmwareVersion\\"'),
            statusFormat.indexOf('{\\"firmwareVersion\\"') + 200))) {
  throw new Error('buzzerSupported must not ride in the shared status open append');
}
if (!statusFormat.includes('page == StatusPage::Settings') ||
    !statusFormat.includes(',\\"buzzerSupported\\":%s') ||
    statusFormat.includes('StatusPage::Debug')) {
  throw new Error('buzzerSupported must be gated to status settings only');
}
if (ui.includes("statusPageOk(v,s)") && !ui.includes("v!=='diagnostic'&&!statusPageOk(v,s)")) {
  throw new Error('statusPageOk must not validate a diagnostic REST payload');
}
if (network.includes('page == StatusPage::Diagnostic')) {
  throw new Error('diagnostic metrics must not ride the REST status pages');
}
{
  const homeStreamSource = fs.readFileSync(
      path.join(sketchDir, 'network', 'ShotStopperHomeStream.inc'), 'utf8');
  const diagStart = homeStreamSource.indexOf('sendDiagnosticStream');
  const diagEnd = homeStreamSource.indexOf('sendLogStream', diagStart);
  const diagRegion = homeStreamSource.slice(diagStart, diagEnd);
  for (const field of [
    'apActive', 'apSsid', 'apIp', 'apClients', 'wifiConfigured', 'ssid', 'staState',
    'wifiPs', 'wifiCoex', 'channel', 'staIp', 'ipMode', 'configState', 'confirmRemainingMs', 'rssi',
    'signalQualityPct', 'lastSyncUtcSec',
    'activeServer', 'maintenance', 'persistPending', 'uptimeMs', 'hwmon',
    'minimumFreeHeapBytes', 'largestFreeHeapBlockBytes',
    'internalHeapAllocatedBlocks', 'internalHeapFreeBlocks',
    'internalHeapFragmentationPermille',
    'psramSizeBytes', 'psramFreeBytes', 'psramLargestFreeBlockBytes',
    'resetReasonCode', 'packetGaps', 'rejectedPackets', 'reconnects',
    'eventsDropped', 'recoveredStaleCount', 'recoveredStaleMs',
    'weightUpdateIntervalMs', 'maxPacketGapMs',
    'lastCommand', 'loopMaxGapMs',
    'machineState', 'physicalActivatorOn', 'reedOn', 'controlSource', 'cupPresence',
    'streamState', 'controlState', 'taskWatchdogReady', 'recoveryRequired',
    'compileFlags', 'remoteMachineControl', 'hardwareProfile', 'machineProfile',
    'machineBrand', 'machineModel', 'serial', 'io4',
    'crashCount', 'resetHistory', 'supportedCommands', 'guards'
  ]) {
    if (!diagRegion.includes(field)) {
      throw new Error('diagnostic stream missing required field: ' + field);
    }
  }
  if (!diagRegion.includes('delta.field("guards.atm.remainingMs", "%lu",') ||
      !diagRegion.includes('/ 1000 * 1000') ||
      !diagRegion.includes('/ 60000 * 60000') ||
      !diagRegion.includes('kDiagSlowMs') ||
      !diagRegion.includes('captureNvsDiagnostics') ||
      !diagRegion.includes('crashArchiveStatus') ||
      !diagRegion.includes('statusJsonAppendTaskProfiler') ||
      !diagRegion.includes('statusJsonAppendScaleProfiler')) {
    throw new Error(
        'diagnostic stream must quantize countdowns, gate sampled producers, and reuse the profiler builders');
  }
  if (!diagRegion.includes('scaleBookooCommandAt(control.scaleModel') ||
      !diagRegion.includes('commandsJson') ||
      !html.includes('id="scaleCommandTable"') ||
      !html.includes('id="scaleCommandRows"') ||
      !ui.includes('applyScaleCommands(sc)') ||
      !bleLibrary.includes('case ScaleOp::PowerOff: return 0x15;')) {
    throw new Error('Diagnostic scale command table must use current library model and wire codes');
  }
  // Transversal fields used by Diagnostic (footer + log controls + mutability)
  if (!homeStreamSource.includes('delta.field("firmwareVersion"') ||
      !homeStreamSource.includes('delta.field("bootId"') ||
      !homeStreamSource.includes('delta.field("configMutable"') ||
      !homeStreamSource.includes('delta.field("liveShot"') ||
      !diagRegion.includes('config.ringRetainLogLevel') ||
      !diagRegion.includes('config.appliedTimezoneOffsetMinutes') ||
      !ui.includes("typeof s.bootId==='number'") ||
      !ui.includes('function applyDiagnosticStatus(') ||
      !ui.includes('dBz') ||
      !ui.includes('dCircuit') ||
      !ui.includes('dArch') ||
      !ui.includes("'HW: '+f.hardwareProfile") ||
      !ui.includes("'Machine: '+f.machineBrand+' '+f.machineModel") ||
      !ui.includes('dSerialIo4') ||
      !ui.includes('dSerialState') ||
      !ui.includes("enabled_jtag:'Enabled (compile flag)'") ||
      !ui.includes("enabled_io4:'Enabled (IO04)'") ||
      !ui.includes('Compile flags') ||
      !ui.includes('s.compileFlags') ||
      !html.includes('paddleOnly') ||
      !html.includes('id="dMt"') ||
      !html.includes('<legend>Serial</legend>') ||
      !css.includes('html.momentaryMachine .paddleOnly') ||
      !css.includes('.momentaryOnly') ||
      !ui.includes('function applyMachineTypeUi(')) {
    throw new Error(
        'diagnostic stream must keep transversal bootId/firmware/liveShot/ringRetain for the Diagnostic page');
  }
  for (const forbidden of [
    'configuredIp', 'configuredNetmask', 'configuredGateway', 'configuredDns1',
    'configuredDns2'
  ]) {
    if (diagRegion.includes(forbidden)) {
      throw new Error(
          'diagnostic stream must not include Admin-only network field: ' +
          forbidden);
    }
  }
  if (diagRegion.includes('wifiSleep') || diagRegion.includes('staWifiSleep')) {
    throw new Error('diagnostic stream must report live wifiPs, not wifiSleep config');
  }
  if (!diagRegion.includes('wifiPsLiveName(network.wifiPs)') ||
      !network.includes('status_.wifiPs = wifiPs') ||
      !network.includes('esp_wifi_get_ps(&ps)') ||
      !domainCore.includes('wifiPsLiveName')) {
    throw new Error('diagnostic stream must include live wifiPs from the driver');
  }
  if (!diagRegion.includes('rfCoexPreferenceName(network.wifiCoex)') ||
      !network.includes('status_.wifiCoex = snapshotRfCoexPreference()') ||
      !firmware.includes('rfCoexPreferenceName') ||
      !firmware.includes('snapshotRfCoexPreference')) {
    throw new Error('diagnostic stream must include live BT/Wi-Fi coex preference');
  }
  if (diagRegion.includes('ntpServerPreset') ||
      diagRegion.includes('ntpServerCustom') ||
      diagRegion.includes('buzzerSupported') ||
      diagRegion.includes('presets') ||
      diagRegion.includes('brewByWeight')) {
    throw new Error(
        'diagnostic stream must not include settings/admin-only payload fields');
  }
  if (/\\\"open\\\"|\\"open\\"/.test(diagRegion)) {
    throw new Error('diagnostic stream must not include Admin-only network open flag');
  }
}
if (!network.includes('ShotStopperDebugExport.h') ||
    !network.includes('DEBUG_EXPORT_SCHEMA_VERSION') ||
    !network.includes('exportSchemaVersion') ||
    !network.includes('debugExportHandler') ||
    !network.includes('/api/v1/debug/export') ||
    !network.includes('\\"guards\\"') ||
    !network.includes('\\"bbwEnabled\\"') ||
    !firmware.includes('copyDebugExportExtras') ||
    !firmwareCore.includes('const ControlStatusSnapshot &control') ||
    !network.includes(
        'self.callbacks_.copyDebugExportExtras(work.debugExport, work.control)') ||
    !network.includes(
        'return sendChunk(request, text, strlen(text)) == ESP_OK') ||
    network.includes('httpd_resp_send_chunk(request, text, HTTPD_RESP_USE_STRLEN)') ||
    !firmware.includes('ShotStopperDebugExport.h') ||
    !ui.includes('/api/v1/debug/export') ||
    !ui.includes('exportDebugDataButton') ||
    !html.includes('id="exportDebugDataButton"') ||
    network.slice(
        network.indexOf('esp_err_t ShotStopperNetwork::debugExportHandler'),
        network.indexOf('esp_err_t ShotStopperNetwork::debugExportHandler') +
            450)
        .includes('requireAdminUnlock(request)')) {
  throw new Error(
      'Diagnostic must expose guards status and an always-public GET /api/v1/debug/export with schema version');
}
const sharedRingOpen = statusFormat.indexOf(
    ',\\"config\\":{\\"revision\\":%lu,\\"ringRetainLogLevel\\":\\"%s\\"');
if (sharedRingOpen < 0) {
  throw new Error(
      'Status shared config must open with revision then ringRetainLogLevel');
}
const ringOpenSlice = statusFormat.slice(sharedRingOpen, sharedRingOpen + 180);
if (ringOpenSlice.includes('timezoneOffsetMinutes') ||
    ringOpenSlice.includes('ntpServerPreset') ||
    ringOpenSlice.includes('serialDebugOutput')) {
  throw new Error(
      'NTP/serialDebug must not share the revision/ringRetainLogLevel open append');
}
if (!ui.includes(
        "typeof s.buzzerSupported==='boolean')updateBuzzerAlertVisibility") &&
    !ui.includes(
        'typeof s.buzzerSupported==="boolean")updateBuzzerAlertVisibility')) {
  throw new Error(
      'applyCommonStatus must only update buzzer visibility when buzzerSupported is present');
}

// The serial log rides the owned UI stream: the retired REST handler's
// invariants move to the stream sender.
const logStreamStart = network.indexOf('bool ShotStopperNetwork::sendLogStream');
const logStreamRegion = network.slice(
    logStreamStart, network.indexOf('\n}\n', logStreamStart));
if (logStreamStart < 0) {
  throw new Error('Log stream sender not found');
}
if (logStreamRegion.includes('requireAdminUnlock(request)') ||
    logStreamRegion.includes('authenticate(request')) {
  throw new Error('Diagnostic log frames must stay behind the owned claim');
}
if (!logStreamRegion.includes('escapeJsonString(message, work.jsonItem')) {
  throw new Error('Log event messages must be JSON-escaped before framing');
}
for (const field of forbiddenResponseFields) {
  if (logStreamRegion.includes(field)) {
    throw new Error('Secret field exposed by diagnostic log: ' + field);
  }
}
if (!logStreamRegion.includes('debugLogLatestSequence()') ||
    !logStreamRegion.includes('logPush') ||
    !networkHeader.includes('uint32_t logAfter = 0;')) {
  throw new Error('Log frames must be watermark-gated with cursor paging');
}

if (/AP_WINDOW_MS/.test(networkHeader) || /AP_WINDOW_MS/.test(network)) {
  throw new Error('Idle SoftAP shutdown window must remain removed');
}
if (!network.includes('WiFi.mode(WIFI_STA)') ||
    !network.includes('WiFi.mode(WIFI_AP)') ||
    !network.includes('WIFI_AP_STA') ||
    !network.includes('ensureAccessPoint') ||
    !network.includes('beginStationConnect') ||
    !network.includes('WIFI_ALL_CHANNEL_SCAN') ||
    !network.includes('WIFI_CONNECT_AP_BY_SIGNAL') ||
    !network.includes('setScanMethod') ||
    !network.includes('setSortMethod') ||
    !network.includes('all-channel RSSI sort') ||
    !network.includes('brewRfActive()') ||
    !network.includes('associate deferred; brew RF active') ||
    !network.includes('associate aborted; brew RF active') ||
    !network.includes('STA reconnect deferred; brew RF active') ||
    !network.includes('associate deferred; scale connecting') ||
    !network.includes('associate aborted; scale connecting') ||
    !network.includes('STA reconnect deferred; scale connecting') ||
    network.includes('esp_coex_preference_set') ||
    network.includes('findBestStaCandidate') ||
    !network.includes('stopSoftApKeepStation') ||
    !network.includes('wifiScanInProgress') ||
    !network.includes('STA_RECOVERY_ATTEMPT_MS')) {
  throw new Error(
      'Network must use STA-first boot, SoftAP when unassociated, WIFI_AP_STA while retrying STA, pause retries during Wi-Fi scan, and associate via IDF all-channel RSSI sort (no incomplete BSSID lock)');
}
{
  const beginStaStart = network.indexOf(
      'bool ShotStopperNetwork::beginStationConnect');
  const beginStaEnd = network.indexOf(
      'void ShotStopperNetwork::startStation', beginStaStart);
  const beginSta = beginStaStart >= 0 && beginStaEnd > beginStaStart
      ? network.slice(beginStaStart, beginStaEnd)
      : '';
  if (!beginSta.includes('WiFi.disconnect(false, false)') ||
      !beginSta.includes('brewRfActive()') ||
      !beginSta.includes('scaleConnecting_') ||
      beginSta.includes('WIFI_OFF')) {
    throw new Error(
        'beginStationConnect must disconnect STA before reassociate without WIFI_OFF and defer while brew RF or scale connecting');
  }
}
{
  const abortAt = network.indexOf('associate aborted; brew RF active');
  const abortSlice = abortAt >= 0 ? network.slice(Math.max(0, abortAt - 500), abortAt) : '';
  if (!abortSlice.includes('staState == StaState::CONNECTING') ||
      !abortSlice.includes('WiFi.status() != WL_CONNECTED')) {
    throw new Error(
        'STA associate abort during brew must skip a radio that already has an IP');
  }
}
{
  const ntpArmStart = network.indexOf('bool ShotStopperNetwork::ntpMayArm');
  const ntpArmEnd = network.indexOf('bool ShotStopperNetwork::armNtp', ntpArmStart);
  const ntpArm = ntpArmStart >= 0 && ntpArmEnd > ntpArmStart
      ? network.slice(ntpArmStart, ntpArmEnd)
      : '';
  if (!ntpArm.includes('brewRfActive()') ||
      !ntpArm.includes('scaleConnecting_')) {
    throw new Error(
        'ntpMayArm must not start SNTP while brew RF is active or scale is connecting');
  }
  const serviceNtpStart = network.indexOf('void ShotStopperNetwork::serviceNtp');
  const serviceNtpEnd = network.indexOf('bool ShotStopperNetwork::startHttpServer',
                                        serviceNtpStart);
  const serviceNtp = serviceNtpStart >= 0 && serviceNtpEnd > serviceNtpStart
      ? network.slice(serviceNtpStart, serviceNtpEnd)
      : '';
  const gateAbortAt = serviceNtp.indexOf('if (!ntpMayArm(');
  const syncingAt = serviceNtp.indexOf('TimeSyncState::SYNCING');
  if (gateAbortAt < 0 || syncingAt < 0 || gateAbortAt > syncingAt ||
      !serviceNtp.includes('cancelSyncing()') ||
      !serviceNtp.includes('ntpRearmPending_ = true') ||
      !serviceNtp.includes('stopNtp()')) {
    throw new Error(
        'serviceNtp must abort in-flight SNTP under ntpMayArm gate before SYNCING handling');
  }
  if (!network.includes('ntpCallbackAccepting_') ||
      !network.includes('syncControlCriticalRf') ||
      !network.includes('rfGateGeneration_') ||
      !network.includes('expectedGateGeneration') ||
      !network.includes('if (!gateStable())') ||
      !network.includes('abortNtpForRfGate();') ||
      !network.includes('void ShotStopperNetwork::abortNtpForRfGate()') ||
      !network.includes('ntpAbortRequested_') ||
      !network.includes(
          'ntpCallbackAccepting_.store(false, std::memory_order_release)') ||
      !network.includes('ulTaskNotifyTake') ||
      !network.includes('xTaskNotifyGive') ||
      !serviceNtp.includes('if (g_wallClock.applyPendingSync(now))') ||
      !serviceNtp.includes('stopNtp();') ||
      !serviceNtp.includes('timeStatus.lastSyncAgeMs >= NTP_RESYNC_INTERVAL_MS') ||
      !serviceNtp.includes('armNtp(now, staConnected, gateGeneration)') ||
      serviceNtp.includes('applySystemTimeToWallClock')) {
    throw new Error(
        'SNTP must be callback-driven one-shot work, wake promptly for gates, stop after sync, and rearm only through gated service');
  }
}
if (!domain.includes('selectBestStaAp') ||
    !domain.includes('StaApScanEntry')) {
  throw new Error('Strongest-AP selection helpers must live in Domain for host tests');
}
if (!network.includes('staBssid') ||
    !serialCli.includes('staBssid=')) {
  throw new Error('Associated AP BSSID must be exposed on status and WIFI_STATUS');
}
if (!serialCli.includes('workBufExternal=') ||
    !serialCli.includes('jsonArenaExternal=') ||
    !serialCli.includes('allocExternalFallback=')) {
  throw new Error(
      'Serial HEALTH must report WorkBuf/JSON arena placement and allocExternalFallback');
}
if (!network.includes('WiFi.scanNetworks(true, false, false, 120)') ||
    !network.includes('esp_wifi_scan_stop()') ||
    !network.includes('abortWifiScan') ||
    !network.includes('WIFI_SCAN_TIMEOUT_MS') ||
    !network.includes('WiFi.getScanInfoByIndex(') ||
    network.includes('esp_wifi_scan_get_ap_records')) {
  throw new Error(
      'WiFi scan must be asynchronous, cancelable, abortable on mode change, time-bounded, and read Arduino SCAN_DONE results (not a second IDF AP fetch)');
}
if (network.includes('recycleHttpServer') ||
    network.includes('noteHttpServeResult') ||
    network.includes('recoverFromResourcePressure') ||
    network.includes('associated without IP')) {
  throw new Error(
      'HTTP recycle / sticky no-IP recovery must stay removed — they kill ping when WebUI opens');
}
if (network.includes('networkShutdownPending_') ||
    /networkShutdownPending_\s*=\s*age\s*>=/.test(network)) {
  throw new Error('SoftAP must not shut down on an idle visibility timer');
}
if (!network.includes('never auto-raise SoftAP') ||
    !network.includes('on link loss') ||
    !network.includes('SoftAP via AP_START or reboot')) {
  throw new Error(
      'serviceSessions must keep SoftAP up without idle shutdown; post-CONNECTED link loss must not auto-raise SoftAP');
}
const stopSoftApStart = network.indexOf(
    'void ShotStopperNetwork::stopSoftAp(');
const stopSoftApKeepStart = network.indexOf(
    'void ShotStopperNetwork::stopSoftApKeepStation()');
const stopSoftApEnd = network.indexOf(
    'bool ShotStopperNetwork::wifiScanInProgress()', stopSoftApKeepStart);
if (stopSoftApStart < 0 || stopSoftApKeepStart < 0 || stopSoftApEnd < 0) {
  throw new Error('stopSoftAp / stopSoftApKeepStation implementation not found');
}
const stopSoftAp = network.slice(stopSoftApStart, stopSoftApEnd);
if (stopSoftAp.includes('WiFi.mode(WIFI_STA)')) {
  throw new Error(
      'stopSoftApKeepStation must not force WIFI_STA and risk dropping the STA link');
}
if (!stopSoftAp.includes('stopHttpServer()')) {
  throw new Error(
      'stopSoftAp must be able to stop HTTP so STA rebind can restart the server');
}
if (!network.includes('stopSoftApLeaveHttp') ||
    !network.includes('httpStartHeld_') ||
    !network.includes('staReconnectHeld_') ||
    !network.includes('apStartHeld_') ||
    !network.includes('apKeepRequested_')) {
  throw new Error(
      'Network CLI holds must keep SoftAP/HTTP/STA stop from being undone');
}
const serviceStart = network.indexOf('void ShotStopperNetwork::service()');
const serviceEnd = network.indexOf(
    'bool ShotStopperNetwork::controlAllowsNetworkMutation', serviceStart);
if (serviceStart < 0 || serviceEnd < 0) {
  throw new Error('ShotStopperNetwork::service implementation not found');
}
const serviceBody = network.slice(serviceStart, serviceEnd);
const processAt = serviceBody.indexOf('processAcceptedCommands()');
const startupReturnAt = serviceBody.indexOf('if (!startupComplete_)');
if (processAt < 0 || startupReturnAt < 0 || processAt > startupReturnAt) {
  throw new Error(
      'CLI network actions must drain before the startupComplete_ early return');
}
if (!network.includes('!apKeepRequested_')) {
  throw new Error(
      'STA-up SoftAP teardown must keep a user AP_START SoftAP');
}
const ensureStart = network.indexOf(
    'bool ShotStopperNetwork::ensureAccessPoint');
const ensureEnd = network.indexOf(
    'void ShotStopperNetwork::stopNetwork()', ensureStart);
if (ensureStart < 0 || ensureEnd < 0) {
  throw new Error('ensureAccessPoint implementation not found');
}
const ensureBody = network.slice(ensureStart, ensureEnd);
if (!ensureBody.includes('httpStartHeld_') ||
    !ensureBody.includes('staLinkUp') ||
    !ensureBody.includes('keepHttp')) {
  throw new Error(
      'ensureAccessPoint must keep a live STA link and skip HTTP while WEBUI_STOP is held');
}
const snapshotStart = network.indexOf(
    'void ShotStopperNetwork::printActionSnapshot');
const snapshotEnd = network.indexOf(
    'void ShotStopperNetwork::noteCliNetworkProgress()', snapshotStart);
if (snapshotStart < 0 || snapshotEnd < 0 ||
    !network.slice(snapshotStart, snapshotEnd)
         .includes('refreshExtendedStatus')) {
  throw new Error(
      'CLI action snapshot must refresh holds after the mutation');
}
if (!network.includes('void ShotStopperNetwork::lifecycleLog(') ||
    network.includes('serialDebugEnabled()')) {
  throw new Error(
      'Network lifecycle logs must use the shared serial/ring logger');
}
if (network.includes('raising SoftAP and retrying STA') ||
    network.includes('retrying STA before SoftAP')) {
  throw new Error(
      'STA disconnect after prior connect must retry station without SoftAP auto-raise');
}
if (!network.includes('no SoftAP after prior connect') ||
    !network.includes('SoftAP suppressed after prior connect') ||
    !network.includes('!staEverConnected_') ||
    !network.includes('STA_CONNECT_TIMEOUT_MS')) {
  throw new Error(
      'SoftAP auto-raise must gate on !staEverConnected_ and wait STA_CONNECT_TIMEOUT only for boot/bootstrap');
}
if (network.includes('staEverConnected_ = false')) {
  throw new Error(
      'staEverConnected_ must latch for process lifetime (never clear after first CONNECTED)');
}
if (!network.includes('SoftAP suppressed (AP_START or reboot)') ||
    !networkHeader.includes('Latched for process lifetime')) {
  throw new Error(
      'Pending revert / startStation must keep SoftAP boot-only after prior STA join');
}
if (!network.includes('!status.apActive') ||
    !network.includes('STA_CONNECT_TIMEOUT_MS')) {
  throw new Error(
      'STA connect timeout must remain SoftAP bootstrap-only when AP is inactive');
}
if (network.includes('\\"passwordChangeRequired\\"') ||
    network.includes('PASSWORD_CHANGE_REQUIRED') ||
    ui.includes('passwordChangeRequired') ||
    ui.includes('factory AP/UI password') ||
    ui.includes('Change the factory AP/UI password')) {
  throw new Error('Factory password change gate must remain removed from status/UI/API');
}

const safeBeepStart = bleLibrary.indexOf('EspressoScaleBLE::beepWithoutStateChange()');
const safeBeepEnd = bleLibrary.indexOf('EspressoScaleBLE::setBeepLevel(', safeBeepStart);
if (safeBeepStart < 0 || safeBeepEnd < 0) {
  throw new Error('State-safe BLE beep implementation not found');
}
const safeBeep = bleLibrary.slice(safeBeepStart, safeBeepEnd);
if (!safeBeep.includes('return beepWithoutStateChange(nullptr)') ||
    !safeBeep.includes('return setBeepLevel(1, admission)') ||
    !bleLibrary.includes('case ScaleOp::SetVolume: return 0x02;') ||
    !bleLibrary.includes('fillGenericCommand') ||
    safeBeep.includes('BEEP_LEVEL_1_BOOKOO') ||
    safeBeep.includes('TARE_ACAIA') || safeBeep.includes('TARE_GENERIC') ||
    safeBeep.includes('_connected = false')) {
  throw new Error('First-drop beep must not tare or mutate scale connection state');
}
if (!firmware.includes('emitAlert(AlertEvent::FIRST_DROP') ||
    !firmware.includes('emitAlert(AlertEvent::SCALE_CONNECTED') ||
    !firmware.includes('emitAlert(AlertEvent::SCALE_LOST') ||
    !alertTone.includes('BuzzerCue::SCALE_CONNECTED') ||
    !alertTone.includes('BuzzerCue::SCALE_LOST') ||
    !firmware.includes('requestScaleBrewBeep(') ||
    !firmware.includes('cancelScaleBrewBeep(session.id)') ||
    !firmware.includes('onFirstDropsDetected') ||
    !firmware.includes('notifyRetareFlowDetected') ||
    !firmware.includes('retareFlowFirstDetectedAtMs') ||
    !firmware.includes('bbwProtectionActive') ||
    !firmware.includes('classifyAccidentalTouch') ||
    !firmware.includes('stepFirstFlow') ||
    !firmware.includes('accidentalTouchHolding') ||
    !firmware.includes('retareWindowOpen') ||
    !firmware.includes('ScaleFeatureCombinedTareStart') ||
    !firmware.includes('alertOutputChannel') ||
    !firmware.includes('applyBookooConnectBeepPolicy') ||
    !firmware.includes('armBookooConnectBeepPolicy') ||
    !firmware.includes('serviceBookooConnectBeepPolicy') ||
    !firmware.includes('requestBookooSilenceIfConfigured') ||
    !firmware.includes('emitCommandAlert') ||
    !firmware.includes('emitImmediateCommandAlertIfBuzzer') ||
    !firmware.includes('emitCircuitCycleAlert') ||
    !firmware.includes('commandAlertUsesBuzzer') ||
    /enum class ScaleCommandType[\s\S]*BEEP/.test(
      firmware.slice(firmware.indexOf('enum class ScaleCommandType'),
                     firmware.indexOf('enum class ScaleEventType')))) {
  throw new Error('Best-effort beep must stay outside the critical BLE command queue');
}

const emitCommandImplStart = firmwareCore.indexOf(
    '// BLE-result fallback only');
const emitCommandImplEnd = firmwareCore.indexOf(
    'void requestCompletionAlert()', emitCommandImplStart);
const emitCommandImpl = emitCommandImplStart < 0 || emitCommandImplEnd < 0
    ? ''
    : firmware.slice(emitCommandImplStart, emitCommandImplEnd);
if (!alertChannel.includes('AlertKind::CommandImmediate') ||
    !alertChannel.includes('AlertKind::CommandFallback') ||
    !alertChannel.includes('AlertOutputChannel::BUZZER_ONLY') ||
    emitCommandImpl.includes('if (commandAlertUsesBuzzer())') ||
    /if \(channel == AlertOutputChannel::BUZZER_ONLY\) \{\s*emitLocalAlertBuzzer/.test(
        emitCommandImpl)) {
  throw new Error('Buzzer-routed command alerts must not wait for BLE results');
}
const immediateCommandAlertCalls =
    firmware.split('emitImmediateCommandAlertIfBuzzer(').length - 1;
const circuitCycleAlertCalls = firmware.split('emitCircuitCycleAlert(').length - 1;
if (immediateCommandAlertCalls < 3 || circuitCycleAlertCalls < 4 ||
    !firmware.includes(
        'emitCircuitCycleAlert(session.startedWithScale && session.config.autoTare &&\n                            session.config.canTareStartTimer\n                        ? AlertEvent::TARE_START\n                        : AlertEvent::START_TIMER,\n                    true);') ||
