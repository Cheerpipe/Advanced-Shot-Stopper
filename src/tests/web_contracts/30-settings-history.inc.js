{
  const labels = ['Automatic tare outside a brew', 'Automatic tare at shot start',
    'Late-cup retare during a shot'];
  if (labels.some(label => !html.includes(label)) ||
      !html.includes('id="autoTareOutsideBrew" type="checkbox" checked') ||
      !network.includes('autoTareOutsideBrew must be a boolean.') ||
      !firmwareCore.includes('candidate.autoTareOutsideBrew = command.config.autoTareOutsideBrew;')) {
    throw new Error('Idle tare must have an independent default-ON machine setting');
  }
  const payloadLine = js.split('\n').find(line => line.startsWith('function machinePayload(){'));
  if (!payloadLine) throw new Error('Missing machine payload function');
  const makePayload = new Function('$', 'number', 'sToMs', 'extRate',
    'HOME_GUARD_SWITCHES', 'homeSwitchPending', 'syncSettingsFromHomeSwitches',
    payloadLine + ';return machinePayload();');
  for (const idleOn of [false, true]) {
    const payload = makePayload(id => ({checked: id === 'autoTareOutsideBrew' && idleOn,
      value: 'off'}), () => 1, () => 1000, value => value, [], {}, () => {});
    if (payload.autoTareOutsideBrew !== idleOn || payload.autoTare !== false) {
      throw new Error('Idle tare payload must be independent of shot-start tare');
    }
  }
  if (!js.includes("['autoTare','autoTareOutsideBrew','brewByWeight'")) {
    throw new Error('Settings hydration must restore the saved idle tare value');
  }
}

