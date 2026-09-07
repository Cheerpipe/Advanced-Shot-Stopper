{
  const machineFn = runtimeJs.slice(
      runtimeJs.indexOf('function machinePayload('),
      runtimeJs.indexOf('function dateTimePayload('));
  const dateTimeFn = runtimeJs.slice(
      runtimeJs.indexOf('function dateTimePayload('),
      runtimeJs.indexOf('function brewPayload('));
  if (!machineFn.includes("scaleConnectedLed:$('scaleConnectedLed').checked") ||
      machineFn.includes('ntpServerPreset') ||
      machineFn.includes('timezoneOffsetMinutes') ||
      machineFn.includes('serialDebugOutput') ||
      machineFn.includes('ringRetainLogLevel') ||
      machineFn.includes('goalWeightG') ||
      machineFn.includes('brewByWeight') ||
      !dateTimeFn.includes('timezoneOffsetMinutes') ||
      !dateTimeFn.includes('ntpServerPreset') ||
      !dateTimeFn.includes('ntpServerCustom') ||
      dateTimeFn.includes('scaleConnectedLed') ||
      !ui.includes("saveDateTimeButton').onclick=R.saveDateTimeConfig") ||
      ui.includes("saveDateTimeButton').onclick=R.saveMachineConfig") ||
      (ui.includes("function markConfigDirty(") &&
       runtimeJs.slice(runtimeJs.indexOf('function markConfigDirty('),
                       runtimeJs.indexOf('function markDateTimeDirty('))
           .includes('dateTimeDirtyHint'))) {
    throw new Error(
        'Save machine must send only machine fields; Date & time has its own payload and dirty flag');
  }
}
if (!(ui.includes("if($('serialDebugOutput'))$('serialDebugOutput').checked=!!c.serialDebugOutput") ||
         ui.includes("$('serialDebugOutput').checked=!!c.serialDebugOutput")) ||
    !(ui.includes("if($('ringRetainLogLevel'))$('ringRetainLogLevel').value=c.ringRetainLogLevel||'none'") ||
         ui.includes("$('ringRetainLogLevel').value=c.ringRetainLogLevel||'none'")) ||
    firmware.indexOf('publishLogLevels(serialLogLevelFromRuntime(runtimeConfig)',
                     firmware.indexOf('persistenceReady = EEPROM.begin')) < 0 ||
    firmware.indexOf('publishLogLevels(serialLogLevelFromRuntime(runtimeConfig)',
                     firmware.indexOf('persistenceReady = EEPROM.begin')) >
        firmware.indexOf('BOOT_RESET_REASON') ||
    !ui.includes("serialLogLevel:$('serialLogLevel').value") ||
    !ui.includes("ringRetainLogLevel:$('ringRetainLogLevel').value||'none'") ||
    !ui.includes("serialLogLevel').onchange") ||
    !ui.includes("ringRetainLogLevel').onchange") ||
    !ui.includes('baseRevision') ||
    !network.includes('ringRetainLogLevel') ||
    !html.includes('id="ruleChart"') ||
    !html.includes('id="ruleChartTimeTrack"') ||
    !html.includes('id="ruleChartWeightTrack"') ||
    html.indexOf('id="ruleChart"') < html.indexOf('id="homePresetCards"') ||
    html.indexOf('id="ruleChart"') > html.indexOf('id="shotPanel"') ||
    html.includes('Extraction rules') ||
    !css.includes('.ruleChart') ||
    !css.includes('.ruleSeg-fast') ||
    !css.includes('.ruleSeg-bbw') ||
    !css.includes('.ruleSeg-slow') ||
    !css.includes('.ruleChartIdle') ||
    !ui.includes('function buildRuleChartModel(') ||
    !ui.includes('function renderRuleChart(') ||
    !ui.includes('function updateRuleChartFromStatus(') ||
    !ui.includes('updateRuleChartFromStatus(s)') ||
    !ui.includes('bbw&&!!c.fastExtractionGuardEnabled') ||
    !ui.includes('bbw&&!!c.slowExtractionGuardEnabled') ||
    !ui.includes("mode:'timerOnly'") ||
    !ui.includes("['fast'") ||
    !ui.includes("['bbw'") ||
    !ui.includes("['slow'") ||
    !ui.includes("['idle'") ||
    !ui.includes("if($('ruleChartPreset'))$('ruleChartPreset').textContent=''") ||
    !ui.includes("if($('ruleChartMode'))$('ruleChartMode').textContent=''") ||
    !firmware.includes('SERIAL_DEBUG_ON') ||
    !firmware.includes('SERIAL_DEBUG_OFF') ||
    !firmware.includes('DEBUG_FULL') ||
    !firmware.includes('DEBUG_OFF') ||
    !firmware.includes('DEBUG_STATUS') ||
    !firmware.includes('WIFI_CONNECT') ||
    !firmware.includes('WIFI_DISCONNECT') ||
    !firmware.includes('WEBUI_STOP') ||
    !firmware.includes('LOG_DUMP') ||
    !firmware.includes('SCALE_STATUS') ||
    !firmware.includes('NTP_STATUS') ||
    !firmware.includes('NET_STATUS') ||
    !firmware.includes('WebCommandType::BUZZER_TEST') ||
    !firmware.includes('WebCommandType::BOOKOO_DEBUG') ||
    !firmware.includes('localBuzzer.request(command.buzzerPattern)') ||
    !firmware.includes('enqueueScaleDebugCommand') ||
    !firmware.includes('executeScaleDebugCommand')) {
  throw new Error('Ring/serial config, rule chart, CLI, and scale/buzzer command paths must remain');
}
if (!ui.includes('id="staIpMode"') ||
    !ui.includes('id="staStaticIp"') ||
    !ui.includes('id="staNetmask"') ||
    !ui.includes('id="staGateway"') ||
    !ui.includes('id="staDns1"') ||
    !ui.includes('function networkSavePayload(') ||
    !ui.includes("ipMode:$('staIpMode').value") ||
    !ui.includes('staticIpOpt') ||
    !ui.includes('pending confirm') ||
    !ui.includes('savedStaSsid') ||
    !ui.includes('Leave empty to keep the saved password') ||
    !ui.includes('keep=!!savedStaSsid') ||
    !ui.includes("savedStaSsid=n.wifiConfigured&&n.ssid?n.ssid:''") ||
    !ui.includes('function formatNetworkStatus(n)') ||
    !ui.includes("n.staState==='CONNECTED'&&typeof n.channel==='number'&&n.channel>0") ||
    !ui.includes("' — channel '+n.channel") ||
    !ui.includes("signalQualityPct") ||
    !ui.includes("n.rssi") ||
    !ui.includes('signal ') ||
    !ui.includes(' dBm)') ||
    !ui.includes("$('networkStatus').textContent=formatNetworkStatus(s.network)") ||
    !ui.includes("t('hSsid',n.ssid)") ||
    !ui.includes("t('hWifiState',n.staState)") ||
    !ui.includes("t('hWifiPs',n.wifiPs)") ||
    !ui.includes("t('hWifiCoex',n.wifiCoex)") ||
    !ui.includes("$('apStatus').textContent='AP: '+(s.network.apActive?'active':'inactive')") ||
    !ui.includes("t('hApState',n.apActive?'active':'inactive')") ||
    !html.includes('<legend>WiFi</legend>') ||
    !html.includes('<strong>Sleep</strong><div id="hWifiPs">') ||
    !html.includes('<strong>Coex</strong><div id="hWifiCoex">') ||
    !html.includes('<strong>SSID</strong><div id="hSsid">') ||
    !html.includes('<legend>AP</legend>') ||
    !network.includes('WiFi.config(') ||
    !network.includes('confirmPendingNetwork') ||
    !network.includes('revertPendingNetwork') ||
    !network.includes('\\"ipMode\\"') ||
    !network.includes('\\"configState\\"') ||
    !network.includes('\\"ssid\\"') ||
    !network.includes('\\"rssi\\"') ||
    !network.includes('\\"signalQualityPct\\"') ||
    !network.includes('\\"channel\\"') ||
    !network.includes('wifiRssiToSignalQualityPct') ||
    !network.includes('staLinkMetricsValid') ||
    !network.includes('WiFi.RSSI()') ||
    !network.includes('shouldReuseSavedWifiCredentials') ||
    !network.includes('or empty to keep the saved password.') ||
    !network.includes('StaIpMode::STATIC') ||
    !network.includes('STA_CONFIRM_TIMEOUT_MS') ||
    !network.includes('action must be \\"save\\", \\"forget\\", or \\"confirm\\".') ||
    !network.includes('No pending network configuration to confirm.')) {
  throw new Error('DHCP/static IP mode must be wired in UI, status, WiFi.config, and confirm/revert path');
}
if (!network.includes('restoreLkgToActive(next)') ||
    !network.includes('startStation(settings, now)')) {
  throw new Error('STA confirm timeout must reassociate last-known-good before SoftAP fallback');
}
{
  if (      !html.includes('id="staWifiSleep"') ||
      !html.includes('id="staWifiSleep" type="checkbox" checked') ||
      !html.includes('Wi-Fi sleep<small') ||
      !html.includes('Modem sleep while STA is associated') ||
      !html.includes('SoftAP and OTA keep the radio awake') ||
      !html.includes('May lag the Web UI') ||
      !ui.includes("wifiSleep:$('staWifiSleep').checked") ||
      !ui.includes("savedStaWifiSleep=!!n.wifiSleep") ||
      !ui.includes("if($('staWifiSleep'))$('staWifiSleep').checked=savedStaWifiSleep") ||
      !ui.includes("if(n.wifiSleep)t+=' — sleep on'") ||
      !ui.includes('function networkSaveIsSleepOnly(') ||
      !ui.includes('_noReconnectWait') ||
      !ui.includes('delete payload._noReconnectWait') ||
      !ui.includes('sleepOnly') ||
      !ui.includes('if(noReconnect)savedStaWifiSleep=') ||
      !ui.includes('if(!sleepOnly)R.resetNetworkAddressLoaded()') ||
      !ui.includes('Wi-Fi sleep saved.') ||
      !network.includes('\\"wifiSleep\\":%s') ||
      !network.includes('jsonHasOnlyUniqueFields(root, saveFields, 11)') ||
      !network.includes('jsonBoolean(root, "wifiSleep", command.wifiSleep)') ||
      !network.includes('command.wifiSleepSpecified = true') ||
      !network.includes('void ShotStopperNetwork::applyWifiPowerSave()') ||
      !network.includes('WIFI_PS_NONE') ||
      !network.includes('WIFI_PS_MIN_MODEM') ||
      /WiFi\.setSleep\(\s*WIFI_PS_MAX_MODEM\s*\)/.test(network) ||
      !network.includes('syncScaleLinkRf') ||
      !network.includes('syncScaleConnectingRf') ||
      !network.includes('syncScaleHuntRf') ||
      !firmware.includes('networkManager.syncScaleLinkRf') ||
      !firmware.includes('networkManager.syncScaleConnectingRf') ||
      !firmware.includes('networkManager.syncScaleHuntRf') ||
      !domainCore.includes('desiredWifiPowerSave') ||
      domainCore.includes('scaleConnectingOrUp') ||
      !domainCore.includes('staAssociated') ||
      !domainCore.includes('otaBusy')) {
    throw new Error(
        'Wi-Fi sleep must be wired in Admin UI, /network save, status, and power-save helper');
  }
  const applyStart = network.indexOf('void ShotStopperNetwork::applyWifiPowerSave()');
  const applyEnd = network.indexOf(
      'bool ShotStopperNetwork::beginStationConnect', applyStart);
  const applyBody = applyStart >= 0 && applyEnd > applyStart
      ? network.slice(applyStart, applyEnd)
      : '';
  if (!applyBody.includes('desiredWifiPowerSave') ||
      !applyBody.includes('ShotStopperOta::instance().busy()') ||
      !applyBody.includes('WIFI_PS_NONE') ||
      !applyBody.includes('WIFI_PS_MIN_MODEM') ||
      !applyBody.includes('WiFi.setSleep(desiredPs)') ||
      !applyBody.includes('esp_wifi_get_ps') ||
      !applyBody.includes('mode == WIFI_OFF') ||
      applyBody.includes('WIFI_PS_MAX_MODEM') ||
      applyBody.includes('scaleConnectingOrUp') ||
      /WiFi\.mode\(\s*WIFI_OFF\s*\)/.test(applyBody)) {
    throw new Error(
        'applyWifiPowerSave must set NONE/MIN_MODEM via setSleep+get_ps, skip if driver off, never WIFI_OFF/MAX_MODEM, and not follow scale link state');
  }
  const saveStart = network.indexOf('case WebCommandType::SAVE_NETWORK:');
  const saveEnd = network.indexOf('case WebCommandType::FORGET_NETWORK', saveStart);
  const saveBody = saveStart >= 0 && saveEnd > saveStart
      ? network.slice(saveStart, saveEnd)
      : '';
  const sleepOnlyAt = saveBody.indexOf('wifiSleepSpecified && sameCredentials');
  const copyLkgAt = saveBody.indexOf('copyActiveStaToLkg');
  const sleepOnly = sleepOnlyAt >= 0 && copyLkgAt > sleepOnlyAt
      ? saveBody.slice(sleepOnlyAt, copyLkgAt)
      : '';
  if (!saveBody.includes('applyWifiPsAfterPersist') ||
      saveBody.includes('wifiSleepSpecified && sameCredentials &&') ||
      sleepOnly.includes('restartPending_') ||
      !sleepOnly.includes('break;') ||
      !sleepOnly.includes('next.staWifiSleep != command.wifiSleep')) {
    throw new Error(
        'Identical STA credentials must persist sleep without restart, and no-op when sleep is unchanged');
  }
}
if (!firmwareCore.includes('command.commitConfirmed = true') ||
    !network.includes('finalizeSavedStaCredentials(next, command.commitConfirmed)')) {
  throw new Error(
      'USB SET_WIFI must commit STA credentials; Web UI / BLE Companion keep the confirm window');
}
{
  if (network.includes('ShotStopperNetwork::loginHandler') ||
      network.includes('ShotStopperNetwork::logoutHandler') ||
      network.includes('loginRateLimited') ||
      network.includes('recordFailedLoginAttempt')) {
    throw new Error('Login handlers and rate limits must be removed; WebUI claim owns the session');
  }
}
{
  const oldParseStart = bleLibrary.indexOf('parseAcaiaOldWeight');
  const oldParseEnd = bleLibrary.indexOf('parseAcaiaOldTimer', oldParseStart);
  const oldParse = oldParseStart >= 0 && oldParseEnd > oldParseStart
      ? bleLibrary.slice(oldParseStart, oldParseEnd)
      : '';
  if (!oldParse.includes('length == 14') ||
      !oldParse.includes('scaleValidAcaiaChecksum')) {
    throw new Error('OLD 14-byte Acaia frames must validate checksum');
  }
}
{
  const startCmd = firmware.slice(
      firmware.indexOf('void executeScaleStartCommand'),
      firmware.indexOf('void executeScaleStopCommand'));
  if (!startCmd.includes('tareStartTimer()') ||
      !startCmd.includes('resetTimer()') ||
      !startCmd.includes('if (!event.writeSucceeded)')) {
    throw new Error('Combined tare/start must fall back to reset/start/tare');
  }
}
if (!/<script\s+type="module"\s+src="\/app\.js\?v=/.test(shellHtml) &&
    !shellHtml.includes('src="/app.js?v=__FW_VERSION__"')) {
  throw new Error('Web UI must load same-origin /app.js with a firmware version query');
}
if (!/<link\s+rel="stylesheet"\s+href="\/app\.css\?v=/.test(shellHtml) &&
    !shellHtml.includes('href="/app.css?v=__FW_VERSION__"')) {
  throw new Error('Web UI must load same-origin /app.css with a firmware version query');
}
if (!shellHtml.includes('name="color-scheme" content="light dark"')) {
  throw new Error('Web UI must declare color-scheme so Safari form controls follow dark mode');
}
if (shellHtml.includes('class="themeSel"') ||
    css.includes('.themeSel{') ||
    css.includes('.themeOpt') ||
    !css.includes('html.theme-light{') ||
    !css.includes('html.theme-dark{') ||
    !appJsSource.includes("THEME_KEY='ssTh'") ||
    !appJsSource.includes("THEME_MODES=['auto','light','dark']") ||
    !appJsSource.includes("getElementById('uiTheme')") ||
    !appJsSource.includes('localStorage.getItem(THEME_KEY)') ||
    !appJsSource.includes("classList.toggle('theme-dark'") ||
    !appJsSource.includes("classList.toggle('theme-light'") ||
    !html.includes('id="frontendPanel"') ||
    !html.includes('<legend>Frontend</legend>') ||
    !html.includes('id="uiTheme"') ||
    !html.includes('<option value="auto" selected>Auto</option>') ||
    !html.includes('<option value="light">Light</option>') ||
    !html.includes('<option value="dark">Dark</option>') ||
    html.indexOf('id="dateTimePanel"') > html.indexOf('id="frontendPanel"') ||
    html.indexOf('id="frontendPanel"') > html.indexOf('id="devicePasswordPanel"')) {
  throw new Error('Theme must be an Admin Frontend dropdown (Auto/Light/Dark), not a Home selector');
}
if (/<link\s+[^>]*href=["']https?:\/\//i.test(shellHtml) ||
    /cdn\.|unpkg\.|jsdelivr\./i.test(shellHtml)) {
  throw new Error('Web UI must not depend on CDN or third-party assets');
}

const expected = new Map([
  ['GET /', 'rootHandler'],
  ['GET /diagnostic', 'rootHandler'],
  ['GET /log', 'rootHandler'],
  ['GET /stats', 'rootHandler'],
  ['GET /admin', 'rootHandler'],
  ['GET /settings', 'rootHandler'],
  ['GET /app.js', 'jsHandler'],
  ['GET /app.css', 'cssHandler'],
  ['GET /js/runtime.js', 'runtimeJsHandler'],
  ['GET /js/ota-image.js', 'otaImageJsHandler'],
  ['GET /js/secondary.js', 'secondaryJsHandler'],
  ['GET /partials/stats.html', 'partialStatsHandler'],
  ['GET /partials/diagnostic.html', 'partialDiagnosticHandler'],
  ['GET /partials/settings.html', 'partialSettingsHandler'],
  ['GET /partials/admin.html', 'partialAdminHandler'],
  ['GET /js/settings.js', 'viewSettingsHandler'],
  ['GET /favicon.ico', 'browserIconHandler'],
  ['GET /apple-touch-icon.png', 'browserIconHandler'],
  ['GET /apple-touch-icon-precomposed.png', 'browserIconHandler'],
  ['POST /api/v1/ui/claim', 'claimHandler'],
  ['GET /api/v1/status/home', 'ownedApiHandler'],
  ['GET /api/v1/status/settings', 'ownedApiHandler'],
  ['GET /api/v1/status/admin', 'ownedApiHandler'],
  ['GET /api/v1/status/diagnostic', 'ownedApiHandler'],
  ['GET /api/v1/debug/export', 'ownedApiHandler'],
  ['GET /api/v1/log', 'ownedApiHandler'],
  ['POST /api/v1/config', 'ownedApiHandler'],
  ['POST /api/v1/scale/preferred/clear', 'ownedApiHandler'],
  ['POST /api/v1/scale/preferred/select', 'ownedApiHandler'],
  ['POST /api/v1/presets', 'ownedApiHandler'],
  ['POST /api/v1/calibration/reset', 'ownedApiHandler'],
  ['POST /api/v1/calibration/reset-guard-samples', 'ownedApiHandler'],
  ['POST /api/v1/control/paddle', 'ownedApiHandler'],
  ['POST /api/v1/control/rinse', 'ownedApiHandler'],
  ['POST /api/v1/control/stop', 'ownedApiHandler'],
  ['POST /api/v1/control/state-override', 'ownedApiHandler'],
  ['POST /api/v1/control/restart', 'ownedApiHandler'],
  ['POST /api/v1/diagnostic/reset-history', 'ownedApiHandler'],
  ['POST /api/v1/factory-reset', 'ownedApiHandler'],
  ['GET /api/v1/shots', 'ownedApiHandler'],
  ['POST /api/v1/shots/clear', 'ownedApiHandler'],
  ['POST /api/v1/shots/delete', 'ownedApiHandler'],
  ['POST /api/v1/shots/rate', 'ownedApiHandler'],
  ['POST /api/v1/last-shot/clear', 'ownedApiHandler'],
  ['POST /api/v1/time/sync', 'ownedApiHandler'],
  ['POST /api/v1/webhooks', 'ownedApiHandler'],
  ['POST /api/v1/network', 'ownedApiHandler'],
  ['POST /api/v1/network/scan', 'ownedApiHandler'],
  ['GET /api/v1/network/scan', 'ownedApiHandler'],
  ['POST /api/v1/device/password', 'ownedApiHandler'],
  ['POST /api/v1/admin/unlock', 'ownedApiHandler'],
  ['POST /api/v1/admin/lock', 'ownedApiHandler'],
  ['POST /api/v1/diagnostic/profiler', 'ownedApiHandler'],
  // OTA authenticates with the device password instead of the exclusive
  // WebUI claim, so a command line client can update firmware without stealing
  // control from an open browser window.
  ['GET /api/v1/ota', 'otaStatusHandler'],
  ['POST /api/v1/ota', 'otaUploadHandler'],
  ['POST /api/v1/ota/flash', 'otaFlashHandler'],
  ['POST /api/v1/ota/abort', 'otaAbortHandler'],
]);

const maxSocketsMatch = network.match(/max_open_sockets\s*=\s*(\d+)/);
if (!maxSocketsMatch || Number(maxSocketsMatch[1]) !== 4) {
  throw new Error('HTTP server must reserve exactly 4 open sockets for the single-owner WebUI');
}
if (!network.includes('backlog_conn = 3')) {
  throw new Error('HTTP server backlog must be limited to 3 for the single-owner WebUI');
}
if (!network.includes('/api/v1/ui/claim') ||
    !network.includes('X-WebUI-Client') ||
    !network.includes('requireActiveWebUiClient') ||
    !network.includes('UI_CLAIM_REQUIRED') ||
    !network.includes('UI_TAKEN_OVER')) {
  throw new Error('WebUI APIs must enforce the exclusive client claim');
}

if (!webhookSource.includes('QueuedWebhook') ||
    !webhookSource.includes('queued.configGeneration != generation') ||
    !webhookSource.includes('++status_.staleConfigDropped')) {
  throw new Error('Queued webhooks must be discarded after their configuration changes');
}
if (!webhookSource.includes('xSemaphoreTake(lifecycleMutex_, 0)') ||
    !webhookSource.includes('WorkerState::STOPPING') ||
    !webhookSource.includes('releaseWorkerFromTask()')) {
  throw new Error('Webhook dispatch must remain non-blocking and release disabled worker resources');
}
const networkWebhookBegin = network.indexOf('webhooks_.begin(settings.webhook)');
const networkPassiveInit = [
  network.indexOf('xQueueCreate(WEB_COMMAND_QUEUE_LENGTH'),
  network.indexOf('xSemaphoreCreateMutex()'),
  network.indexOf('allocExternal(sizeof(NetworkWorkBuf))'),
];
if (networkWebhookBegin < 0 ||
    networkPassiveInit.some(index => index < 0 || index > networkWebhookBegin) ||
    !network.includes('bool ShotStopperNetwork::stop()') ||
    !network.includes('xSemaphoreTake(taskStopped_') ||
    !network.includes('if (!webhooks_.stop()) return false;') ||
    !webhookSource.includes('bool WebhookDispatcher::stop()') ||
    !webhookSource.includes('xSemaphoreTake(workerStopped_')) {
  throw new Error(
      'Network/Webhook initialization must acquire passive resources first and provide joined rollback');
}
if (!webhookSource.includes('allocExternal(queueStorageBytes)') ||
    !webhookSource.includes('allocExternal(kWebhookPayloadCapacity)') ||
    !webhookSource.includes('xQueueCreateStatic') ||
    !webhookSource.includes('tskIDLE_PRIORITY') ||
    !webhookSource.includes('dispatchAllowed()') ||
    !webhookSource.includes('config.event_handler') ||
    !webhookSource.includes('setControlCritical') ||
    !webhookSource.includes('setScaleConnecting') ||
    !webhookSource.includes('bool haveQueued = false') ||
    !webhookSource.includes('if (haveQueued && dispatchAllowed())') ||
    !webhookSource.includes('esp_http_client_close(event->client)') ||
    !webhookSource.includes('esp_http_client_cancel_request(client)') ||
    !webhookSource.includes('cancelActive_') ||
    !webhookSource.includes('ensureHttpClient(live.url)') ||
    !webhookSource.includes('sampleHeapCaps()') ||
    !webhookSource.includes('webhookClientMustRecreate') ||
    !webhookSource.includes('++status_.clientReuses') ||
    !webhookSource.includes('++status_.transportResets') ||
    !webhookSource.includes('void WebhookDispatcher::serviceAbort()') ||
    !network.includes('webhooks_.serviceAbort()')) {
  throw new Error(
      'Webhook queue/payload must live in PSRAM; delivery must recheck its gate and actively cancel HTTP outside control/BLE');
}
if (!webhookHeader.includes('UniqueResource<esp_http_client_handle_t') ||
    webhookHeader.includes('void *httpClient_') ||
    webhookSource.includes('else requestWorkerStop();') ||
    !webhookSource.includes('++status_.workerStarts') ||
    !webhookSource.includes('++status_.heapSamples')) {
  throw new Error(
      'Webhook worker/client ownership must remain stable and publish per-operation heap evidence');
}
if (!webhookSource.includes('\\"sentAtUptimeMs\\"') ||
    !firmwareCore.includes('webhookUnixSecAt(uint32_t occurredAtMs)') ||
    !firmwareCore.includes('baseWebhookEvent(WebhookEventType::END, snapshot.cycleId,\n                                        snapshot.endedAtMs)')) {
  throw new Error('Webhook payloads must preserve occurrence time separately from delivery time');
}
if (!firmwareCore.includes('if (brewEndIsAbandonedStart(reason) || endingRinseCycle(reason)) {\n    webhookBrewStartPending = false;') ||
    !firmwareCore.includes('!endingRinseCycle(reason) && !brewEndIsAbandonedStart(reason)')) {
  throw new Error('Abandoned starts and rinses must not emit misleading brew-state webhooks');
}
if (!ui.includes('function claimWebUiOwnership()') ||
    !ui.includes('function deactivateWebUi()') ||
    !ui.includes('function showInactiveOverlay()') ||
    !ui.includes('function hideInactiveOverlay()') ||
    !ui.includes('id="webUiReload"') ||
    !ui.includes('>Reload<') ||
    !ui.includes('X-WebUI-Client')) {
  throw new Error('Inactive WebUI windows must become passive and offer Reload');
}
if (!ui.includes('const WEB_UI_INACTIVITY_MS=15*60*1000') ||
    !ui.includes('function resetWebUiInactivity()') ||
    !ui.includes('function webUiPollingActive()') ||
    !ui.includes('function noteWebUiInteraction(event)') ||
    !(ui.includes("document.addEventListener('pointerdown',noteWebUiInteraction,true)") ||
      ui.includes("document.addEventListener('pointerdown', R.noteWebUiInteraction, true)") ||
      ui.includes("document.addEventListener('pointerdown',R.noteWebUiInteraction,true)")) ||
    !(ui.includes("document.addEventListener('keydown',noteWebUiInteraction,true)") ||
      ui.includes("document.addEventListener('keydown', R.noteWebUiInteraction, true)") ||
      ui.includes("document.addEventListener('keydown',R.noteWebUiInteraction,true)")) ||
    ui.includes("addEventListener('scroll',noteWebUiInteraction") ||
    ui.includes("addEventListener('scroll', R.noteWebUiInteraction")) {
  throw new Error('WebUI inactivity must expire after 15 minutes of direct control interaction, never scrolling');
}
if (!ui.includes('Press Reload to enable this window again.') ||
    !ui.includes('id="webUiInactive"') ||
    !ui.includes('id="inactiveHint"') ||
    !ui.includes('id="inactiveError"') ||
    !css.includes('.inactiveOverlay') ||
    !css.includes('.inactiveOverlay.isVisible') ||
    !css.includes('opacity .5s') ||
    !network.includes('This WebUI window is inactive. Reactivate to continue.') ||
    ui.includes('Another WebUI window controls this device.') ||
    network.includes('Another WebUI window has taken control.') ||
    ui.includes('function ownershipBanner(') ||
    ui.includes('webUiOwnership') ||
    ui.includes('btnTakeControl') ||
    ui.includes('Reactivate')) {
  throw new Error('WebUI inactive notice must be a full-screen Reload overlay');
}
if (!ui.includes("w.id='reconnectWait'") ||
    !ui.includes("w.className='reconnectRing'") ||
    !ui.includes('id="reconnectSeconds"') ||
    !ui.includes('function beginNetworkReconnectWait()') ||
    !ui.includes('function endNetworkReconnectWait()') ||
    !ui.includes('function pollNetworkReconnect()') ||
    !ui.includes('NETWORK_RECONNECT_WAIT_MS=180000') ||
    !ui.includes('setTimeout(pollNetworkReconnect,2e3)') ||
    !ui.includes('setInterval(updateReconnectCountdown,1e3)') ||
    !ui.includes('rec?4e3:8e3') ||
    !ui.includes("value.action==='save'") ||
    !ui.includes('beginNetworkReconnectWait()') ||
    !ui.includes('claimWebUiOwnership()') ||
    !ui.includes('Waiting for the controller on this address.') ||
    !ui.includes('the previous network should return shortly.') ||
    !css.includes('.inactiveOverlay.isReconnectWait') ||
    !css.includes('.reconnectRing') ||
    !css.includes('conic-gradient') ||
    ui.includes('function heartbeat(') ||
    ui.includes('/api/v1/heartbeat')) {
  throw new Error(
      'Wi-Fi save must show a 180s reconnect overlay that polls ui/claim even after 0s, without a heartbeat endpoint');
}
if (!ui.includes('clearTimeout(webUiInactivityTimer)') ||
    !ui.includes('clearTimeout(scanTimer)') ||
    !ui.includes('stopViewPolls()') ||
    !ui.includes('if(!webUiPollingActive())throw new Error')) {
  throw new Error('WebUI inactivity must cancel poll timers and block further API calls');
}
if (!ui.includes('setMutable(!!s.configMutable||!!s.webUiOverrideActive)') ||
    !ui.includes('webUiOverrideActive') ||
    !ui.includes('webUiOverrideRemainingMs') ||
    !ui.includes("uiOverridePanel") ||
    !ui.includes("uiOverrideButton") ||
    !ui.includes('UI Override') ||
    !ui.includes("closest('#adminLockPanel,#diagnosticLockPanel,#uiOverridePanel,#bleCompanionPanel')") ||
    !ui.includes('function ensureUiOverridePanel(') ||
    !ui.includes('/api/v1/ui/unlock') ||
    !ui.includes('UNSAFE_WEBUI_OVERRIDE') ||
    !network.includes('/api/v1/ui/unlock') ||
    !network.includes('UNSAFE_WEBUI_OVERRIDE') ||
    !network.includes('WEB_UI_OVERRIDE_MS') ||
    !network.includes('webUiOverrideUntilMs_') ||
    !network.includes('webUiOverrideRemainingMs') ||
    !network.includes('requireAdminUnlock(request)') ||
    network.includes('clearWebUiOverrideIfSafe')) {
  throw new Error('Web UI must honor configMutable/webUiOverrideActive with a timed admin-gated UI Override');
}
if (ui.includes('configLockBanner') || css.includes('configLockBanner') ||
    ui.includes('ensureConfigLockBanner') || ui.includes('renderConfigLockBanner') ||
    ui.includes('Controls locked:') || ui.includes('Unsafe WebUI override active')) {
  throw new Error('Web UI must not render the configuration-lock warning banner');
}
if (network.includes('return "safety_recovery"') ||
    network.includes('return "safety_lockout"') ||
    ui.includes("safety_recovery:'safety recovery'") ||
    ui.includes("safety_lockout:'safety lockout'")) {
  throw new Error('Safety recovery must not lock the WebUI');
}
if (!ui.includes("s.safety.recoveryRequired||s.safety.state==='LOCKOUT'") ||
    !ui.includes('admin&&remoteReady&&relayStartReady&&canControl') ||
    !ui.includes("live?'Stop shot':'Start shot'") ||
    !ui.includes("dataset.mode==='stop'") ||
    !ui.includes("/api/v1/control/paddle") ||
    !ui.includes("/api/v1/control/stop") ||
    !ui.includes('function updateHomeAdminActions(') ||
    !ui.includes("id=\"actionsPanel\" class=\"hidden\"") ||
    !ui.includes('shot.disabled=!admin||(!live&&!(remoteReady&&relayStartReady&&canControl))')) {
  throw new Error('Circuit actions must stay behind admin unlock and preserve Stop only while unlocked');
}
if (!ui.includes('id="forcePulseButton"') ||
    !ui.includes('class="btnGlyph btnWarn momentaryOnly"') ||
    !ui.includes('Force switch press') ||
    !ui.includes("R.command('/api/v1/control/force-pulse')") ||
    !runtimeJs.includes("'control/force-pulse':['Switch pulse sent.','send switch pulse']") ||
    !runtimeJs.includes("force.disabled=!(admin&&remoteReady&&relayStartReady&&webUiOwner)") ||
    !css.includes('.presetActions>.btnGlyph,#actionsPanel #forcePulseButton{flex-direction:column;') ||
    !network.includes('"/api/v1/control/force-pulse"') ||
    !network.includes('ShotStopperNetwork::forcePulseHandler') ||
    !network.includes('WebCommandType::FORCE_SWITCH_PULSE') ||
    !network.includes('requireAdminUnlock(request)') ||
    !domain.includes('FORCE_SWITCH_PULSE') ||
    !firmware.includes('machineRequestForcedPulse()') ||
    !firmwareCore.includes('machineRequestWebStop()')) {
  throw new Error('Momentary Web controls must expose an admin-gated forced pulse and a dedicated Web STOP path');
}
if (!network.includes('/api/v1/status/home') ||
    !network.includes('/api/v1/status/settings') ||
    !network.includes('/api/v1/status/admin') ||
    !network.includes('/api/v1/status/diagnostic')) {
  throw new Error('Status API must expose per-page /api/v1/status/{home|settings|admin|diagnostic}');
}
if (!network.includes('statusResponseMux_') ||
    !network.includes('STATUS_BUSY')) {
  throw new Error(
      'Status handler must serialize the shared status buffers with a mutex');
}
if (!network.includes('"Connection"') || !network.includes('"close"')) {
  throw new Error(
      'API JSON responses must send Connection: close to avoid keep-alive socket pinning');
}
if (!network.includes('recv_wait_timeout = 5') ||
    !network.includes('send_wait_timeout = 5')) {
  throw new Error(
      'HTTP recv/send wait timeouts must stay short so LRU can free stalled sockets');
}
const maxReqHdrMatch = network.match(/max_req_hdr_len\s*=\s*(\d+)/);
if (!maxReqHdrMatch || Number(maxReqHdrMatch[1]) < 2048) {
  throw new Error(
      'HTTP server must allow at least 2048 request header bytes (Safari UA/Cookie 431)');
}
const maxRespHeadersMatch = network.match(/max_resp_headers\s*=\s*(\d+)/);
if (!maxRespHeadersMatch || Number(maxRespHeadersMatch[1]) < 12) {
  throw new Error('HTTP server must allow at least 12 response headers for gzip and ETag');
}
if (!ui.includes('function withPollGate(') ||
    !ui.includes('function withCommandGate(') ||
    !ui.includes('function armStatusTimer(') ||
    !ui.includes('function statusPollDue(') ||
    !ui.includes('commandBusy') ||
    !ui.includes('statusLiveShot') ||
    !ui.includes('visibilitychange') ||
    !ui.includes('await refreshStatus()') ||
    !ui.includes('noteReachFail(') ||
    !ui.includes('function startView(') ||
    !ui.includes('function stopViewPolls(') ||
    !ui.includes('function renderRoute(') ||
    !ui.includes('armStatusTimer()') ||
    !ui.includes('AbortController') ||
    !ui.includes('Device timeout') ||
    !ui.includes("throw new Error('Invalid response')") ||
    !ui.includes("throw new Error('Invalid status')") ||
    !ui.includes('function statusUrl(') ||
    !ui.includes('function ensureSettingsHydrated(') ||
    !ui.includes('function homeConfigPatch(') ||
    !ui.includes('function withBaseRev(') ||
    !ui.includes('function isConfigStale(') ||
    !ui.includes('formRev') ||
    !ui.includes('formRev===c.revision') ||
    !ui.includes('baseRevision') ||
    !ui.includes('command(path,value={},soft,okMsg,failMsg)') ||
    !ui.includes('/api/v1/status/') ||
    !ui.includes('function statusPageOk(') ||