if (/R\.(homeFlushConfig|homeFlushPreset|configLoaded|formRev|brewDirty)\s*=/.test(js) ||
    !js.includes('function persistHomeBrewByWeight(') ||
    !js.includes('function invalidateSettingsHydration(') ||
    !js.includes('function clearBrewDirty(')) {
  throw new Error(
      'View modules must call runtime helpers instead of assigning read-only ESM namespace exports');
}
if (!ui.includes('<legend>Brew</legend>') ||
    !ui.includes('<legend>Machine and scale</legend>') ||
    !ui.includes('<legend>Wi-Fi</legend>') ||
    !ui.includes('<legend>Device password</legend>') ||
    !ui.includes('<legend>Frontend</legend>') ||
    !ui.includes('id="presetCards"') ||
    !ui.includes('id="presetNewBtn"') ||
    !ui.includes('id="presetDupBtn"') ||
    ui.includes('id="presetLoadBtn"') ||
    ui.includes('id="presetSaveBtn"') ||
    !html.includes('id="saveBrewPresetButton" class="btnGlyph mutable btnInvert"') ||
    !html.includes('id="saveConfigButton" class="btnGlyph mutable btnInvert"') ||
    html.includes('id="exportShotsButton" class="btnGlyph btnInvert"') ||
    !css.includes('font-variant-emoji:text') ||
    !html.includes('<span class="g">×</span>') ||
    !html.includes('<span class="g">✓</span>') ||
    ui.includes('💾') || ui.includes('⚡') ||
    !ui.includes('Save brew settings') ||
    !ui.includes('id="activeBrewProfileHint"') ||
    !ui.includes('Current profile: ') ||
    !ui.includes('function updateActiveBrewProfileHint(') ||
    html.indexOf('id="activeBrewProfileHint"') <
        html.indexOf('id="saveBrewPresetButton"') ||
    html.indexOf('id="saveBrewPresetButton"') <
        html.indexOf('id="resetGuardSamplesButton"') ||
    html.indexOf('id="saveBrewPresetButton"') >
        html.indexOf('<legend>Machine and scale</legend>') ||
    html.indexOf('id="saveConfigButton"') <
        html.indexOf('<summary>Alerts</summary>') ||
    html.indexOf('id="saveConfigButton"') >
        html.indexOf('<legend>Wi-Fi</legend>') ||
    !ui.includes('id="presetResetBtn"') ||
    !ui.includes('id="presetDeleteBtn"') ||
    !ui.includes('id="presetRenameDialog"') ||
    !ui.includes('id="homePresetCards"') ||
    !html.includes('id="homePresetLabel"') ||
    !html.includes('>Presets</p>') ||
    html.indexOf('id="homePresetLabel"') > html.indexOf('id="homePresetCards"') ||
    html.indexOf('id="homePresetBlock"') > html.indexOf('id="homePresetLabel"') ||
    !ui.includes('id="homeBrewByWeight"') ||
    !ui.includes('id="homeNoScaleBbwEnabled"') ||
    !ui.includes('id="quickSettingsPanel"') ||
    !html.includes('class="homeSwitchGrid"') ||
    !html.includes('id="homeBbwSub"') ||
    !html.includes('id="homeNoScaleSub"') ||
    !html.includes('id="homeFastSub"') ||
    !html.includes('id="homeTouchSub"') ||
    !ui.includes('function formatAccidentalTouch(') ||
    !html.includes('id="homeSlowSub"') ||
    !html.includes('id="homeAtmSub"') ||
    !html.includes('id="homeCupSub"') ||
    html.includes('id="homeAlertsSub"') ||
    !html.includes('>Cup protection<span') ||
    html.includes('>Alerts<span') ||
    !html.includes('class="swS"') ||
    html.indexOf('class="homeSwitchGrid"') > html.indexOf('id="homeBrewByWeight"') ||
    html.indexOf('id="homeBrewByWeight"') > html.indexOf('id="homeNoScaleBbwMode"') ||
    html.indexOf('id="homeNoScaleBbwMode"') >
        html.indexOf('id="homeAutoToManualGuardEnabled"') ||
    html.indexOf('id="homeAutoToManualGuardEnabled"') >
        html.indexOf('id="homeSlowExtractionGuardEnabled"') ||
    html.indexOf('id="homeSlowExtractionGuardEnabled"') >
        html.indexOf('id="homeFastExtractionGuardEnabled"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') > html.indexOf('id="homePresetBlock"') ||
    !html.includes('class="homeGuardGrid"') ||
    html.indexOf('class="homeGuardGrid"') > html.indexOf('id="homeNoScaleBbwMode"') ||
    !ui.includes('id="homeNoScaleBbwMode"') ||
    !ui.includes('id="homeFastExtractionGuardEnabled"') ||
    !ui.includes('id="homeSlowExtractionGuardEnabled"') ||
    !ui.includes('id="homeAutoToManualGuardEnabled"') ||
    html.indexOf('id="quickSettingsPanel"') > html.indexOf('id="shotPanel"') ||
    html.indexOf('id="homeBrewByWeight"') > html.indexOf('id="homeNoScaleBbwMode"') ||
    html.indexOf('id="homeNoScaleBbwMode"') >
        html.indexOf('id="homeAutoToManualGuardEnabled"') ||
    html.indexOf('id="homeAutoToManualGuardEnabled"') >
        html.indexOf('id="homeSlowExtractionGuardEnabled"') ||
    html.indexOf('id="homeSlowExtractionGuardEnabled"') >
        html.indexOf('id="homeFastExtractionGuardEnabled"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') >
        html.indexOf('id="homePresetBlock"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') >
        html.indexOf('id="homeAvoidAccidentalTouchEnabled"') ||
    html.indexOf('id="homeAvoidAccidentalTouchEnabled"') >
        html.indexOf('id="homeCupProtectionEnabled"') ||
    html.indexOf('id="homeCupProtectionEnabled"') >
        html.indexOf('id="homePresetBlock"') ||
    html.indexOf('id="homeFastExtractionGuardEnabled"') > html.indexOf('id="shotPanel"') ||
    !html.includes('>No-scale BBW<span') ||
    !html.includes('>Fast extraction guard<span') ||
    !html.includes('>Avoid accidental touch<span') ||
    !html.includes('>Slow extraction guard<span') ||
    !html.includes('>A→M time guard<span') ||
    html.indexOf('<summary>No-scale BBW</summary>') < 0 ||
    html.indexOf('<summary>Fast extraction guard</summary>') < 0 ||
    html.indexOf('<summary>Cup protection</summary>') < 0 ||
    html.indexOf('<summary>Brew by Weight</summary>') >
        html.indexOf('<summary>Cup protection</summary>') ||
    html.indexOf('<summary>Cup protection</summary>') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    !ui.includes('id="cupProtectionEnabled"') ||
    html.indexOf('id="cupProtectionEnabled"') >
        html.indexOf('id="stopIfCupRemoved"') ||
    !ui.includes('id="stopIfCupRemoved"') ||
    !ui.includes('id="requireCupToStart"') ||
    html.indexOf('id="requireCupToStart"') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    ui.includes('id="cupPresentWeightG"') ||
    html.includes('id="cupPresentWeightG"') ||
    html.includes('id="requireCupToStart" type="checkbox" checked') ||
    !ui.includes('place the cup after connect so it can be detected.') ||
    !ui.includes('id="homeCupProtectionEnabled"') ||
    html.indexOf('<summary>Slow extraction guard</summary>') < 0 ||
    html.indexOf('<summary>A→M time guard</summary>') < 0 ||
    html.includes('<summary>Avoid accidental touch</summary>') ||
    html.indexOf('<summary>Brew by Weight</summary>') >
        html.indexOf('id="avoidAccidentalTouchEnabled"') ||
    html.indexOf('id="avoidAccidentalTouchEnabled"') >
        html.indexOf('<summary>Cup protection</summary>') ||
    html.indexOf('<summary>Fast extraction guard</summary>') >
        html.indexOf('<summary>Slow extraction guard</summary>') ||
    html.indexOf('<summary>Slow extraction guard</summary>') >
        html.indexOf('<summary>A→M time guard</summary>') ||
    !ui.includes('function updateHomeGuardSwitchesLock(') ||
    !ui.includes('function persistHomeGuard(') ||
    !ui.includes('function flushHomeGuards(') ||
    !ui.includes('function scheduleHomeGuardFlush(') ||
    !ui.includes('function withCommandGate(') ||
    !ui.includes('function beginHomeSwitchPending(') ||
    !ui.includes('function applyPolledHomeSwitch(') ||
    !ui.includes('function applyHomeSwitchesFromConfig(') ||
    !ui.includes('Date.now()+5e3') ||
    !ui.includes('pollAt<p.until') ||
    !ui.includes("classList.toggle('switchPending',!!on)") ||
    !ui.includes("persistHomeGuard('homeFastExtractionGuardEnabled'") ||
    !ui.includes("persistHomeGuard('homeAvoidAccidentalTouchEnabled'") ||
    !ui.includes("persistHomeGuard('homeSlowExtractionGuardEnabled'") ||
    !ui.includes("persistHomeGuard('homeAutoToManualGuardEnabled'") ||
    !ui.includes("persistHomeGuard('homeCupProtectionEnabled'") ||
    !ui.includes('onchange=R.persistHomeNoScaleBbw') ||
    !ui.includes('function persistHomeNoScaleBbw(') ||
    !ui.includes("p.noScaleBbwMode=$('homeNoScaleBbwEnabled').checked?nsm:'off'") ||
    ui.includes("persistHomeGuard('homeSoundAlertsEnabled'") ||
    !ui.includes("'fastExtractionGuardEnabled',1)") ||
    !ui.includes("'avoidAccidentalTouchEnabled',1)") ||
    !ui.includes("'slowExtractionGuardEnabled',1)") ||
    !ui.includes("'autoToManualGuardEnabled',1)") ||
    !ui.includes("'cupProtectionEnabled',1)") ||
    !ui.includes('el.disabled=!controlsMutable||u||off') ||
    ui.includes('el.disabled=!controlsMutable||off||pend') ||
    !ui.includes('homeFlushBusy') ||
    !ui.includes('scheduleHomeGuardFlush()') ||
    !ui.includes("classList.toggle('fieldOff',off)") ||
    !ui.includes('bbw.disabled=!controlsMutable||x') ||
    !ui.includes('function homeSwitchUnset(') ||
    !ui.includes("classList.add('swR')") ||
    !ui.includes("typeof c[k]==='boolean'") ||
    html.includes('id="homeBrewByWeight" type="checkbox" role="switch" aria-label="Brew by weight" checked') ||
    html.includes('id="homeNoScaleBbwMode" type="checkbox"') ||
    !html.includes('id="homeNoScaleBbwEnabled" type="checkbox" role="switch" aria-label="No-scale BBW"') ||
    html.includes('id="homeSoundAlertsEnabled"') ||
    html.includes('id="homeCupProtectionEnabled" type="checkbox" role="switch" aria-label="Cup protection" checked') ||
    !css.includes('.switchRow.switchPending') ||
    !css.includes('.switch{position:relative;display:inline-block;width:3rem;height:var(--tap)') ||
    !css.includes('.slider{position:absolute;inset:.5rem 0;') ||
    !css.includes('.switch input:checked+.slider{background:var(--pri);border-color:var(--pri)}') ||
    !css.includes('.switch input:checked+.slider:before{transform:translateX(1.25rem)}') ||
    !css.includes('.homeSwitchGrid') ||
    !css.includes('justify-content:space-between') ||
    !css.includes('.homeSwitchGrid{') ||
    !css.includes('border-bottom:1px solid') ||
    !css.includes('.switchState{display:none') ||
    !css.includes('.homeSwitchGrid .swS') ||
    !css.includes('.homeGuardGrid{') ||
    !css.includes('grid-template-columns:repeat(2,minmax(0,1fr))') ||
    !css.includes('#brewModeRow .swL{font-size:1.125rem;font-weight:700;line-height:1.2;color:var(--fg);letter-spacing:0}') ||
    !css.includes('.ruleChartHead strong,.ruleChartMode{display:none}') ||
    !css.includes('.switchRow.switchPending .slider,.switchRow.switchPending input:checked+.slider{background:var(--wn);border-color:var(--wn)}') ||
    !ui.includes('function persistHomeBrewByWeight(') ||
    !ui.includes("onchange=R.persistHomeBrewByWeight") ||
    !ui.includes('beginHomeSwitchPending(h,on)') ||
    !ui.includes('id="clearLastShotButton"') ||
    html.indexOf('id="shotPanel"') > html.indexOf('id="clearLastShotButton"') ||
    (html.includes('id="clearLastShotButton"') &&
     html.slice(html.indexOf('id="clearLastShotButton"'),
                html.indexOf('</button>', html.indexOf('id="clearLastShotButton"')) + 9)
         .includes('<span class="t">Clear</span>')) ||
    !css.includes('#shotPanel{position:relative}') ||
    css.includes('#shotPanel{position:relative;padding-right:3.4rem') ||
    !css.includes('#shotTable .btnGlyph,#shotPanel .btnGlyph{border:0;border-radius:2rem;min-height:var(--tap);min-width:var(--tap);padding:0;flex:0 0 auto;background:none;box-shadow:none;filter:none') ||
    html.includes('id="lastCycle"') ||
    !ui.includes('function renderShotPanel(') ||
    !ui.includes('function renderShotSpark(') ||
    !network.includes('\\"lastShot\\"') ||
    !network.includes('\\"shotCurve\\"') ||
    !network.includes('formatShotCurveJsonBody') ||
    !network.includes('lastShotClearHandler') ||
    !network.includes('LAST_SHOT_CLEAR_NOT_CONFIRMED') ||
    !firmware.includes('persistLastShotFromEndedCycle') ||
    !firmware.includes('endedCycleDurationMs') ||
    firmware.includes('elapsedMs(relayBeforeOpen.closedAtMs)') ||
    !firmware.includes('clearLastShot') ||
    !firmware.includes('clearLastShotSnapshot') ||
    !firmware.includes('serviceShotStorePersistence') ||
    !firmware.includes('durableFlashWriteAllowed(') ||
    !domainCore.includes('durableFlashWriteAllowed') ||
    !firmwareCore.includes('shotLogPersistFailLatched') ||
    !firmwareCore.includes('shotStorePersistRetryAtMs') ||
    !firmwareCore.includes('SHOT_STORE_PERSIST_RETRY_MS') ||
    !firmware.includes('noteScaleHistory(seenMac, seenName, false)') ||
    !firmwareCore.includes('constexpr uint32_t kTryLockMs = 0') ||
    wallClock.includes('monotonicMs >= anchorMonotonicMs_') ||
    !wallClock.includes('monotonicElapsedMs(monotonicMs, anchorMonotonicMs_)') ||
    !network.includes('StaJoinHints ShotStopperNetwork::staJoinHints()') ||
    !shotLogIo.includes('copyToFlashIoScratch(&store_') ||
    !shotCurveIo.includes('copyToFlashIoScratch(&store_') ||
    !lastShotIo.includes('copyToFlashIoScratch(&blob_') ||
    !jsonArena.includes('static const bool initialized') ||
    !jsonArena.includes('JSON_DOCUMENT_MAX_BYTES') ||
    !jsonArena.includes('JSON_DOCUMENT_MAX_DEPTH') ||
    !network.includes('workBuf_->~NetworkWorkBuf()') ||
    !firmware.includes('resetAllDurableStoresForNetwork') ||
    !firmware.includes(
        'return resetAllDurableStoresForNetwork(persistedSettings)') ||
    !network.includes('historyMutationAllowed') ||
    !network.includes('controlAllowsHistoryMutation') ||
    (network.match(/historyMutationAllowed/g) || []).length < 5 ||
    ui.includes('id="view-presets"') ||
    ui.includes('data-route="/presets"') ||
    ui.includes('id="presetsPageCards"') ||
    ui.includes('id="homePresetChips"') ||
    !ui.includes("action:'new'") ||
    !ui.includes("action:'duplicate'") ||
    !ui.includes("action:'rename'") ||
    !ui.includes("action:'restore_factory_values'") ||
    !ui.includes('function startRenamePreset(') ||
    !ui.includes('function updatePresetActionButtons(') ||
    !ui.includes("function presetSummary(p){return 'Target '+(p.goalWeightG||'?')+' g'}") ||
    !ui.includes("badge.textContent=p.isFactory?'factory':'custom'") ||
    !ui.includes('Discard them and switch presets') ||
    !ui.includes('saveBrewPreset') ||
    !ui.includes('/api/v1/presets') ||
    ui.includes('id="presetNameInput"') ||
    !network.includes('/api/v1/presets') ||
    !network.includes('presetsHandler') ||
    !network.includes('restore_factory_values') ||
    !network.includes('\\"presets\\"') ||
    network.includes('"/presets"') ||
    !css.includes('.presetCard') ||
    !css.includes('#homePresetCards') ||
    !css.includes('.btnGlyph') ||
    !css.includes('.btnGlyph .g') ||
    !css.includes('.btnGlyph .t')) {
  throw new Error('Brew presets CRUD UI must block factory delete, support reset, click-to-load, and unsaved switch confirm');
}
{
  const diagHtml = html.slice(html.indexOf('id="view-diagnostic"'),
                              html.indexOf('</section>', html.indexOf('id="view-diagnostic"')) + 10);
  const adminHtml = html.slice(html.indexOf('id="view-admin"'),
                               html.indexOf('id="firmwareFooter"'));
  const statusHtml = html.slice(html.indexOf('id="statusPanel"'),
                                html.indexOf('id="actionsPanel"'));
  if (!ui.includes('id="hCpu5s"') ||
      !ui.includes('id="hCpu1m"') ||
      !ui.includes('id="hCpu5m"') ||
      !ui.includes('id="hCpuMhz"') ||
      !ui.includes('id="hWifiState"') ||
      !ui.includes('id="hWifiPs"') ||
      !ui.includes('id="hWifiCoex"') ||
      !ui.includes('id="hSsid"') ||
      !ui.includes('id="hWifiChannel"') ||
      !ui.includes('id="hWifiIp"') ||
      !ui.includes('id="hWifiSignal"') ||
      !ui.includes('id="hWifiRssi"') ||
      !ui.includes('id="hApState"') ||
      !ui.includes('id="hApSsid"') ||
      !ui.includes('id="hApIp"') ||
      !ui.includes('id="hApClients"') ||
      !ui.includes('id="hUptime"') ||
      !ui.includes('id="hFirmware"') ||
      !ui.includes('id="hBoot"') ||
      !ui.includes('id="hResetReason"') ||
      !ui.includes('id="hTemp"') ||
      !ui.includes('id="hTPeak"') ||
      !ui.includes('id="hRamT"') ||
      !ui.includes('id="hRamU"') ||
      !ui.includes('id="hRamF"') ||
      !ui.includes('id="hTaskState"') ||
      !ui.includes('id="hTaskElapsed"') ||
      !ui.includes('id="taskProfilerStartButton"') ||
      !ui.includes('id="taskProfilerStopButton"') ||
      !ui.includes('id="taskTable"') ||
      !ui.includes('id="taskTableBody"') ||
      !ui.includes('id="taskTableHint"') ||
      !ui.includes('function applyTaskProfiler(') ||
      !ui.includes('/api/v1/diagnostic/profiler') ||
      !ui.includes('100% = 1 core busy (sum can exceed 100)') ||
      !ui.includes('id="hHeapMin"') ||
      !ui.includes('id="hHeapLargest"') ||
      !ui.includes('id="hPsramT"') ||
      !ui.includes('id="hPsramF"') ||
      !ui.includes('id="hPsramL"') ||
      !ui.includes('id="hLoopGap"') ||
      !ui.includes('id="hLoopMax"') ||
      !ui.includes('id="lastCommandState"') ||
      !ui.includes('function updH(') ||
      !ui.includes('updH(s.health,s.safety)') ||
      !ui.includes('function applyDiagnosticStatus(') ||
      !ui.includes('loopIntervalGapMs') ||
      !ui.includes("s.health.loopIntervalGapMs+' ms'") ||
      !ui.includes("s.health.loopMaxGapMs+' ms'") ||
      !ui.includes('h.uptimeMs') ||
      !ui.includes('h.minimumFreeHeapBytes') ||
      !ui.includes('h.largestFreeHeapBlockBytes') ||
      !ui.includes('h.psramSizeBytes') ||
      !ui.includes('h.psramFreeBytes') ||
      !ui.includes('h.psramLargestFreeBlockBytes') ||
      !ui.includes("toFixed(1)+' KB'") ||
      !ui.includes('resetReasonCode') ||
      !ui.includes("RR[s.resetReasonCode]") ||
      !network.includes('\\"hwmon\\"') ||
      !network.includes('cpuLoad5s') ||
      !network.includes('cpuLoad1m') ||
      !network.includes('cpuLoad5m') ||
      !network.includes('cpu0Busy') ||
      !network.includes('cpu1Busy') ||
      !network.includes('cpuLoadValid') ||
      !network.includes('cpuMhz') ||
      !ui.includes('cpuLoad5s') ||
      !ui.includes('cpuLoad1m') ||
      !ui.includes('cpuLoad5m') ||
      !ui.includes('cpuLoadValid') ||
      !ui.includes('cpuMhz') ||
      !ui.includes("w.cpuMhz+' MHz'") ||
      !ui.includes('w.cpu0Busy') ||
      !ui.includes('w.cpu1Busy') ||
      !ui.includes("t+' ('+a.toFixed(2)+' + '+b.toFixed(2)+')'") ||
      !ui.includes("split(w.cpuLoad5s,w.cpu0Busy,w.cpu1Busy)") ||
      !ui.includes('0–2 (cpu0 + cpu1)') ||
      !network.includes('tempPeakC') ||
      !network.includes('ramTotalBytes') ||
      !network.includes('\\"uptimeMs\\"') ||
      !network.includes('\\"minimumFreeHeapBytes\\"') ||
      !network.includes('\\"largestFreeHeapBlockBytes\\"') ||
      !network.includes('\\"psramSizeBytes\\"') ||
      !network.includes('\\"psramFreeBytes\\"') ||
      !network.includes('\\"psramLargestFreeBlockBytes\\"') ||
      !network.includes('\\"bleHostAllocPsram\\"') ||
      !network.includes('\\"bleHostAllocFallback\\"') ||
      !network.includes('\\"hciRxDropped\\"') ||
      !network.includes('\\"hciTxDropped\\"') ||
      !network.includes('\\"workBufExternal\\"') ||
      !network.includes('\\"jsonArenaExternal\\"') ||
      !network.includes('\\"allocExternalFallback\\"') ||
      !network.includes('\\"resetReasonCode\\"') ||
      !diagHtml.includes('id="diagnosticsPanel"') ||
      !diagHtml.includes('<legend>Diagnostics</legend>') ||
      !diagHtml.includes('<legend>States</legend>') ||
      !diagHtml.includes('<legend>Guards</legend>') ||
      !diagHtml.includes('<legend>Machine I/O</legend>') ||
      !diagHtml.includes('<legend>Serial</legend>') ||
      !diagHtml.includes('id="dSerialIo4"') ||
      !diagHtml.includes('id="dSerialState"') ||
      !diagHtml.includes('<legend>WiFi</legend>') ||
      !diagHtml.includes('<legend>AP</legend>') ||
      !diagHtml.includes('<legend>CPU') ||
      !diagHtml.includes('<legend>Tasks') ||
      !diagHtml.includes('<legend>RAM</legend>') ||
      !diagHtml.includes('<legend>HEAP</legend>') ||
      !diagHtml.includes('<legend>Scale</legend>') ||
      !diagHtml.includes('<legend>MISC</legend>') ||
      !diagHtml.includes('id="dMachine"') ||
      !diagHtml.includes('id="dBrew"') ||
      !diagHtml.includes('id="dCup"') ||
      !diagHtml.includes('id="dGuardNoScaleUser"') ||
      !diagHtml.includes('id="dGuardNoScaleRaw"') ||
      !diagHtml.includes('id="dGuardAtmUser"') ||
      !diagHtml.includes('id="dGuardSlowUser"') ||
      !diagHtml.includes('id="dGuardFastUser"') ||
      !diagHtml.includes('id="dGuardTouchUser"') ||
      !diagHtml.includes('id="dGuardCupUser"') ||
      !diagHtml.includes('id="exportDebugDataButton"') ||
      !diagHtml.includes('id="dActivator"') ||
      !diagHtml.includes('id="dReed"') ||
      !diagHtml.includes('class="paddleOnly">Paddle</strong>') ||
      !diagHtml.includes('class="momentaryOnly">Switch</strong>') ||
      !diagHtml.includes('class="metric reedOnly"') ||
      !diagHtml.includes('id="dRelay"') ||
      !diagHtml.includes('id="dSource"') ||
      !diagHtml.includes('id="dSafety"') ||
      !diagHtml.includes('id="dFault"') ||
      !diagHtml.includes('id="dWatchdog"') ||
      !diagHtml.includes('id="dExternal"') ||
      !diagHtml.includes('id="dRecovery"') ||
      !diagHtml.includes('id="dStream"') ||
      !diagHtml.includes('id="dControl"') ||
      !diagHtml.includes('id="dScaleRssi"') ||
      !diagHtml.includes('id="hScaleRate"') ||
      !diagHtml.includes('id="hRecoveredStales"') ||
      !diagHtml.includes('id="hStaleTime"') ||
      !ui.includes("t('dMachine',s.machineState)") ||
      !ui.includes("t('hFirmware',s.firmwareVersion)") ||
      !ui.includes("t('hBoot',typeof s.bootId==='number'&&s.bootId?'#'+s.bootId:'')") ||
      !ui.includes("t('dBrew',s.state)") ||
      !ui.includes("t('dCup',cp.state)") ||
      !ui.includes('function applyDiagnosticGuards(') ||
      !ui.includes('function exportDebugData(') ||
      !ui.includes('/api/v1/debug/export') ||
      !ui.includes("t('dActivator',s.physicalActivatorOn?'ON':'OFF')") ||
      !ui.includes("t('dReed',s.reedOn?'ON':'OFF')") ||
      !ui.includes("t('dStream',sc.streamState)") ||
      !ui.includes("t('dControl',sc.controlState)") ||
      !ui.includes("t('dScaleRssi',typeof sc.rssi==='number'?sc.rssi+' dBm':'')") ||
      !ui.includes("t('hScaleRate',typeof wi==='number'&&wi>0?") ||
      !ui.includes("(1000/wi).toFixed(1)+' Hz (≈'+wi+' ms)'") ||
      !ui.includes("t('hRecoveredStales',String(sc.recoveredStaleCount))") ||
      !ui.includes("t('hStaleTime',typeof sc.recoveredStaleMs==='number'?sc.recoveredStaleMs+' ms':'')") ||
      !diagHtml.includes('<strong>Heap min</strong>') ||
      !diagHtml.includes('<strong>Heap largest</strong>') ||
      !diagHtml.includes('<strong>PSRAM size</strong>') ||
      !diagHtml.includes('<strong>PSRAM free</strong>') ||
      !diagHtml.includes('<strong>PSRAM largest</strong>') ||
      !diagHtml.includes('<strong>Clock</strong>') ||
      !diagHtml.includes('<strong>Temp current</strong>') ||
      !diagHtml.includes('<strong>Temp peak</strong>') ||
      (diagHtml.match(/<details/g) || []).length !== 1 ||
      !diagHtml.includes('<details><summary>Last 10 resets</summary>') ||
      diagHtml.includes('<summary>Diagnostics</summary>') ||
      !diagHtml.includes('id="hFirmware"') ||
      !diagHtml.includes('id="hBoot"') ||
      !diagHtml.includes('<strong>Firmware</strong>') ||
      !diagHtml.includes('<strong>Boot</strong>') ||
      !diagHtml.includes('id="currentTime"') ||
      !diagHtml.includes('id="ntpStatus"') ||
      !diagHtml.includes('id="ntpLastSync"') ||
      !diagHtml.includes('id="ntpServer"') ||
      diagHtml.indexOf('id="diagnosticsPanel"') > diagHtml.indexOf('id="logPanel"') ||
      diagHtml.indexOf('<legend>States</legend>') > diagHtml.indexOf('<legend>Machine I/O</legend>') ||
      diagHtml.indexOf('<legend>Machine I/O</legend>') > diagHtml.indexOf('<legend>Scale</legend>') ||
      diagHtml.indexOf('<legend>Scale</legend>') > diagHtml.indexOf('<legend>Guards</legend>') ||
      diagHtml.indexOf('<legend>Guards</legend>') > diagHtml.indexOf('<legend>WiFi</legend>') ||
      diagHtml.indexOf('<legend>WiFi</legend>') > diagHtml.indexOf('<legend>AP</legend>') ||
      diagHtml.indexOf('<legend>AP</legend>') > diagHtml.indexOf('<legend>Serial</legend>') ||
      diagHtml.indexOf('<legend>Serial</legend>') > diagHtml.indexOf('<legend>CPU') ||
      diagHtml.indexOf('<legend>CPU') > diagHtml.indexOf('<legend>Tasks') ||
      diagHtml.indexOf('<legend>Tasks') > diagHtml.indexOf('<legend>RAM</legend>') ||
      diagHtml.indexOf('<legend>RAM</legend>') > diagHtml.indexOf('<legend>HEAP</legend>') ||
      diagHtml.indexOf('<legend>HEAP</legend>') > diagHtml.indexOf('<legend>MISC</legend>') ||
      diagHtml.indexOf('id="hWifiState"') > diagHtml.indexOf('id="hWifiPs"') ||
      diagHtml.indexOf('id="hWifiPs"') > diagHtml.indexOf('id="hWifiCoex"') ||
      diagHtml.indexOf('id="hWifiCoex"') > diagHtml.indexOf('id="hSsid"') ||
      diagHtml.indexOf('id="hCpu5s"') > diagHtml.indexOf('id="hCpuMhz"') ||
      diagHtml.indexOf('id="hCpuMhz"') > diagHtml.indexOf('id="hTemp"') ||
      diagHtml.indexOf('id="hRamF"') > diagHtml.indexOf('id="hHeapMin"') ||
      diagHtml.indexOf('id="hHeapMin"') > diagHtml.indexOf('id="hHeapLargest"') ||
      diagHtml.indexOf('id="hHeapLargest"') > diagHtml.indexOf('id="hPsramT"') ||
      diagHtml.indexOf('id="hPsramT"') > diagHtml.indexOf('id="hPsramF"') ||
      diagHtml.indexOf('id="hPsramF"') > diagHtml.indexOf('id="hPsramL"') ||
      diagHtml.indexOf('id="hFirmware"') > diagHtml.indexOf('id="hBoot"') ||
      diagHtml.indexOf('id="hBoot"') > diagHtml.indexOf('id="currentTime"') ||
      diagHtml.indexOf('id="currentTime"') > diagHtml.indexOf('id="ntpStatus"') ||
      diagHtml.indexOf('id="ntpStatus"') > diagHtml.indexOf('id="ntpLastSync"') ||
      adminHtml.includes('id="diagnosticsPanel"') ||
      adminHtml.includes('id="currentTime"') ||
      adminHtml.includes('<summary>Diagnostics</summary>') ||
      statusHtml.includes('id="currentTime"') ||
      statusHtml.includes('id="ntpStatus"') ||
      !css.includes('#diagnosticsPanel fieldset') ||
      !css.includes('.metric,.shotCard > *{') ||
      css.includes('#diagnosticsPanel .metric,#statusPanel .metric,#scalePanel .metric,.shotCard > *{') ||
      css.includes('diagGroup')) {
    throw new Error(
        'Diagnostics must be a non-collapsible fieldset at the top of Diagnostic, above Log, with States/Machine I/O/Scale/Guards/WiFi/AP/Serial/CPU/Tasks/RAM/HEAP/MISC sections and one value per label');
  }
}
if (!ui.includes('id="shotTable"') ||
    !ui.includes('id="exportShotsButton"') ||
    !ui.includes('id="clearShotsButton"') ||
    !html.includes('id="clearShotsButton" class="btnGlyph btnInvert"') ||
    html.includes('id="clearShotsButton" class="btnGlyph btnDanger"') ||
    !css.includes('#shotLogPanel .btnGlyph:not(.btnInvert)') ||
    !ui.includes("confirm:'CLEAR_SHOT_LOG'") ||
    !ui.includes('refreshShots()') ||
    !js.includes("'shotDur'") ||
    !js.includes("'shotActual'") ||
    !css.includes('#shotTable .shotDur,#shotTable .shotActual') ||
    !css.includes('grid-template-areas:"dur dur dur actual actual actual" "time time time time time time" "goal goal flow flow drop drop" "err err shot shot ended ended" "rate rate rate rate rate rate" "spark spark spark spark spark spark"') ||
    !css.includes('#shotTable tr.noSpark{grid-template-areas:"dur dur dur actual actual actual" "time time time time time time" "goal goal flow flow drop drop" "err err shot shot ended ended" "rate rate rate rate rate rate"}') ||
    css.includes('grid-area:guard') ||
    css.includes('grid-area:ext') ||
    css.includes('grid-area:stop') ||
    css.includes('grid-area:cut') ||
    !css.includes('#shotTable .shotDel') ||
    !js.includes("className='shotDel'") ||
    runtimeJs.includes('<span class="t">Delete</span>') ||
    !ui.includes('formatShotTime(r)') ||
    !runtimeJs.includes('function formatShotEnded(') ||
    !runtimeJs.includes('function shotDisplayActualG(') ||
    !runtimeJs.includes('shotDisplayActualG(r.actualG,r.wCg)') ||
    !runtimeJs.includes('shotDisplayActualG(ls.currentWeightG,cv.wCg)') ||
    !runtimeJs.includes('return y!=null&&y>=1') ||
    !runtimeJs.includes('shotDisplayFlowGS(r)') ||
    !runtimeJs.includes('const live=!!(s.cycle&&s.cycle.active)') ||
    runtimeJs.includes('const live=!!((s.cycle&&s.cycle.active)||s.liveShot)') ||
    runtimeJs.includes('const live=!!((s.cycle&&s.cycle.active)||s.relayClosed)') ||
    !runtimeJs.includes('const dropMs=live?(src.firstDropElapsedMs||0):(ls&&ls.firstDropElapsedMs||0)') ||
    !runtimeJs.includes('formatShotEnded(r.stopDetail)') ||
    !runtimeJs.includes("labels=['Time','Dur','Goal','Weight','Err%','Flow','1st drop','Ended','Shot']") ||
    runtimeJs.includes("labels=['Time','Dur','Goal','Actual','Err%','Flow','1st drop','Ended','Shot']") ||
    runtimeJs.includes("labels=['Time','Dur','Goal','Actual','Err%','Flow','1st drop','Guard','Ext','Stop','Shot','Cut']") ||
    partialHtml.stats.includes('<th>Guard</th>') ||
    partialHtml.stats.includes('<th>Ext</th>') ||
    partialHtml.stats.includes('<th>Stop</th>') ||
    partialHtml.stats.includes('>Cut</th>') ||
    !partialHtml.stats.includes('<th>Ended</th>') ||
    !partialHtml.stats.includes('<th>Weight</th>') ||
    partialHtml.stats.includes('<th>Actual</th>') ||
    !ui.includes('no time') ||
    !ui.includes('id="timezoneOffsetMinutes"') ||
    !js.includes('m+=15') ||
    js.includes('Request accepted.') ||
    js.includes("message('Request queued.','ok')") ||
    js.includes('Request queued successfully.') ||
    !network.includes('hasWallTime') ||
    !network.includes('endedAtLocalSec') ||
    !network.includes('SHOT_LOG_CLEAR_NOT_CONFIRMED')) {
  throw new Error('Shot history UI/API must expose table, CSV export, clear confirmation, and timezone setting');
}
if (!ui.includes('id="shotRating"') ||
    !partialHtml.home.includes('id="shotRating"') ||
    !partialHtml.home.includes('<strong>Rate</strong>') ||
    !partialHtml.stats.includes('<th>Rate</th>') ||
    !runtimeJs.includes('function fillStarRate(') ||
    !runtimeJs.includes("viewBox=\"0 0 24 24\"") ||
    runtimeJs.includes('star.jpg') ||
    runtimeJs.includes('star.png') ||
    !runtimeJs.includes("className='shotRateCell'") ||
    !runtimeJs.includes("dataset.label='Rate'") ||
    !runtimeJs.includes('function postShotRating(') ||
    !runtimeJs.includes('{id,rating:n}') ||
    !runtimeJs.includes("'rating','ended_at_ms'") ||
    !css.includes('.starRate{display:inline-flex;align-items:center;margin-top:-.6rem}') ||
    !css.includes('.starRate button+button{margin-left:-.18rem}') ||
    !css.includes('.starRate button.on{color:var(--ac)}') ||
    !css.includes('.shotCard .shotRate{grid-area:rate;display:grid;justify-items:start}') ||
    !css.includes('#shotTable td.shotRateCell{grid-area:rate}') ||
    !network.includes('shotsRateHandler') ||
    !network.includes('LAST_SHOT_NOT_FOUND') ||
    !network.includes('\\"rating\\":%u') ||
    !network.includes('\\"shotLogId\\":%lu') ||
    !lastShotIo.includes('LAST_SHOT_SCHEMA_VERSION = 2') ||
    !shotLogIo.includes('updateRating') ||
    !firmwareCore.includes('rateLastShot') ||
    !firmwareCore.includes('rateShotRecord') ||
    !firmwareCore.includes('applyLastShotManualFields')) {
  throw new Error('Shot rating must be SVG stars on last shot and history, persisted on the device');
}
if (!js.includes('function commandOkMessage(') ||
    !js.includes('function commandFailMessage(') ||
    !js.includes('function formatCommandError(') ||
    !js.includes('function homePendingPairs(') ||
    !js.includes('command(path,value={},soft,okMsg,failMsg)') ||
    js.includes("message(e&&e.message?e.message:'Request failed.','error')") ||
    !js.includes('Machine settings saved.') ||
    !js.includes("cn('save machine settings')") ||
    !js.includes('Brew settings saved.') ||
    !js.includes("'save brew settings'") ||
    !js.includes("homeFastExtractionGuardEnabledState','fastExtractionGuardEnabled','Fast extraction guard'") ||
    !js.includes("label+(on?' enabled.':' disabled.')") ||
    !js.includes("'Could not '+(on?'enable ':'disable ')+label+'.'") ||
    !js.includes('Wi-Fi settings saved. Restarting.') ||
    !js.includes('Wi-Fi sleep saved.') ||
    !js.includes("cn('save Wi-Fi settings')") ||
    !js.includes('Wi-Fi scan started.') ||
    !js.includes('Could not start Wi-Fi scan.') ||
    !js.includes('Administration unlocked.') ||
    !js.includes('Could not unlock administration.') ||
    !js.includes("R.noteReachOk();R.message('Administration unlocked.','ok')") ||
    !js.includes('Administration locked.') ||
    !js.includes('Could not lock administration.') ||
    !js.includes("noteReachOk();message('Administration locked.','ok')") ||
    !js.includes("noteReachOk();message((wanted?'BLE companion enabled.'") ||
    !js.includes('Shot history cleared.') ||
    !js.includes('Could not clear shot history.') ||
    !js.includes('BLE companion enabled.') ||
    !js.includes('Could not enable BLE companion.') ||
    !js.includes('Could not update Quick Settings.') ||
    js.includes('Shot history cleared successfully.') ||
    js.includes('Unlock failed.') ||
    js.includes('Lock failed.')) {
  throw new Error('Web UI must show action-specific success and failure toasts instead of generic queued/failed copy');
}
{
  const commandFn = runtimeJs.slice(runtimeJs.indexOf('async function command('),
      runtimeJs.indexOf('async function setBleCompanionEnabled('));
  if (!commandFn.includes("path.endsWith('/config')||path.endsWith('/presets')") ||
      !commandFn.includes('configRevision===previousRevision') ||
      commandFn.indexOf("message(okMsg||") < commandFn.indexOf("throw new Error('Device did not apply the change.')")) {
    throw new Error('Config and preset saves must confirm a new revision before showing success');
  }
}
if (!runtimeJs.includes('SHOTS_PAGE_SIZE=10') ||
    !runtimeJs.includes('SHOTS_EXPORT_LIMIT=120') ||
    !runtimeJs.includes("shotsUrl(offset,limit)") ||
    !runtimeJs.includes("'/api/v1/shots?offset='") ||
    !runtimeJs.includes("fetchShotPage(0,SHOTS_PAGE_SIZE,'replace')") ||
    !runtimeJs.includes("fetchShotPage(shotHistory.shots.length,SHOTS_PAGE_SIZE,'append')") ||
    !runtimeJs.includes("fetchShotPage(0,SHOTS_PAGE_SIZE,'poll')") ||
    !runtimeJs.includes('async function loadMoreShots(){') ||
    !runtimeJs.includes('function shotStatsViewActive(){') ||
    !runtimeJs.includes('if(ok)maybeLoadMoreShots()') ||
    !runtimeJs.includes("shotsUrl(0,SHOTS_EXPORT_LIMIT") ||
    runtimeJs.includes("api('/api/v1/shots')") ||
    !viewJs.stats.includes('IntersectionObserver') ||
    !viewJs.stats.includes("R.loadMoreShots()") ||
    !partialHtml.stats.includes('id="shotLogSentinel"') ||
    !css.includes('#shotLogSentinel{min-height:1px') ||
    !network.includes('parseShotsPageQuery') ||
    !network.includes('shotLogPageSlice') ||
    !network.includes('shotLogSortRecords') ||
    !network.includes('SHOT_LOG_PAGE_DEFAULT') ||
    !network.includes('\\"hasMore\\":%s') ||
    !network.includes('\\"total\\":%u') ||
    !network.includes('index == start ? "" : ","') ||
    !appJsSource.includes('mod.activate()')) {
  throw new Error('Shot history must page 10 shots with infinite scroll and poll only the first page');
}
const shotLogTypes = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperShotLogTypes.h'), 'utf8');
if (!partialHtml.stats.includes('id="shotSort"') ||
    !partialHtml.stats.includes('class="shotSort"') ||
    !partialHtml.stats.includes('aria-label="Sort history"') ||
    !css.includes('.shotSort{') ||
    !css.includes('.shotSort button[aria-pressed="true"]') ||
    !runtimeJs.includes('function setShotSort(') ||
    !runtimeJs.includes('function toggleShotSortDir(') ||
    !runtimeJs.includes('function syncShotSortButtons(') ||
    !runtimeJs.includes("shotsUrl(offset,limit,sort,dir)") ||
    !runtimeJs.includes("'date','desc'") ||
    !runtimeJs.includes("shotSort==='rating'") ||
    !runtimeJs.includes('shotStatsWindow') ||
    !runtimeJs.includes('Highest rating') ||
    !runtimeJs.includes('Oldest first') ||
    !runtimeJs.includes('Lowest rating') ||
    !viewJs.stats.includes('id="sortDateButton"') ||
    !viewJs.stats.includes('id="sortRatingButton"') ||
    !viewJs.stats.includes('id="sortDirButton"') ||
    !viewJs.stats.includes('Newest first') ||
    !viewJs.stats.includes("R.setShotSort('date')") ||
    !viewJs.stats.includes("R.setShotSort('rating')") ||
    !viewJs.stats.includes('R.toggleShotSortDir()') ||
    !viewJs.stats.includes('R.syncShotSortButtons()') ||
    !shotLogTypes.includes('shotLogSortRecords') ||
    !shotLogTypes.includes('ShotLogSort::Rating') ||
    !network.includes('shotLogSortFromName') ||
    !network.includes('query, "sort"') ||
    !network.includes('query, "dir"') ||
    !css.includes('button{-webkit-appearance:none;appearance:none;border-radius:2rem}') ||
    !css.includes('#shotLogPanel .btnGlyph:not(.btnInvert){background:var(--bg);color:var(--ac);border-color:var(--ln)}') ||
    !css.includes('.shotSort{display:flex;max-width:100%;border:1px solid var(--ln);border-radius:2rem;overflow:hidden;background:var(--bg)}') ||
    !css.includes('.shotSort button{') ||
    !css.includes('.shotSort button{margin:0;border:0;border-right:1px solid var(--ln);border-radius:0;background:transparent;color:var(--ac);font:inherit;font-size:.8125rem;font-weight:600') ||
    css.includes('.shotSort button{margin:0;border:0;border-right:1px solid var(--ln);background:none') ||
    css.includes('#shotLogPanel .btnGlyph:not(.btnInvert){background:transparent') ||
    css.includes('#message,.configSaveBar,#shotLogPanel .btnBar{background:var(--bg)}') ||
    css.includes('#message,.configSaveBar{background:var(--bg)}') ||
    css.includes('html.theme-dark #message,html.theme-dark .configSaveBar,html.theme-dark #shotLogPanel .btnBar{background:var(--bg)}') ||
    css.includes('html.theme-dark #message,html.theme-dark .configSaveBar{background:var(--bg)}')) {
  throw new Error('Shot history must sort by date or rating with unrated last and keep stats on newest shots');
}
const statsSection = partialHtml.stats.match(
    /<fieldset id="shotStatsPanel"><legend>Stats<\/legend>([\s\S]*?)<\/fieldset>/);
if (!statsSection ||
    partialHtml.stats.indexOf('id="shotStatsPanel"') < 0 ||
    partialHtml.stats.indexOf('id="shotStatsPanel"') >
        partialHtml.stats.indexOf('id="shotLogPanel"') ||
    !statsSection[1].includes('class="shotCard"') ||
    !statsSection[1].includes('class="shotDur"') ||
    !statsSection[1].includes('class="shotActual"') ||
    !statsSection[1].includes('<strong>Avg time</strong>') ||
    !statsSection[1].includes('<strong>Avg weight</strong>') ||
    !statsSection[1].includes('<strong>Daily shots</strong>') ||
    !statsSection[1].includes('<strong>Avg error</strong>') ||
    !statsSection[1].includes('<strong>Avg flow</strong>') ||
    !statsSection[1].includes('id="statsAvgDur"') ||
    !statsSection[1].includes('id="statsAvgWeight"') ||
    !statsSection[1].includes('id="statsAvgDaily"') ||
    !statsSection[1].includes('id="statsAvgErr"') ||
    !statsSection[1].includes('id="statsAvgFlow"') ||
    !statsSection[1].includes('class="fieldHint"') ||
    !statsSection[1].includes('Based on the last 10 shots.') ||
    !statsSection[1].includes('id="statsDurChart"') ||
    !runtimeJs.includes('function renderStatsDurChart(') ||
    !runtimeJs.includes('id="statsDurChartPlot"') ||
    !runtimeJs.includes('class="shotSparkHost"') ||
    !runtimeJs.includes('fill-opacity=".22"') ||
    !runtimeJs.includes('statsDurSparkY') ||
    !runtimeJs.includes('renderStatsDurChart()') ||
    !runtimeJs.includes('const BIN=0.5,tMax=6e4/1e3,tLow=28,tHigh=32') ||
    !runtimeJs.includes("fillChartTicks(host.lastChild,[[0,'0 s'],[tLow,L(tLow,'s')],[tHigh,L(tHigh,'s')],[tMax,L(tMax,'s')]],tMax)") ||
    !css.includes('#statsDurChart{margin-top:') ||
    !runtimeJs.includes('function renderShotStats(){') ||
    !runtimeJs.includes('shotHistory.shots.slice(0,SHOTS_PAGE_SIZE)') ||
    !runtimeJs.includes('renderShotStats();') ||
    runtimeJs.includes('slice(0,20)') ||
    !css.includes('.shotCard:has(>:nth-child(5):last-child){grid-template-areas:"dur dur dur actual actual actual" "goal goal err err flow flow"}') ||
    css.includes('#shotStatsPanel') ||
    css.includes('#statsAvgDur') ||
    css.includes('statsAvgDaily') ||
    network.includes('shotLogComputeAverages') ||
    network.includes('SHOT_LOG_STATS_WINDOW') ||
    network.includes('\\"avgDailyShots\\"') ||
    network.includes('/api/v1/shots/stats') ||
    shotLogTypes.includes('shotLogComputeAverages') ||
    shotLogTypes.includes('SHOT_LOG_STATS_WINDOW')) {
  throw new Error(
      'Stats view must be a 2+3 shotCard, duration histogram, last-10 window in JS, and no stats API/firmware');
}

{
  const start = runtimeJs.indexOf('function axisLabel(');
  const end = runtimeJs.indexOf('function renderShotStats(');
  if (start < 0 || end < 0 || end <= start) {
    throw new Error('Duration histogram helpers not found for bin checks');
  }
  const helpers = new Function(
      runtimeJs.slice(start, end) +
      ';return{renderStatsDurChart:renderStatsDurChart};')();
  const kind = new Function('t', 'return t<28?"fast":t<=32?"bbw":"slow"');
  if (kind(27.9) !== 'fast' || kind(28) !== 'bbw' ||
      kind(32) !== 'bbw' || kind(32.1) !== 'slow') {
    throw new Error('Duration histogram zone colors must use 28/32 s thresholds');
  }
  const histRoot = {dataset: {}, className: '', innerHTML: ''};
  const histPlot = {innerHTML: ''};
  global.$ = (id) => {
    if (id === 'statsDurChart') {
      return histRoot;
    }
    if (id === 'statsDurChartPlot') {
      return histPlot;
    }
    return null;
  };
  global.shotHistory = {
    shots: [
      {shotType: 'auto', actualG: 36, durationS: 30.2},
      {shotType: 'auto', actualG: 36, durationS: 30.2},
      {shotType: 'auto', actualG: 36, durationS: 27.1},
      {shotType: 'manual', actualG: 36, durationS: 40},
      {shotType: 'auto', actualG: 0.5, durationS: 30},
    ]
  };
  global.shotStatsWindow = [];
  global.SHOTS_PAGE_SIZE = 10;
  global.shotDisplayActualG = (actual) => actual;
  global.fillChartTicks = () => {};
  helpers.renderStatsDurChart();
  const filled = (histPlot.innerHTML.match(/fill-opacity=".22"/g) || []).length;
  if (filled !== 2) {
    throw new Error('Duration histogram must render filled area peaks for auto shots');
  }
  if (!histPlot.innerHTML.includes('statsDurSparkY">2<')) {
    throw new Error('Duration histogram Y axis must scale to the max bin count');
  }
  delete global.$;
  delete global.shotHistory;
  delete global.shotStatsWindow;
  delete global.SHOTS_PAGE_SIZE;
  delete global.shotDisplayActualG;
  delete global.fillChartTicks;
}

if (!ui.includes('id="firmwareFooter"') ||
    !ui.includes('id="inactiveFirmware"') ||
    !ui.includes('firmwareVersion') ||
    !ui.includes('updateFirmwareFooter()') ||
    !ui.includes("const nav=$('navFirmware')") ||
    !ui.includes("const inactive=$('inactiveFirmware')") ||
    !shellHtml.includes('id="navFirmware"') ||
    !shellHtml.includes('class="navMeta"') ||
    !css.includes('body.homeAdminActions #view-home:not(.hidden)~.pageFooter{margin-bottom:calc(7rem + env(safe-area-inset-bottom))}') ||
    css.includes('body.homeAdminActions #view-home:not(.hidden)~.pageFooter{display:none}') ||
    !css.includes('#actionsPanel{position:fixed;left:0;right:0') ||
    !css.includes('@media(min-width:700px){#actionsPanel{left:1rem;right:1rem') ||
    !network.includes('\\"firmwareVersion\\"') ||
    !network.includes('\\"bootId\\":%lu') ||
    !network.includes('FW_VERSION')) {
  throw new Error('Firmware version must be exposed in status API, nav menu, and Diagnostic');
}
if (!shellHtml.includes('https://github.com/Cheerpipe/AcaiaArduinoBLE') ||
    !shellHtml.includes('https://github.com/Cheerpipe') ||
    !shellHtml.includes('Hecho por') ||
    !shellHtml.includes('>Cheerpipe</a>') ||
    shellHtml.indexOf('class="navMeta"') > shellHtml.indexOf('id="app"') ||
    !css.includes('.navMeta{margin-top:auto')) {
  throw new Error('Web UI footer must credit the GitHub repo and Cheerpipe in the nav menu');
}
if (!shellHtml.includes('id="message"') ||
    !shellHtml.includes('id="messageText"') ||
    !shellHtml.includes('id="messageClose"') ||
    !runtimeJs.includes('function clearMessage(') ||
    !runtimeJs.includes('b.onclick=clearMessage') ||
    !runtimeJs.includes("kind==='ok'?5e3") ||
    !runtimeJs.includes("kind==='warn'&&!e.querySelector('button:not(#messageClose)')?15e3") ||
    !runtimeJs.includes('setTimeout(clearMessage,ms)') ||
    !css.includes('#message:not(.error):not(.warn){display:none}') ||
    !css.includes('.messageClose')) {
  throw new Error('Status message bar must auto-hide ok/warn and stay for errors');
}
if (!css.includes('.hidden,[hidden]{display:none!important}') ||
    css.includes('#message[hidden]{display:flex') ||
    !/#message\{[^}]*min-height:var\(--tap\)/.test(css)) {
  throw new Error('Status message bar must have an accessible visible target and no hidden footprint');
}
if (!/<fieldset[^>]*><legend>Log<\/legend>/.test(html) ||
    /authenticatedOnly[^>]*><legend>Log<\/legend>/.test(html) ||
    !ui.includes('loadLog()') ||
    !ui.includes('refreshLog()') ||
    !(ui.includes("name==='diagnostic'") || ui.includes("name === 'diagnostic'")) ||
    !ui.includes('id="view-diagnostic"') ||
    !ui.includes('data-route="/diagnostic"') ||
    !html.includes('>Diagnostic</a>') ||
    !ui.includes('id="logLevelFilter"') ||
    !ui.includes('e.level') ||
    !ui.includes('value="boot"') ||
    html.indexOf('id="ringRetainLogLevel"') < html.indexOf('id="view-diagnostic"') ||
    html.indexOf('id="ringRetainLogLevel"') >
        html.indexOf('</section>', html.indexOf('id="view-diagnostic"')) ||
    html.indexOf('id="serialLogLevel"') < html.indexOf('id="view-diagnostic"') ||
    html.indexOf('id="serialLogLevel"') >
        html.indexOf('</section>', html.indexOf('id="view-diagnostic"')) ||
    html.indexOf('id="ringRetainLogLevel"') > html.indexOf('id="serialLogLevel"') ||
    !html.includes('id="serialLogLevel" class="mutable"') ||
    !html.includes('id="ringRetainLogLevel" class="mutable"') ||
    html.includes('id="navLogWrap"') ||
    ui.includes('function ringLogEnabled(') ||
    ui.includes('function updateLogNavVisibility(') ||
    !html.includes('<hr class="logSep">') ||
    html.indexOf('<hr class="logSep">') < html.indexOf('id="serialLogLevel"') ||
    html.indexOf('<hr class="logSep">') > html.indexOf('id="logLevelFilter"') ||
    !css.includes('.logSep') ||
    html.includes('data-route="/debug"') ||
    html.includes('id="view-debug"') ||
    html.includes('id="view-log"')) {
  throw new Error('Diagnostic tab must host always-visible Log with ring/serial controls and separator');
}
if (!network.includes('historyOverwritten') ||
    !network.includes('missedEvents') ||
    !network.includes('serialDropped') ||
    !network.includes('hasMore') ||
    !network.includes('cursorInvalid') ||
    !ui.includes('logBootId') ||
    !ui.includes('Missed while disconnected') ||
    !ui.includes('d.cursorInvalid')) {
  throw new Error('Diagnostic log must distinguish history rotation from unread and serial loss');
}
if (!ui.includes('id="factoryResetButton"') ||
    !ui.includes("confirm('Restore all factory settings?") ||
    !ui.includes("confirm:'ERASE_ALL_SETTINGS'") ||
    !network.includes('FACTORY_RESET_NOT_CONFIRMED') ||
    !network.includes('resetAllDurableStores(next)') ||
    !network.includes('ensureFactoryResetIntent(') ||
    !network.includes('releaseNvsSpaceForFactoryReset') ||
    !ui.includes('id="restartPanel"') ||
    html.indexOf('id="saveDateTimeButton"') > html.indexOf('id="restartPanel"') ||
    html.indexOf('id="restartPanel"') > html.indexOf('id="factoryResetButton"') ||
    html.slice(html.indexOf('id="actionsPanel"'), html.indexOf('id="view-stats"'))
        .includes('restartButton') ||
    !html.includes('id="restartButton" class="btnGlyph btnWarn"') ||
    html.includes('id="restartButton" class="btnGlyph btnInvert"') ||
    !html.includes('id="factoryResetButton" class="btnGlyph mutable btnInvert"') ||
    html.includes('id="factoryResetButton" class="btnGlyph mutable btnWarn"') ||
    !css.includes('.btnGlyph.btnInvert')) {
  throw new Error('Factory reset must require UI and server-side confirmation');
}
if (!html.includes('<legend>NVS</legend>') ||
    !html.includes('id="hNvsLastFailure"') ||
    !js.includes('const nv=s.nvs||{}') ||
    !network.includes('\\\"availableEntries\\\"') ||
    !network.includes('\\\"flashIoLockTimeouts\\\"') ||
    !network.includes('captureNvsDiagnostics()')) {
  throw new Error('Diagnostic status, UI, and debug export must expose bounded NVS diagnostics');
}
if (!css.includes('.btnBar,.presetActions{display:flex;gap:.5rem') ||
    css.includes('.btnBar,.presetActions{display:flex;gap:0') ||
    !css.includes('.btnGlyph{display:inline-flex;align-items:center;justify-content:center;gap:.45rem;margin:0;border:1px solid var(--ln);border-radius:2rem;background:var(--bg)') ||
    css.includes('.btnGlyph.btnDanger,.btnGlyph.btnInvert{background:var(--pri)') ||
    !css.includes('.btnGlyph.btnInvert{background:var(--pri)') ||
    !css.includes('.btnGlyph.btnDanger{background:var(--bg);color:var(--ac);border-color:var(--ac)}') ||
    css.includes('#factoryResetButton,#clearShotsButton{') ||
    !css.includes('#actionsPanel .btnGlyph{flex:1;border:1.5px solid var(--ac);border-radius:2rem;background:transparent;color:var(--ac);min-height:3.25rem}') ||
    !css.includes('#actionsPanel .btnGlyph.btnDanger{background:var(--pri);color:var(--on);border-color:var(--pri)}') ||
    !css.includes('#shotLogPanel .btnBar{') ||
    !css.includes('#shotLogPanel .btnBar{position:sticky;top:var(--hdr);z-index:6;background:var(--sf)') ||
    css.includes('#shotLogPanel .btnBar{position:sticky;top:var(--hdr);z-index:6;background:var(--sf);margin:0 0 .65rem;border:1px solid var(--ln);border-radius:var(--r);overflow:hidden}')) {
  throw new Error('Action buttons must be separate with a gap; btnDanger must not share invert fill');
}
if (html.includes('id="debugPanel"') ||
    html.includes('id="view-debug"') ||
    html.includes('data-route="/debug"') ||
    ui.includes('function debugBuzzer(') ||
    ui.includes('function debugBookoo(') ||
    ui.includes('/api/v1/control/buzzer') ||
    ui.includes('/api/v1/control/bookoo') ||
    network.includes('buzzerHandler') ||
    network.includes('bookooHandler') ||
    network.includes('/api/v1/status/debug') ||
    network.includes('"/debug"') ||
    network.includes('StatusPage::Debug')) {
  throw new Error('Debug tab/API must be removed from Web UI and network handlers');
}
