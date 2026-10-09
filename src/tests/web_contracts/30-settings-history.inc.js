{
  const childId = 'touchStopFallbackEnabled';
  const formatter = blockAt(runtimeJs, 'function formatShotEnded(');
  const ended = new Function(formatter + ';return formatShotEnded;')();
  for (const reason of ['touch_weight_fallback', 'TOUCH_WEIGHT_FALLBACK']) {
    if (ended(reason) !== 'Touch fallback')
      throw new Error('Ended must identify sustained-weight touch protection');
  }
  if (!codeIncludes(runtimeJs, 'formatShotEnded(r.stopDetail)') ||
      !codeIncludes(runtimeJs, 'formatShotEnded(d.endReason)')) {
    throw new Error('Stats and Home must share the Ended cause formatter');
  }
  if (html.indexOf(`id="${childId}"`) < html.indexOf('id="avoidAccidentalTouchEnabled"') ||
      !html.includes(`id="${childId}" type="checkbox" checked`) ||
      !network.includes('touchStopFallbackEnabled must be a boolean.')) {
    throw new Error('Touch fallback must be a default-ON subordinate setting');
  }
  const groups = runtimeJs.slice(runtimeJs.search(/function\s+updateConfigGroups\(\)\s*\{/),
    runtimeJs.indexOf('function extRate('));
  const payload = blockAt(allJs, 'function brewPayload()');
  const bindings = viewJs.settings.slice(
    viewJs.settings.search(/document\s*\.\s*querySelectorAll\(\s*['"]#workflowPanel input/),
    viewJs.settings.search(/const\s+resetBbw\s*=/));
  for (const parent of [false, true]) for (const saved of [false, true]) {
    for (const bbw of [false, true]) for (const mutable of [false, true]) {
      const nodes = {};
      const get = id => nodes[id] ||= {
        checked: false, value: 'off', disabled: !mutable, closest: () => null
      };
      get('avoidAccidentalTouchEnabled').checked = parent;
      get(childId).checked = saved;
      get('brewByWeight').checked = bbw;
      for (const key of ['cupProtectionEnabled', 'stopIfCupRemoved', 'requireCupToStart'])
        get(key).checked = saved;
      const document = {
        querySelector: () => null,
        querySelectorAll: selector => selector === '.touchStopOpt' ? [{
          classList: {toggle() {}}, querySelectorAll: () => [get(childId)]
        }] : [],
        documentElement: {classList: {contains: () => false}}
      };
      const refresh = new Function('$', 'document', 'controlsMutable', 'updateScalePreferenceOptions',
        'soundAlertsAreOn', 'updateScaleIncapableAlertControls', 'number',
        'updateBullseyeControls', 'updateBbwControls', groups + ';return updateConfigGroups;')(
        get, document, mutable, () => {}, () => true, () => {}, () => 50,
        () => {}, () => {});
      refresh();
      if (get(childId).checked !== saved ||
          get(childId).disabled !== (!mutable || !parent || !bbw)) {
        throw new Error('Inactive touch fallback must retain its saved checkbox value');
      }
      const result = new Function('$', 'number', 'sToMs', 'bbwFormPresetId', 'document',
        payload + ';return brewPayload();')(get, () => 36, () => 1000, 1, document);
      if (result.touchStopFallbackEnabled !== saved || result.brewByWeight !== bbw ||
          ['cupProtectionEnabled', 'stopIfCupRemoved', 'requireCupToStart'].some(k => result[k] !== saved) ||
          ['operationalWallMs', 'bbwProtectionMs', 'minBbwBrewTimeMs', 'maxBbwBrewTimeMs',
            'autoToManualGuardManualLimitMs', 'autoToManualGuardBaselineMs'].some(k => result[k] !== 1000)) {
        throw new Error('Saving a disabled touch fallback must preserve its value');
      }
      if (mutable && parent) {
        const field = get('brewByWeight'), events = {};
        field.id = 'brewByWeight';
        field.addEventListener = (type, fn) => events[type] = fn;
        new Function('document', 'R', bindings)({querySelectorAll: () => [field]}, {
          settingsSectionOf: () => 'brew', markBrewDirty() {},
          clearBrewFieldErrors() {}, refreshBrewWarnings() {},
          updateBbwControls() {}, updateConfigGroups: refresh
        });
        for (const type of ['input', 'change']) {
          field.checked = !field.checked;
          events[type]();
          if (get(childId).checked !== saved || get(childId).disabled === field.checked)
            throw new Error('BBW edits must update the fallback immediately without polling');
        }
      }
    }
  }
}

{
  const labels = ['Automatic tare outside a brew',
    'Retare when adding or removing an accessory', 'Automatic tare at shot start',
    'Late-cup retare during a shot'];
  if (labels.some(label => !html.includes(label)) ||
      !html.includes('id="autoTareOutsideBrew" type="checkbox" checked') ||
      !html.includes('id="retareAccessoryOutsideBrew" type="checkbox" checked') ||
      html.indexOf('id="retareAccessoryOutsideBrew"') <
          html.indexOf('id="autoTareOutsideBrew"') ||
      html.indexOf('id="retareAccessoryOutsideBrew"') >
          html.indexOf('id="autoTare"') ||
      !network.includes('autoTareOutsideBrew must be a boolean.') ||
      !network.includes('retareAccessoryOutsideBrew must be a boolean.') ||
      !network.includes('"retareAccessoryOutsideBrew"') ||
      !firmwareCore.includes('candidate.autoTareOutsideBrew = command.config.autoTareOutsideBrew;')) {
    throw new Error('Idle tare must have an independent default-ON machine setting');
  }
  const payloadLine = blockAt(allJs, 'function machinePayload()');
  if (!payloadLine) throw new Error('Missing machine payload function');
  const makePayload = new Function('$', 'number', 'sToMs', 'extRate',
    'HOME_GUARD_SWITCHES', 'homeSwitchPending', 'syncSettingsFromHomeSwitches',
    payloadLine + ';return machinePayload();');
  for (const idleOn of [false, true]) for (const accessoryOn of [false, true]) {
    const payload = makePayload(id => ({checked: (id === 'autoTareOutsideBrew' && idleOn) ||
      (id === 'retareAccessoryOutsideBrew' && accessoryOn),
      value: 'off'}), () => 1, () => 1000, value => value, [], {}, () => {});
    if (payload.autoTareOutsideBrew !== idleOn ||
        payload.retareAccessoryOutsideBrew !== accessoryOn || payload.autoTare !== false) {
      throw new Error('Idle/accessory tare payload must be independent of shot-start tare');
    }
  }
  if (!codeIncludes(js, "['autoTare','autoTareOutsideBrew','retareAccessoryOutsideBrew','brewByWeight'")) {
    throw new Error('Settings hydration must restore both saved idle tare values');
  }
}

{
  const ids = ['presetNewBtn', 'presetDupBtn', 'presetResetBtn', 'presetDeleteBtn'];
  for (const id of ids) {
    if (!partialHtml.settings.includes(`id="${id}" disabled`)) {
      throw new Error(`${id} must start disabled before settings load`);
    }
  }
  const source = runtimeJs.slice(runtimeJs.search(/function\s+updatePresetActionButtons\(\)\s*\{/),
    runtimeJs.indexOf('async function applyPreset('));
  const update = new Function('$', 'document', 'selectedPreset', 'controlsMutable', 'presetsLoaded',
    source + ';updatePresetActionButtons();');
  for (const loaded of [false, true]) for (const mutable of [false, true]) {
    for (const preset of [null, {isFactory: true}, {isFactory: false}]) {
      const nodes = Object.fromEntries(ids.map(id => [id,
        {id, disabled: false, classList: {toggle() {}}}]));
      update(id => nodes[id], {querySelectorAll: () => ids.map(id => nodes[id])},
        () => preset, mutable, loaded);
      const ready = loaded && mutable;
      const expected = [!ready, !ready, !ready || !preset?.isFactory,
        !ready || !preset || !!preset.isFactory];
      if (ids.some((id, i) => nodes[id].disabled !== expected[i])) {
        throw new Error('Preset actions must wait for data and preserve editing restrictions');
      }
    }
  }
  if (!codeIncludes(runtimeJs, 'function ingestPresets(s){if(!s.presets)return;presetsLoaded=true;')) {
    throw new Error('Preset actions must refresh after loading settles');
  }
}

if (/R\.(homeFlushConfig|homeFlushPreset|configLoaded|formRev|brewDirty)\s*=/.test(js) ||
    !codeIncludes(js, 'function persistHomeBrewByWeight(') ||
    !codeIncludes(js, 'function invalidateSettingsHydration(') ||
    !codeIncludes(js, 'function clearBrewDirty(')) {
  throw new Error(
      'View modules must call runtime helpers instead of assigning read-only ESM namespace exports');
}
if (!codeIncludes(ui, '<legend>Brew</legend>') ||
    !codeIncludes(ui, '<legend>Machine and scale</legend>') ||
    !codeIncludes(ui, '<legend>Network</legend>') ||
    !codeIncludes(ui, '<legend>Device password</legend>') ||
    !codeIncludes(ui, '<legend>Frontend</legend>') ||
    !codeIncludes(ui, 'id="presetCards"') ||
    partialHtml.settings.includes('id="presetCardsState"') ||
    partialHtml.settings.includes('class="presetCard skeleton"') ||
    codeIncludes(runtimeJs, 'settlePanel(') ||
    !partialHtml.settings.includes('id="presetCardsWrap" class="panelWrap"') ||
    !css.includes('.panelWrap{position:relative}') ||
    !codeIncludes(ui, 'id="presetNewBtn"') ||
    !codeIncludes(ui, 'id="presetDupBtn"') ||
    codeIncludes(ui, 'id="presetLoadBtn"') ||
    codeIncludes(ui, 'id="presetSaveBtn"') ||
    !html.includes('id="saveBrewPresetButton" class="btnGlyph mutable btnInvert" data-dirty="0" disabled') ||
    !html.includes('id="saveConfigButton" class="btnGlyph mutable btnInvert" data-dirty="0" disabled') ||
    html.includes('id="exportShotsButton" class="btnGlyph btnInvert"') ||
    !css.includes('font-variant-emoji:text') ||
    !html.includes('<span class="g">×</span>') ||
    !html.includes('<span class="g">✓</span>') ||
    codeIncludes(ui, '💾') || codeIncludes(ui, '⚡') ||
    !codeIncludes(ui, 'Save brew settings') ||
    !codeIncludes(ui, 'id="activeBrewProfileHint"') ||
    !codeIncludes(ui, 'Current profile: ') ||
    !codeIncludes(ui, 'function updateActiveBrewProfileHint(') ||
    html.indexOf('id="activeBrewProfileHint"') <
        html.indexOf('id="saveBrewPresetButton"') ||
    html.indexOf('id="saveBrewPresetButton"') <
        html.indexOf('id="resetGuardSamplesButton"') ||
    html.indexOf('id="saveBrewPresetButton"') >
        html.indexOf('<legend>Machine and scale</legend>') ||
    html.indexOf('id="saveConfigButton"') <
        html.indexOf('<summary>Alerts</summary>') ||
    html.indexOf('id="saveConfigButton"') >
        html.indexOf('<legend>Network</legend>') ||
    !codeIncludes(ui, 'id="presetResetBtn"') ||
    !codeIncludes(ui, 'id="presetDeleteBtn"') ||
    !codeIncludes(ui, 'id="presetRenameDialog"') ||
    !codeIncludes(ui, 'id="homePresetAcc"') ||
    !html.includes('<legend>Presets</legend>') ||
    html.indexOf('id="presetPanel"') > html.indexOf('<legend>Presets</legend>') ||
    html.indexOf('<legend>Presets</legend>') > html.indexOf('id="homePresetAcc"') ||
    html.indexOf('id="homePresetBlock"') > html.indexOf('id="homePresetAcc"') ||
    !codeIncludes(ui, 'id="homeBrewByWeight"') ||
    !codeIncludes(ui, 'id="homeNoScaleBbwEnabled"') ||
    !codeIncludes(ui, 'id="quickSettingsPanel"') ||
    !html.includes('class="homeSwitchGrid"') ||
    !html.includes('id="homeBbwSub"') ||
    !html.includes('id="homeNoScaleSub"') ||
    !html.includes('id="homeFastSub"') ||
    !html.includes('id="homeTouchSub"') ||
    !codeIncludes(ui, 'function formatAccidentalTouch(') ||
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
    !codeIncludes(ui, 'id="homeNoScaleBbwMode"') ||
    !codeIncludes(ui, 'id="homeFastExtractionGuardEnabled"') ||
    !codeIncludes(ui, 'id="homeSlowExtractionGuardEnabled"') ||
    !codeIncludes(ui, 'id="homeAutoToManualGuardEnabled"') ||
    html.indexOf('id="quickSettingsPanel"') > html.indexOf('id="equipmentPanel"') ||
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
    html.indexOf('id="homeFastExtractionGuardEnabled"') > html.indexOf('id="equipmentPanel"') ||
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
    !codeIncludes(ui, 'id="cupProtectionEnabled"') ||
    html.indexOf('id="cupProtectionEnabled"') >
        html.indexOf('id="stopIfCupRemoved"') ||
    !codeIncludes(ui, 'id="stopIfCupRemoved"') ||
    !codeIncludes(ui, 'id="requireCupToStart"') ||
    html.indexOf('id="requireCupToStart"') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    codeIncludes(ui, 'id="cupPresentWeightG"') ||
    html.includes('id="cupPresentWeightG"') ||
    html.includes('id="requireCupToStart" type="checkbox" checked') ||
    !codeIncludes(ui, 'If the cup was already tared before connection, lift it and place it again.') ||
    !codeIncludes(ui, 'id="homeCupProtectionEnabled"') ||
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
    !codeIncludes(ui, 'function updateHomeGuardSwitchesLock(') ||
    !codeIncludes(ui, 'function persistHomeGuard(') ||
    !codeIncludes(ui, 'function flushHomeGuards(') ||
    !codeIncludes(ui, 'function scheduleHomeGuardFlush(') ||
    !codeIncludes(ui, 'function withCommandGate(') ||
    !codeIncludes(ui, 'function beginHomeSwitchPending(') ||
    !codeIncludes(ui, 'function applyPolledHomeSwitch(') ||
    !codeIncludes(ui, 'function applyHomeSwitchesFromConfig(') ||
    !codeIncludes(ui, 'Date.now()+5e3') ||
    !codeIncludes(ui, 'pollAt<p.until') ||
    !codeIncludes(ui, "classList.toggle('switchPending',!!on)") ||
    !codeIncludes(ui, "persistHomeGuard('homeFastExtractionGuardEnabled'") ||
    !codeIncludes(ui, "persistHomeGuard('homeAvoidAccidentalTouchEnabled'") ||
    !codeIncludes(ui, "persistHomeGuard('homeSlowExtractionGuardEnabled'") ||
    !codeIncludes(ui, "persistHomeGuard('homeAutoToManualGuardEnabled'") ||
    !codeIncludes(ui, "persistHomeGuard('homeCupProtectionEnabled'") ||
    !codeIncludes(ui, 'onchange=R.persistHomeNoScaleBbw') ||
    !codeIncludes(ui, 'function persistHomeNoScaleBbw(') ||
    !codeIncludes(ui, "p.noScaleBbwMode=$('homeNoScaleBbwEnabled').checked?nsm:'off'") ||
    codeIncludes(ui, "persistHomeGuard('homeSoundAlertsEnabled'") ||
    !codeIncludes(ui, "'fastExtractionGuardEnabled',1") ||
    !codeIncludes(ui, "'avoidAccidentalTouchEnabled',1") ||
    !codeIncludes(ui, "'slowExtractionGuardEnabled',1") ||
    !codeIncludes(ui, "'autoToManualGuardEnabled',1") ||
    !codeIncludes(ui, "'cupProtectionEnabled',1") ||
    !codeIncludes(ui, 'el.disabled=!controlsMutable||u||off') ||
    codeIncludes(ui, 'el.disabled=!controlsMutable||off||pend') ||
    !codeIncludes(ui, 'homeFlushBusy') ||
    !codeIncludes(ui, 'scheduleHomeGuardFlush()') ||
    !codeIncludes(ui, "classList.toggle('fieldOff',off)") ||
    !codeIncludes(ui, 'bbw.disabled=!controlsMutable||x') ||
    !codeIncludes(ui, 'function homeSwitchUnset(') ||
    !codeIncludes(ui, "classList.add('swR')") ||
    !codeIncludes(ui, "typeof c[k]==='boolean'") ||
    html.includes('id="homeBrewByWeight" type="checkbox" role="switch" aria-label="Brew by weight" checked') ||
    html.includes('id="homeNoScaleBbwMode" type="checkbox"') ||
    !html.includes('id="homeNoScaleBbwEnabled" type="checkbox" role="switch" aria-label="No-scale BBW"') ||
    html.includes('id="homeSoundAlertsEnabled"') ||
    html.includes('id="homeCupProtectionEnabled" type="checkbox" role="switch" aria-label="Cup protection" checked') ||
    !css.includes('.switchRow.switchPending') ||
    !css.includes('.ktrack{position:relative;flex:none;width:2.3rem;height:1.3rem;border-radius:2rem;background:var(--bd);transition:.2s}') ||
    !css.includes('.kknob{position:absolute;top:.15rem;left:.15rem;width:1rem;height:1rem;border-radius:50%;background:#fff;box-shadow:0 1px 2px rgba(0,0,0,.3);transition:.2s}') ||
    !css.includes('.homeGuardGrid input:checked~.ktrack{background:var(--ac)}') ||
    !css.includes('.homeGuardGrid input:checked~.ktrack .kknob{transform:translateX(1rem)}') ||
    !css.includes('.homeSwitchGrid') ||
    !css.includes('justify-content:space-between') ||
    !css.includes('.homeSwitchGrid{') ||
    !css.includes('border-bottom:1px solid') ||
    !css.includes('.switchState{display:none') ||
    !css.includes('.homeSwitchGrid .swS') ||
    !css.includes('.homeGuardGrid{') ||
    !css.includes('grid-template-columns:repeat(2,minmax(0,1fr))') ||
    !css.includes('#brewModeRow .swL{font-size:1.05rem;font-weight:700;line-height:1.2;color:var(--fg);letter-spacing:0}') ||
    css.includes('.ruleChartHead') || css.includes('.ruleChartMode') ||
    !css.includes('.homeGuardGrid .switchRow.switchPending .ktrack{opacity:1;background:var(--wn);border-color:var(--wn)}') ||
    !codeIncludes(ui, 'function persistHomeBrewByWeight(') ||
    !codeIncludes(ui, "onchange=R.persistHomeBrewByWeight") ||
    !codeIncludes(ui, 'beginHomeSwitchPending(h,on)') ||
    codeIncludes(ui, 'id="clearLastShotButton"') ||
    css.includes('#shotPanel') ||
    !css.includes('#shotTable .btnGlyph{border:0;border-radius:2rem;min-height:var(--tap);min-width:var(--tap);padding:0;flex:0 0 auto;background:none;box-shadow:none;filter:none') ||
    html.includes('id="lastCycle"') ||
    !codeIncludes(ui, 'function renderShotHero(') ||
    !codeIncludes(ui, 'function renderShotSpark(') ||
    !network.includes('\\"lastShot\\"') ||
    !network.includes('\\"curve\\":{%s}') ||
    !network.includes('formatShotCurveJsonBody') ||
    !network.includes('lastShotClearHandler') ||
    !network.includes('LAST_SHOT_CLEAR_NOT_CONFIRMED') ||
    !firmware.includes('lastShotFromEndedCycle') ||
    !firmware.includes('endedCycleDurationMs') ||
    firmware.includes('elapsedMs(relayBeforeOpen.closedAtMs)') ||
    !firmware.includes('clearLastShot') ||
    !firmware.includes('clearLastShotRuntimeState') ||
    !firmware.includes('serviceShotStorePersistence') ||
    !firmware.includes('durableFlashWriteAllowed(') ||
    !domainCore.includes('durableFlashWriteAllowed') ||
    !activationStoresIo.includes('shotLogPersistFailLatched_') ||
    !firmwareCore.includes(
        'shotStorePersistImage->serviceStep(FLASH_IO_LOCK_TIMEOUT_MS,') ||
    !firmwareCore.includes('shotStorePersistRetryAtMs') ||
    !firmwareCore.includes('SHOT_STORE_PERSIST_RETRY_MS') ||
    !firmware.includes('noteScaleHistory(seenMac, seenName, false)') ||
    wallClock.includes('monotonicMs >= anchorMonotonicMs_') ||
    !wallClock.includes('monotonicElapsedMs(monotonicMs, anchorMonotonicMs_)') ||
    !network.includes('StaJoinHints ShotStopperNetwork::staJoinHints()') ||
    !dualSlotFlashIo.includes(
        'flashIoWriteChunked(part, targetOffset, &store_, sizeof(store_))') ||
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
    // One gate call per destructive history handler (shared helper plus
    // call sites); a dedup that reduces the raw occurrences is fine as long
    // as the gate itself stays in the network sources.
    (network.match(/historyMutationAllowed/g) || []).length < 4 ||
    codeIncludes(ui, 'id="view-presets"') ||
    codeIncludes(ui, 'data-route="/presets"') ||
    codeIncludes(ui, 'id="presetsPageCards"') ||
    codeIncludes(ui, 'id="homePresetChips"') ||
    codeIncludes(ui, 'id="homePresetCards"') ||
    !codeIncludes(ui, "action:'new'") ||
    !codeIncludes(ui, "action:'duplicate'") ||
    !codeIncludes(ui, "action:'rename'") ||
    !codeIncludes(ui, "action:'restore_factory_values'") ||
    !codeIncludes(ui, 'function startRenamePreset(') ||
    !codeIncludes(ui, 'function updatePresetActionButtons(') ||
    !codeIncludes(ui, "function presetSummary(p){return 'Target '+(p.goalWeightG||'?')+' g'}") ||
    !codeIncludes(ui, "badge.textContent=p.isFactory?'factory':'custom'") ||
    !codeIncludes(ui, 'Discard them and switch presets') ||
    !codeIncludes(ui, 'saveBrewPreset') ||
    !codeIncludes(ui, '/api/v1/presets') ||
    codeIncludes(ui, 'id="presetNameInput"') ||
    !network.includes('/api/v1/presets') ||
    !network.includes('presetsHandler') ||
    !network.includes('restore_factory_values') ||
    !network.includes('delta.field("presets"') ||
    network.includes('"/presets"') ||
    !css.includes('.presetCard') ||
    !css.includes('.presetAccItem') ||
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
  const statusHtml = html.slice(html.indexOf('id="equipmentPanel"'),
                                html.indexOf('id="actionsPanel"'));
  if (!codeIncludes(ui, 'id="hCpu5s"') ||
      !codeIncludes(ui, 'id="hCpu1m"') ||
      !codeIncludes(ui, 'id="hCpu5m"') ||
      !codeIncludes(ui, 'id="hCpuMhz"') ||
      !codeIncludes(ui, 'id="hWifiState"') ||
      !codeIncludes(ui, 'id="hWifiPs"') ||
      !codeIncludes(ui, 'id="hWifiCoex"') ||
      !codeIncludes(ui, 'id="hSsid"') ||
      !codeIncludes(ui, 'id="hWifiChannel"') ||
      !codeIncludes(ui, 'id="hWifiIp"') ||
      !codeIncludes(ui, 'id="hWifiSignal"') ||
      !codeIncludes(ui, 'id="hWifiRssi"') ||
      !codeIncludes(ui, 'id="hApState"') ||
      !codeIncludes(ui, 'id="hApSsid"') ||
      !codeIncludes(ui, 'id="hApIp"') ||
      !codeIncludes(ui, 'id="hApClients"') ||
      !codeIncludes(ui, 'id="hUptime"') ||
      !codeIncludes(ui, 'id="hFirmware"') ||
      !codeIncludes(ui, 'id="hBoot"') ||
      !codeIncludes(ui, 'id="hResetReason"') ||
      !codeIncludes(ui, 'id="hTemp"') ||
      !codeIncludes(ui, 'id="hTPeak"') ||
      !codeIncludes(ui, 'id="hRamT"') ||
      !codeIncludes(ui, 'id="hRamU"') ||
      !codeIncludes(ui, 'id="hRamF"') ||
      !codeIncludes(ui, 'id="hTaskState"') ||
      !codeIncludes(ui, 'id="hTaskElapsed"') ||
      !codeIncludes(ui, 'id="taskProfilerStartButton"') ||
      !codeIncludes(ui, 'id="taskProfilerStopButton"') ||
      !codeIncludes(ui, 'id="hProfileState"') ||
      !codeIncludes(ui, 'id="hProfileElapsed"') ||
      !codeIncludes(ui, 'id="hProfileRecords"') ||
      !codeIncludes(ui, 'id="hProfileCapacity"') ||
      !codeIncludes(ui, 'id="hProfileRemaining"') ||
      !codeIncludes(ui, 'id="scaleProfileStartButton"') ||
      !codeIncludes(ui, 'id="scaleProfileStopButton"') ||
      !codeIncludes(ui, 'id="scaleProfileDeleteButton"') ||
      !codeIncludes(ui, 'id="scaleProfileDownloadButton"') ||
      !codeIncludes(ui, 'btnGlyph btnInvert scaleProfileCtl"') ||
      !codeIncludes(ui, 'function applyScaleProfile(') ||
      !codeIncludes(ui, '/api/v1/diagnostic/scale-profile') ||
      !codeIncludes(ui, 'id="taskTable"') ||
      !codeIncludes(ui, 'id="taskTableBody"') ||
      !codeIncludes(ui, 'id="taskTableHint"') ||
      !codeIncludes(ui, 'function applyTaskProfiler(') ||
      !codeIncludes(ui, "[['taskProfiler','profiler','enabled'],['scaleProfile','scale-profile','action']") ||
      !codeIncludes(ui, '100% = 1 core busy (sum can exceed 100)') ||
      !codeIncludes(ui, 'id="hHeapMin"') ||
      !codeIncludes(ui, 'id="hHeapLargest"') ||
      !codeIncludes(ui, 'id="hPsramT"') ||
      !codeIncludes(ui, 'id="hPsramF"') ||
      !codeIncludes(ui, 'id="hPsramL"') ||
      !codeIncludes(ui, 'id="hLoopGap"') ||
      !codeIncludes(ui, 'id="hLoopMax"') ||
      !codeIncludes(ui, 'id="loopMaxResetButton"') ||
      !diagHtml.includes('Max scale gap (<button id="scaleGapMaxResetButton" class="inlineAction diagCtl" type="button">Reset</button>)') ||
      !codeIncludes(ui, 'id="hScaleGapMax"') ||
      !codeIncludes(ui, 'id="loopTimingTable"') ||
      !codeIncludes(ui, 'id="loopTimingBody"') ||
      !diagHtml.includes('<th>Loop gap</th><th>Loop max</th>') ||
      !codeIncludes(ui, "[['loopMax','loop-max'],['scaleGapMax','scale-gap-max']") ||
      !codeIncludes(ui, "$(id+'ResetButton').onclick=()=>R.command('/api/v1/diagnostic/'+path+'/reset',{})") ||
      !codeIncludes(ui, "t('hScaleGapMax',sc.maxPacketGapMs+") ||
      !codeIncludes(ui, 'id="lastCommandState"') ||
      !codeIncludes(ui, 'function updH(') ||
      !codeIncludes(ui, 'updH(s.health,s.safety)') ||
      !codeIncludes(ui, 'function applyDiagnosticStatus(') ||
      !codeIncludes(ui, "t('hLoopMax',s.health.loopMaxGapMs+") ||
      !codeIncludes(ui, 'h.uptimeMs') ||
      !codeIncludes(ui, 'h.minimumFreeHeapBytes') ||
      !codeIncludes(ui, 'h.largestFreeHeapBlockBytes') ||
      !codeIncludes(ui, 'h.heap') ||
      !codeIncludes(ui, 'h.psramSizeBytes') ||
      !codeIncludes(ui, 'h.psramFreeBytes') ||
      !codeIncludes(ui, 'h.psramLargestFreeBlockBytes') ||
      !codeIncludes(ui, "toFixed(1)+' KB'") ||
      !codeIncludes(ui, 'resetReasonCode') ||
      !codeIncludes(ui, "RR[s.resetReasonCode]") ||
      !network.includes('"health.hwmon.') ||
      !network.includes('cpuLoad5s') ||
      !network.includes('cpuLoad1m') ||
      !network.includes('cpuLoad5m') ||
      !network.includes('cpu0Busy') ||
      !network.includes('cpu1Busy') ||
      !network.includes('cpuLoadValid') ||
      !network.includes('cpuMhz') ||
      !codeIncludes(ui, 'cpuLoad5s') ||
      !codeIncludes(ui, 'cpuLoad1m') ||
      !codeIncludes(ui, 'cpuLoad5m') ||
      !codeIncludes(ui, 'cpuLoadValid') ||
      !codeIncludes(ui, 'cpuMhz') ||
      !codeIncludes(ui, "w.cpuMhz+' MHz'") ||
      !codeIncludes(ui, 'w.cpu0Busy') ||
      !codeIncludes(ui, 'w.cpu1Busy') ||
      !codeIncludes(ui, "t+' ('+a.toFixed(2)+' + '+b.toFixed(2)+')'") ||
      !codeIncludes(ui, "split(w.cpuLoad5s,w.cpu0Busy,w.cpu1Busy)") ||
      !codeIncludes(ui, '0–2 (cpu0 + cpu1)') ||
      !network.includes('tempPeakC') ||
      !network.includes('ramTotalBytes') ||
      !network.includes('"health.uptimeMs"') ||
      !network.includes('"health.minimumFreeHeapBytes"') ||
      !network.includes('"health.largestFreeHeapBlockBytes"') ||
      !network.includes('"health.psramSizeBytes"') ||
      !network.includes('"health.psramFreeBytes"') ||
      !network.includes('"health.psramLargestFreeBlockBytes"') ||
      !network.includes('"safety.resetReasonCode"') ||
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
      !diagHtml.includes('<legend>CPU Profiling') ||
      !diagHtml.includes('<legend>RAM</legend>') ||
      !diagHtml.includes('<legend>HEAP</legend>') ||
      !diagHtml.includes('<legend>Scale</legend>') ||
      !diagHtml.includes('<legend>Date and time</legend>') ||
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
      !codeIncludes(ui, "t('dMachine',s.machineState)") ||
      !codeIncludes(ui, "t('hFirmware',s.firmwareVersion)") ||
      !codeIncludes(ui, "t('hBoot',typeof s.bootId==='number'&&s.bootId?'#'+s.bootId:'')") ||
      !codeIncludes(ui, "t('dBrew',formatBackflushState(s)||s.state)") ||
      !codeIncludes(ui, "t('dCup',cp.state)") ||
      !codeIncludes(ui, 'function applyDiagnosticGuards(') ||
      !codeIncludes(ui, 'function exportDebugData(') ||
      !codeIncludes(ui, '/api/v1/debug/export') ||
      !codeIncludes(ui, "t('dActivator',s.physicalActivatorOn?'ON':'OFF'") ||
      !codeIncludes(ui, "t('dReed',s.reedOn?'ON':'OFF')") ||
      !codeIncludes(ui, "t('dStream',sc.streamState)") ||
      !codeIncludes(ui, "t('dControl',sc.controlState)") ||
      !codeIncludes(ui, "t('dScaleRssi',typeof sc.rssi==='number'?sc.rssi+' dBm':'')") ||
      !codeIncludes(ui, "t('hScaleRate',typeof wi==='number'&&wi>0?") ||
      !codeIncludes(ui, "(1000/wi).toFixed(1)+' Hz (≈'+wi+' ms)'") ||
      !codeIncludes(ui, "t('hRecoveredStales',String(sc.recoveredStaleCount))") ||
      !codeIncludes(ui, "t('hStaleTime',typeof sc.recoveredStaleMs==='number'?sc.recoveredStaleMs+' ms':''") ||
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
      !diagHtml.includes('id="ntpStatus"') ||
      !diagHtml.includes('id="ntpServer"') ||
      !diagHtml.includes('id=uo') ||
      !diagHtml.includes('id=ut') ||
      !diagHtml.includes('id=ud') ||
      !diagHtml.includes('id=lt') ||
      !diagHtml.includes('id=ld') ||
      diagHtml.indexOf('id="diagnosticsPanel"') > diagHtml.indexOf('id="logPanel"') ||
      diagHtml.indexOf('<legend>States</legend>') > diagHtml.indexOf('<legend>Machine I/O</legend>') ||
      diagHtml.indexOf('<legend>Machine I/O</legend>') > diagHtml.indexOf('<legend>Scale</legend>') ||
      diagHtml.indexOf('<legend>Scale</legend>') > diagHtml.indexOf('<legend>Guards</legend>') ||
      diagHtml.indexOf('<legend>Guards</legend>') > diagHtml.indexOf('<legend>WiFi</legend>') ||
      diagHtml.indexOf('<legend>WiFi</legend>') > diagHtml.indexOf('<legend>AP</legend>') ||
      diagHtml.indexOf('<legend>AP</legend>') > diagHtml.indexOf('<legend>Serial</legend>') ||
      diagHtml.indexOf('<legend>Serial</legend>') > diagHtml.indexOf('<legend>CPU') ||
      diagHtml.indexOf('<legend>CPU') > diagHtml.indexOf('<legend>CPU Profiling') ||
      diagHtml.indexOf('<legend>CPU Profiling') > diagHtml.indexOf('<legend>RAM</legend>') ||
      diagHtml.indexOf('<legend>RAM</legend>') > diagHtml.indexOf('<legend>HEAP</legend>') ||
      diagHtml.indexOf('<legend>HEAP</legend>') > diagHtml.indexOf('<legend>Date and time</legend>') ||
      diagHtml.indexOf('<legend>Date and time</legend>') > diagHtml.indexOf('<legend>MISC</legend>') ||
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
      diagHtml.indexOf('id="ntpStatus"') > diagHtml.indexOf('id="ntpServer"') ||
      diagHtml.indexOf('id="ntpServer"') > diagHtml.indexOf('id=uo') ||
      diagHtml.indexOf('id=uo') > diagHtml.indexOf('id=ut') ||
      diagHtml.indexOf('id=ut') > diagHtml.indexOf('id=ud') ||
      diagHtml.indexOf('id=ud') > diagHtml.indexOf('id=lt') ||
      diagHtml.indexOf('id=lt') > diagHtml.indexOf('id=ld') ||
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
        'Diagnostics must be a non-collapsible fieldset at the top of Diagnostic, above Log, with States/Machine I/O/Scale/Guards/WiFi/AP/Serial/CPU/CPU Profiling/RAM/HEAP/Date and time/MISC sections and one value per label');
  }
}
{
  const assert = require('assert').strict;
  const first = ui.indexOf('function pad2(');
  const last = ui.indexOf('const HUMAN_WD', first);
  if (first < 0 || last < first) throw new Error('Missing wall-time formatter');
  const {formatWallTime, formatWallTimeLocal} = new Function('__WEBUI_TEXT__',
      ui.slice(first, last) + ';return {formatWallTime,formatWallTimeLocal};')(
      key => ({'runtime.symbol': ':', 'runtime.symbol_2': '-',
        'runtime.symbol_3': ' '}[key] || ''));
  assert.equal(formatWallTimeLocal(1704069000), '2024-01-01 00:30:00');
  assert.equal(formatWallTime(1704069000, -180), '2023-12-31 21:30:00');
  if (!codeIncludes(ui, "t('ut')(utc&&utc.slice(11))") ||
      !codeIncludes(ui, "t('ud')(utc&&utc.slice(0,10))") ||
      !codeIncludes(ui, "t('lt')(local&&local.slice(11))") ||
      !codeIncludes(ui, "t('ld')(local&&local.slice(0,10))") ||
      !codeIncludes(ui, 'function renderDiagClock(') ||
      !codeIncludes(ui, 'statusUtcAnchorSec+Math.floor((performance.now()-statusUtcAnchorAt)/1000)')) {
    throw new Error('Diagnostic UTC and local date/time fields must use the configured offset');
  }
}
{
  const assert = require('assert').strict;
  const source = viewJs.diagnostic;
  const nodes = Object.fromEntries(['hProfileState', 'hProfileElapsed', 'hProfileRecords',
    'hProfileCapacity', 'hProfileRemaining', 'scaleProfileStartButton', 'scaleProfileStopButton',
    'scaleProfileDeleteButton', 'scaleProfileDownloadButton'].map(id => [id, {}]));
  const runtime = {webUiOwner: true, formatUptime: ms => `${ms / 1000}s`};
  const render = new Function('$', 'R', '__WEBUI_TEXT__',
    source.slice(source.indexOf('function applyScaleProfile('),
      source.indexOf('async function downloadScaleProfile(')) + ';return applyScaleProfile;')(
      id => nodes[id], runtime, key => key);
  const profile = {state: 'recording', partitionAvailable: true, persistence: 'none',
    recordCount: 4095, recordCapacity: 8192, recordBytes: 32, reservedRecords: 1,
    elapsedMs: 181000, estimatedRemainingMs: 61100, canStop: true,
    weightCount: 4000, eventCount: 95, lostCount: 0};
  render(profile);
  assert.equal(nodes.hProfileElapsed.textContent, '181s');
  assert.equal(nodes.hProfileCapacity.textContent, '50%');
  assert.equal(nodes.hProfileRemaining.textContent, '≈ 65s');
  assert.equal(nodes.scaleProfileStopButton.disabled, false);
  render({...profile, elapsedMs: 3600000, estimatedRemainingMs: 59300});
  assert.equal(nodes.hProfileCapacity.textContent, '50%');
  assert.equal(nodes.hProfileRemaining.textContent, '≈ 60s');
  for (const estimatedRemainingMs of [null, undefined]) {
    render({...profile, estimatedRemainingMs});
    assert.equal(nodes.hProfileRemaining.textContent, 'Estimating…');
  }
  render({...profile, recordCount: 8189});
  assert.match(nodes.hProfileCapacity.textContent, /^99%/);
  render({...profile, recordCount: 8191, estimatedRemainingMs: 0});
  assert.match(nodes.hProfileCapacity.textContent, /^100%/);
  assert.equal(nodes.hProfileRemaining.textContent, '≈ 0s');
  render({...profile, state: 'stopped', reservedRecords: 0});
  assert.match(nodes.hProfileCapacity.textContent, /^49%/);
  assert.equal(nodes.hProfileRemaining.textContent, '—');
  render({...profile, state: 'saved', recordCount: 4096, reservedRecords: 0});
  assert.match(nodes.hProfileCapacity.textContent, /^50%/);
  render({...profile, state: 'stopped', persistence: 'failed', lastError: 'save_failed', canDownload: true});
  assert.match(nodes.hProfileState.textContent, /Could not save the recording/);
  assert.equal(nodes.scaleProfileDownloadButton.disabled, false);
  render({...profile, partitionAvailable: false, lastError: 'no_partition'});
  assert.equal(nodes.hProfileState.textContent, 'Recording storage unavailable');
  assert.equal(nodes.scaleProfileStopButton.disabled, true);
  render({...profile, lastError: 'none'});
  assert(!nodes.hProfileState.textContent.includes('Could not'));
  runtime.webUiOwner = false;
  render(profile);
  assert.equal(nodes.scaleProfileStopButton.disabled, true);
  render({...profile, state: 'saved', canDownload: true});
  assert.equal(nodes.scaleProfileDownloadButton.disabled, true);
  render(null);
  assert.equal(nodes.hProfileCapacity.textContent, '—');
  assert.equal(nodes.hProfileRemaining.textContent, '—');
  for (const name of ['lastError', 'reservedRecords', 'estimatedRemainingMs'])
    assert.ok(network.includes(`\\"${name}\\"`), `Missing profiler status field ${name}`);
  assert.ok(!source.includes('p.durationLimitMs'), 'Profiler UI must not display a fixed deadline');
}
{
  const source = viewJs.diagnostic;
  const first = source.indexOf('function applyLoopTiming(');
  const last = source.indexOf('function updateCrashRow(', first);
  if (first < 0 || last < first) throw new Error('Missing loop timing renderer');
  const table = {rows: [], replaceChildren() { this.rows = []; }, insertRow() {
    const cells = [];
    this.rows.push(cells);
    return {insertCell() { const cell = {textContent: ''}; cells.push(cell); return cell; }};
  }};
  const maximum = {textContent: '12 ms'};
  const recent = {textContent: '5 ms'};
  const render = new Function('$', '__WEBUI_TEXT__', source.slice(first, last) +
    ';return applyLoopTiming;')(id => ({loopTimingBody: table, hLoopMax: maximum, hLoopGap: recent})[id],
    key => key);
  const status = {health: {loopIntervalGapMs: 5, loopMaxGapMs: 12}, tasks: {
    recentGapMs: 5, peakGapMs: 12, recentGapUs: 5000, peakGapUs: 12000,
    recentDelayUs: 3600, peakDelayUs: 7500,
    recentDispatchUs: 800, peakDispatchUs: 1400,
    rows: [{name: 'loopTask/safety/health',
      recentGapExecutionUs: 180, peakGapExecutionUs: 2630,
      maxExecutionUs: 9999, lastExecutionUs: 9999}]}};
  render(status);
  if (maximum.textContent !== '12 ms' || recent.textContent !== '5 ms' ||
      table.rows[0][1].textContent !== '0.18 ms' ||
      table.rows[0][2].textContent !== '2.63 ms' ||
      table.rows[1][0].textContent !== 'delay call' ||
      table.rows[1][1].textContent !== '3.60 ms' ||
      table.rows[1][2].textContent !== '7.50 ms' ||
      table.rows[2][1].textContent !== '0.80 ms' ||
      table.rows[2][2].textContent !== '1.40 ms' ||
      table.rows[3][1].textContent !== '0.42 ms' ||
      table.rows[3][2].textContent !== '0.47 ms') {
    throw new Error('Loop table must break down the two displayed gap events');
  }
  status.health.loopIntervalGapMs = 8;
  render(status);
  if (recent.textContent !== '5 ms' ||
      table.rows.map(row => row[1].textContent).join(',') !== '0.18 ms,3.60 ms,0.80 ms,0.42 ms') {
    throw new Error('A newer health gap must not hide or relabel the published recent breakdown');
  }
  status.tasks.recentGapMs = 8;
  status.tasks.recentGapUs = 8000;
  render(status);
  if (recent.textContent !== '8 ms' || table.rows[3][1].textContent !== '3.42 ms') {
    throw new Error('The recent total and breakdown must advance together');
  }
  status.health.loopMaxGapMs = 0;
  render(status);
  if (table.rows.some(row => row[2].textContent !== '—')) {
    throw new Error('Loop max reset must still hide the old peak breakdown');
  }
  delete status.tasks.recentGapMs;
  render(status);
  if (recent.textContent !== '—' || table.rows.some(row => row[1].textContent !== '—')) {
    throw new Error('A missing recent sample must remain unavailable');
  }
  delete status.tasks;
  render(status);
  if (recent.textContent !== '—' || table.rows.some(row => row[1].textContent !== '—')) {
    throw new Error('Missing task diagnostics must not throw or invent timing values');
  }
}
{
  const source = viewJs.diagnostic;
  const first = source.indexOf('function updateCrashRow(');
  const last = source.indexOf('async function downloadCrashes(', first);
  if (first < 0 || last < first) throw new Error('Missing coredump row renderer');
  const value = {children: [], replaceChildren() { this.children = []; },
    append(...nodes) { this.children.push(...nodes); }};
  const misc = {append() {}};
  let downloads = 0, empties = 0;
  const build = (busy) => new Function('$', '__WEBUI_TEXT__', 'R', 'crashBusy',
      'downloadCrashes', 'emptyCrashes', 'document',
      source.slice(first, last) + ';return updateCrashRow;')(
    id => id === 'crashArchiveValue' ? value
        : id === 'crashArchiveRow' ? {} : {closest: () => misc},
    key => key,
    {webUiOwner: true}, busy,
    () => { downloads++; }, () => { empties++; },
    {createElement: tag => ({tag, className: '', _text: '',
        get textContent() { return this._text; },
        set textContent(v) { this._text = String(v); }}),
      createTextNode: text => ({text})});
  const render = (status, busy) => { value.replaceChildren(); build(!!busy)(status); return value.children; };
  const links = children => children.filter(n => n.tag === 'a').map(n => n.textContent);
  const admin = {crashCount: 2, crashState: 0, adminUnlocked: true};
  let children = render(admin);
  if (children[0].textContent !== '2' || children[0].className !== 'stateFault' ||
      children.filter(n => n === ' - ').length !== 2 ||
      links(children).join('|') !== 'download|empty') {
    throw new Error('An admin session with crashes must show the red count plus both actions');
  }
  children = render({...admin, adminUnlocked: undefined});
  if (children[0].textContent !== '2' || children[0].className !== 'stateFault' ||
      links(children).length) {
    throw new Error('Without an admin session only the coredump counter may show');
  }
  children = render({crashCount: 2, crashState: 0, development: true});
  if (links(children).join('|') !== 'download|empty') {
    throw new Error('Development builds must keep the coredump actions available');
  }
  children = render({...admin, crashCount: 0});
  if (children[0].textContent !== '0' || children[0].className || links(children).length) {
    throw new Error('An empty archive must show a plain zero without actions');
  }
  children = render({...admin, crashState: 1});
  if (children[0].textContent !== 'unavailable' || children[0].className || links(children).length) {
    throw new Error('Unsupported coredump storage must show neither count tone nor actions');
  }
  children = render(admin, true);
  if (links(children).length) {
    throw new Error('Coredump actions must hide while a download or empty run is busy');
  }
  children = render({...admin, crashState: 2});
  if (children.at(-1).text !== ' (capture needs attention)' ||
      links(children).join('|') !== 'download|empty') {
    throw new Error('A capture error must be reported next to the still-usable actions');
  }
  const [download] = render(admin).filter(n => n.tag === 'a');
  if (download.onclick() !== false || downloads !== 1 || empties !== 0) {
    throw new Error('The Download link must trigger only its own action');
  }
}
if (!codeIncludes(ui, 'id="shotTable"') ||
    !codeIncludes(ui, 'id="exportShotsButton"') ||
    !codeIncludes(ui, 'id="clearShotsButton"') ||
    !html.includes('id="clearShotsButton" class="btnGlyph btnInvert"') ||
    html.includes('id="clearShotsButton" class="btnGlyph btnDanger"') ||
    !css.includes('.glassBar>.btnGlyph.btnInvert') ||
    !codeIncludes(ui, "confirm:'CLEAR_SHOT_LOG'") ||
    !codeIncludes(runtimeJs, 'function setEmptyState(') ||
    !codeIncludes(ui, 'refreshShots()') ||
    !codeIncludes(js, "'shotDur'") ||
    !codeIncludes(js, "'shotActual'") ||
    !css.includes('#shotTable .shotDur,#shotTable .shotActual') ||
    !css.includes('grid-template-areas:"dur dur dur actual actual actual" "time time time time time time" "goal goal avgflow avgflow maxflow maxflow" "err err tare tare drop drop" "ended ended shot shot preset preset" "scale scale rate rate rate rate" "spark spark spark spark spark spark"') ||
    !css.includes('#shotTable tr.noSpark{grid-template-areas:"dur dur dur actual actual actual" "time time time time time time" "goal goal avgflow avgflow maxflow maxflow" "err err tare tare drop drop" "ended ended shot shot preset preset" "scale scale rate rate rate rate"}') ||
    css.includes('grid-area:guard') ||
    css.includes('grid-area:ext') ||
    css.includes('grid-area:stop') ||
    css.includes('grid-area:cut') ||
    !css.includes('#shotTable .shotDel') ||
    !css.includes('#shotTable td.shotDel,#shotTable td.shotDel>.btnGlyph{background:transparent!important}') ||
    !codeIncludes(js, "className='shotDel'") ||
    codeIncludes(runtimeJs, '<span class="t">Delete</span>') ||
    !codeIncludes(ui, 'formatShotTime(r)') ||
    !codeIncludes(runtimeJs, 'function formatShotEnded(') ||
    !codeIncludes(runtimeJs, 'function shotDisplayActualG(') ||
    !codeIncludes(runtimeJs, 'shotDisplayActualG(r.actualG,r.wCg)') ||
    !codeIncludes(runtimeJs, 'shotFrame.card') ||
    !codeIncludes(runtimeJs, 'return y!=null&&y>=1') ||
    !codeIncludes(runtimeJs, 'shotDisplayFlowGS(r)') ||
    !codeIncludes(runtimeJs, 'shotMaxFlowGS(r)') ||
    !codeIncludes(runtimeJs, 'shotPresetName(r)') ||
    !codeIncludes(runtimeJs, "'preset_id','scale_name','max_flow_g_s'") ||
    !network.includes('const bool live = control.activeCycle || control.homePending') ||
    codeIncludes(runtimeJs, 'const live=!!((s.cycle&&s.cycle.active)||s.liveShot)') ||
    codeIncludes(runtimeJs, 'const live=!!((s.cycle&&s.cycle.active)||s.relayClosed)') ||
    !codeIncludes(runtimeJs, 'drop=d.firstDropMs') ||
    !codeIncludes(runtimeJs, 'formatShotEnded(r.stopDetail)') ||
    !codeIncludes(js, "labels=['Time','Dur','Goal','Yield','Err%','Avg flow','Max flow','Tare time','1st drop','Ended','Shot','Preset','Scale'") ||
    codeIncludes(js, "labels=['Time','Dur','Goal','Actual','Err%','Flow','1st drop','Ended','Shot']") ||
    codeIncludes(js, "labels=['Time','Dur','Goal','Actual','Err%','Flow','1st drop','Guard','Ext','Stop','Shot','Cut']") ||
    partialHtml.stats.includes('<th>Guard</th>') ||
    partialHtml.stats.includes('<th>Ext</th>') ||
    partialHtml.stats.includes('<th>Stop</th>') ||
    partialHtml.stats.includes('>Cut</th>') ||
    !partialHtml.stats.includes('<th>Ended</th>') ||
    !partialHtml.stats.includes('<th>Yield</th>') ||
    !partialHtml.stats.includes('<th>Avg flow</th>') ||
    !partialHtml.stats.includes('<th>Max flow</th>') ||
    !partialHtml.stats.includes('<th>Preset</th>') ||
    !partialHtml.stats.includes('<strong>Avg yield</strong>') ||
    !partialHtml.stats.includes('id="shotTableState" class="emptyState" role="status" hidden') ||
    partialHtml.stats.includes('<th>Actual</th>') ||
    !codeIncludes(ui, 'no time') ||
    !codeIncludes(ui, 'id="timezoneId"') ||
    !codeIncludes(js, '/api/v1/time/zones') ||
    codeIncludes(js, 'Request accepted.') ||
    codeIncludes(js, "message('Request queued.','ok')") ||
    codeIncludes(js, 'Request queued successfully.') ||
    !network.includes('hasWallTime') ||
    !network.includes('endedAtLocalSec') ||
    !network.includes('\\"presetName\\":\\"%s\\"') ||
    !network.includes('SHOT_LOG_CLEAR_NOT_CONFIRMED')) {
  throw new Error('Shot history UI/API must expose table, CSV export, clear confirmation, and timezone setting');
}
if (codeIncludes(ui, 'id="shotRating"') ||
    partialHtml.home.includes('<strong>Rate</strong>') ||
    !partialHtml.stats.includes('<th>Rate</th>') ||
    !codeIncludes(runtimeJs, 'function fillStarRate(') ||
    !codeIncludes(runtimeJs, '0 0 24 24') ||
    codeIncludes(runtimeJs, 'star.jpg') ||
    codeIncludes(runtimeJs, 'star.png') ||
    !codeIncludes(runtimeJs, "className='shotRateCell'") ||
    !codeIncludes(js, "dataset.label='Rate'") ||
    !codeIncludes(runtimeJs, 'function postShotRating(') ||
    !codeIncludes(runtimeJs, '{id,rating:n}') ||
    !codeIncludes(runtimeJs, "'rating','ended_at_ms'") ||
    !css.includes('.starRate{display:inline-flex;align-items:center;margin:-.6rem 0 0 -.15rem}') ||
    !css.includes('.starRate button+button{margin-left:-.18rem}') ||
    !css.includes('.starRate button.on{color:var(--ac)}') ||
    css.includes('.shotCard .shotRate') ||
    !css.includes('#shotTable td.shotRateCell{grid-area:rate}') ||
    !network.includes('shotsRateHandler') ||
    !network.includes('LAST_SHOT_NOT_FOUND') ||
    !network.includes('\\"rating\\":%u') ||
    !network.includes('\\"shotId\\":%lu') ||
    !lastShotIo.includes('LAST_SHOT_SCHEMA_VERSION = 1') ||
    !lastShotIo.includes(
        'void advance(const PersistedLastShot &shot,') ||
    !lastShotIo.includes('uint32_t protectionMs = DEFAULT_BBW_PROTECTION_MS') ||
    !firmwareCore.includes('lastShotNvsDirty = false') ||
    !shotLogIo.includes('updateRating') ||
    !shotLogIo.includes('copyRatingById') ||
    !firmwareCore.includes('rateLastShot') ||
    !firmwareCore.includes('rateShotRecord') ||
    firmwareCore.includes('lastShotStore.updateRating')) {
  throw new Error('Shot rating must be SVG stars on last shot and history, persisted on the device');
}
if (!codeIncludes(js, 'function commandOkMessage(') ||
    !codeIncludes(js, 'function commandFailMessage(') ||
    !codeIncludes(js, 'function formatCommandError(') ||
    !codeIncludes(js, 'function homePendingPairs(') ||
    !codeIncludes(js, 'command(path,value={},soft,okMsg,failMsg,busyId)') ||
    codeIncludes(js, "message(e&&e.message?e.message:'Request failed.','error')") ||
    !codeIncludes(js, 'Machine settings saved.') ||
    !codeIncludes(js, "cn('save machine settings')") ||
    !codeIncludes(js, 'Brew settings saved.') ||
    !codeIncludes(js, "'save brew settings'") ||
    !codeIncludes(js, "homeFastExtractionGuardEnabledState','fastExtractionGuardEnabled','Fast extraction guard'") ||
    !codeIncludes(js, "label+(on?' enabled.':' disabled.')") ||
    !codeIncludes(js, "'Could not '+(on?'enable ':'disable ')+label+'.'") ||
    !codeIncludes(js, 'Wi-Fi settings saved. Restarting.') ||
    !codeIncludes(js, 'Wi-Fi sleep saved.') ||
    !codeIncludes(js, "cn('save Wi-Fi settings')") ||
    !codeIncludes(js, 'Wi-Fi scan started.') ||
    !codeIncludes(js, 'Could not start Wi-Fi scan.') ||
    !codeIncludes(js, 'Administration unlocked.') ||
    !codeIncludes(js, 'Could not unlock administration.') ||
    !codeIncludes(js, "R.noteReachOk();R.message('Administration unlocked.','ok')") ||
    !codeIncludes(js, 'Administration locked.') ||
    !codeIncludes(js, 'Could not lock administration.') ||
    !codeIncludes(js, "noteReachOk();message('Administration locked.','ok')") ||
    !codeIncludes(js, 'Shot history cleared.') ||
    !codeIncludes(js, 'Could not clear shot history.') ||
    !codeIncludes(js, 'Could not update Quick Settings.') ||
    codeIncludes(js, 'Shot history cleared successfully.') ||
    codeIncludes(js, 'Unlock failed.') ||
    codeIncludes(js, 'Lock failed.')) {
  throw new Error('Web UI must show action-specific success and failure toasts instead of generic queued/failed copy');
}
{
  const commandFn = runtimeJs.slice(runtimeJs.indexOf('async function command('),
      runtimeJs.indexOf('async function setBleScanIntensity('));
  if (!codeIncludes(commandFn, "path.endsWith('/config')||path.endsWith('/presets')") ||
      !codeIncludes(commandFn, 'configRevision!==previousRevision') ||
      !codeIncludes(commandFn, 'result?.requestId!==accepted.requestId') ||
      compactSource(commandFn).indexOf(compactSource("message(okMsg||")) <
          compactSource(commandFn).indexOf(
              compactSource("throw new Error('Device did not apply the change.')"))) {
    throw new Error('Config and preset saves must confirm their request and revision before showing success');
  }
}
if (!codeIncludes(runtimeJs, 'SHOTS_PAGE_SIZE=10') ||
    !codeIncludes(runtimeJs, 'SHOTS_EXPORT_LIMIT=100') ||
    codeIncludes(runtimeJs, "'/api/v1/stats?offset='") ||
    codeIncludes(runtimeJs, "api('/api/v1/stats')") ||
    !codeIncludes(runtimeJs, 'function startStatsStream(') ||
    !codeIncludes(runtimeJs, 'function ensureStatsStream(') ||
    !codeIncludes(runtimeJs, 'function ensureBackgroundRecordStreams(') ||
    // Navigation must settle a pending cold record entry so the serialized
    // poll gate never waits on an abandoned view's page.
    !codeIncludes(runtimeJs, 'if(activeView!==name){statsResolve?.(false);statsResolve=null;historyResolve?.(false);historyResolve=null;}') ||
    !codeIncludes(runtimeJs, 'function statsStreamFrame(') ||
    !codeIncludes(runtimeJs, 'function applyStatsStream(') ||
    !codeIncludes(runtimeJs, 'if(statsStreamWanted)statsSendSubscribe()') ||
    !codeIncludes(runtimeJs, 'if(statsExportInFlight)return statsExportInFlight') ||
    !codeIncludes(runtimeJs, 'statsRequest=++statsNextRequest') ||
    !codeIncludes(runtimeJs, "sendUiOperation({op:'stats',on:true,fetch:true,request:statsFetchMark.request,offset:statsFetchMark.offset,limit:SHOTS_PAGE_SIZE,sort:shotSort,dir:shotSortDir,}") ||
    !codeIncludes(runtimeJs, "statsFrameWindow(0,SHOTS_EXPORT_LIMIT,'date','desc',90e3)") ||
    !codeIncludes(runtimeJs, 'function shotStatsViewActive(){') ||
    !codeIncludes(runtimeJs, 'function renderShots(') ||
    !codeIncludes(runtimeJs, 'function armListSentinel(') ||
    !codeIncludes(viewJs.stats, 'R.armListSentinel("shotLogSentinel", R.loadMoreShots)') ||
    !partialHtml.stats.includes('id="shotLogSentinel"') ||
    !css.includes('#shotLogSentinel{min-height:1px') ||
    network.includes('parseShotsPageQuery') ||
    !network.includes('sendStatsStream') ||
    !network.includes('formatShotStatsRow') ||
    !network.includes('shotLogPageSlice') ||
    !network.includes('shotLogSortRecords') ||
    !networkHeader.includes('SHOT_LOG_PAGE_DEFAULT') ||
    !networkHeader.includes('shotLogEpoch') ||
    !networkHeader.includes('sendStatsStream') ||
    !network.includes('\\"hasMore\\":%s') ||
    !network.includes('\\"total\\":%u') ||
    !codeIncludes(appJsSource, 'R.startStatsStream()') ||
    !codeIncludes(appJsSource, 'R.ensureBackgroundRecordStreams()') ||
    // Backpressure pacing: the client guard covers a solo maximal row frame
    // (server rows are bounded by the external kJsonItem workspace), and a
    // tab returning from the background resyncs the owned socket instead of
    // tearing it down.
    !codeIncludes(runtimeJs, 'event.data.length>28672') ||
    !codeIncludes(runtimeJs, 'function requestShotResync(') ||
    !codeIncludes(runtimeJs, 'function shotActivity(') ||
    codeIncludes(runtimeJs, 'stopUiStream();startUiStream()')) {
  throw new Error('Shot history must page 10 shots over the owned WebSocket with infinite scroll and a streamed export');
}
const shotLogTypes = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperShotLogTypes.h'), 'utf8');
if (!partialHtml.stats.includes('id="shotSort"') ||
    !partialHtml.stats.includes('class="shotSort"') ||
    !partialHtml.stats.includes('aria-label="Sort history"') ||
    !css.includes('.shotSort{') ||
    !css.includes('.shotSort button[aria-pressed="true"]') ||
    !codeIncludes(runtimeJs, 'function setShotSort(') ||
    !codeIncludes(runtimeJs, 'function toggleShotSortDir(') ||
    !codeIncludes(runtimeJs, 'function syncShotSortButtons(') ||
    !codeIncludes(runtimeJs, 'syncShotSortButtons();statsSendSubscribe()') ||
    !codeIncludes(runtimeJs, "'date','desc'") ||
    !codeIncludes(runtimeJs, "shotSort==='rating'") ||
    !codeIncludes(js, 'Highest rating') ||
    !codeIncludes(js, 'Oldest first') ||
    !codeIncludes(js, 'Lowest rating') ||
    !codeIncludes(viewJs.stats, 'sortDateButton') ||
    !codeIncludes(viewJs.stats, 'sortRatingButton') ||
    !codeIncludes(viewJs.stats, 'sortDirButton') ||
    !codeIncludes(js, 'Newest first') ||
    !codeIncludes(viewJs.stats, "R.setShotSort('date')") ||
    !codeIncludes(viewJs.stats, "R.setShotSort('rating')") ||
    !codeIncludes(viewJs.stats, 'R.toggleShotSortDir()') ||
    !codeIncludes(viewJs.stats, 'R.syncShotSortButtons()') ||
    !shotLogTypes.includes('shotLogSortRecords') ||
    !shotLogTypes.includes('ShotLogSort::Rating') ||
    !network.includes('shotLogSortFromName') ||
    !network.includes('strcmp(sort->valuestring, "date")') ||
    !network.includes('strcmp(dir->valuestring, "asc")') ||
    !css.includes('button{-webkit-appearance:none;appearance:none;border-radius:2rem}') ||
    !css.includes('.glassBar>.btnGlyph{flex:0 0 auto;min-height:2.1rem;min-width:0;padding:0 .8rem;border:0;border-radius:1.6rem;background:transparent;color:var(--ac);white-space:nowrap}') ||
    !css.includes('.shotSort{display:flex;align-items:center;gap:.25rem;max-width:100%}') ||
    !css.includes('.shotSort button{') ||
    !css.includes('.shotSort button{margin:0;border:0;border-radius:1.6rem;background:transparent;color:var(--ac);font:inherit;font-size:.8rem;font-weight:600') ||
    css.includes('.shotSort button{margin:0;border:0;border-right:1px solid var(--ln);background:none') ||
    css.includes('#shotLogPanel .btnGlyph:not(.btnInvert){background:transparent') ||
    css.includes('#message,.configSaveBar,#shotLogPanel .btnBar{background:var(--bg)}') ||
    css.includes('#message,.configSaveBar{background:var(--bg)}') ||
    css.includes('html.theme-dark #message,html.theme-dark .configSaveBar,html.theme-dark #shotLogPanel .btnBar{background:var(--bg)}') ||
    css.includes('html.theme-dark #message,html.theme-dark .configSaveBar{background:var(--bg)}')) {
  throw new Error('Shot history must sort by date or rating with unrated last and keep stats on newest shots');
}
const historyTypesIo = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperHistoryTypes.h'), 'utf8');
const historyIo = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperHistory.h'), 'utf8');
if (!historyTypesIo.includes('BACKFLUSH = 5') ||
    !codeIncludes(runtimeJs, 'HIST_TYPE_SVG.backflush=') ||
    !partialHtml.history.includes('id="hBF" viewBox="0 0 256 256"') ||
    !codeIncludes(runtimeJs, 'Backflush in progress') ||
    !codeIncludes(runtimeJs, 'Backflush unavailable')) {
  throw new Error('Backflush must preserve history ordinals, supplied icon and state-driven guidance');
}
{
  const formatter = blockAt(runtimeJs, 'function formatBackflushState(');
  const format = new Function(formatter + ';return formatBackflushState;')();
  for (const [state, backflush, expected] of [
    ['BACKFLUSH_CANDIDATE', {}, 'Checking backflush'],
    ['BACKFLUSH_RUNNING', {}, 'Backflush in progress'],
    ['REQUIRES_OFF', {stopReason: 'unconfirmed_end'}, 'Other activation'],
    ['REQUIRES_OFF', {stopReason: 'supervision_lost'}, 'Activation interrupted'],
    ['READY', {waiting: true, ready: true}, 'waiting for paddle'],
    ['READY', {waiting: true, ready: false}, 'Backflush unavailable'],
    ['READY', {unresolved: true}, 'Backflush unavailable'],
  ]) {
    if (!format({state, backflush}).includes(expected))
      throw new Error('Incorrect backflush state guidance for ' + state);
  }
  if (format({state: 'READY', backflush: {}}) || format({state: 'BREW'}))
    throw new Error('Ordinary activity must retain its existing presentation');
}
if (!shellHtml.includes('href="/history" data-route="/history"') ||
    !shellHtml.includes('<section id="view-history" class="view" data-view="history"></section>') ||
    !codeIncludes(appJsSource, "'/history':'history'") ||
    !codeIncludes(appJsSource, "SECONDARY=new Set(['stats','history','diagnostic','admin'])") ||
    !codeIncludes(appJsSource, 'R.startHistoryStream()') ||
    codeIncludes(appJsSource, 'historyTimer') ||
    codeIncludes(appJsSource, '/api/v1/status/home') ||
    // Record subscriptions are session-standing: navigation must not stop them.
    codeIncludes(appJsSource, 'R.stopHistoryStream()') ||
    codeIncludes(appJsSource, 'R.stopStatsStream()') ||
    !codeIncludes(viewJs.history, 'R.armListSentinel("historySentinel", R.loadMoreHistory)') ||
    !codeIncludes(viewJs.history, 'R.clearActivationHistory') ||
    !codeIncludes(viewJs.history, 'R.toggleHistoryDir') ||
    !codeIncludes(viewJs.history, 'R.syncHistoryDirButton()') ||
    !partialHtml.history.includes('id="historyPanel"') ||
    !partialHtml.history.includes('id="historyTable"') ||
    !partialHtml.history.includes('id="historyRows"') ||
    !partialHtml.history.includes('id="historyTableState" class="emptyState" role="status" hidden') ||
    !partialHtml.history.includes('id="historySentinel"') ||
    !partialHtml.history.includes('id="historyDirButton"') ||
    !partialHtml.history.includes('class="shotSort"') ||
    !partialHtml.history.includes('id="clearHistoryButton"') ||
    !partialHtml.history.includes('btnGlyph btnInvert') ||
    !css.includes('#historyTable{') ||
    !css.includes('#historySentinel{min-height:1px') ||
    css.includes('.panelState{') ||
    css.includes('.panelState.isOver{') ||
    !css.includes('.histBadge{') ||
    !codeIncludes(runtimeJs, 'HISTORY_PAGE_SIZE=20') ||
    codeIncludes(runtimeJs, "'/api/v1/history?offset='") ||
    !codeIncludes(runtimeJs, 'function startHistoryStream(') ||
    !codeIncludes(runtimeJs, 'function ensureHistoryStream(') ||
    !codeIncludes(runtimeJs, 'function historyStreamFrame(') ||
    !codeIncludes(runtimeJs, 'function applyHistoryStream(') ||
    !codeIncludes(runtimeJs, 'if(diagStreamWanted){if(!diagResolve)armDiagnosticReady();') ||
    !codeIncludes(runtimeJs, 'if(historyStreamWanted)historySendSubscribe()') ||
    !codeIncludes(runtimeJs, 'function applyHistoryPage(') ||
    !codeIncludes(runtimeJs, 'function renderHistory(') ||
    !codeIncludes(runtimeJs, 'function deleteOneHistory(') ||
    !codeIncludes(runtimeJs, 'function clearActivationHistory(') ||
    !codeIncludes(runtimeJs, "power_on:'Power ON'") ||
    !codeIncludes(runtimeJs, "HIST_TYPE_SVG.power_on='⏻'") ||
    !codeIncludes(runtimeJs, 'No scale guard aborted') ||
    !codeIncludes(runtimeJs, 'HIST_TYPE_SVG.no_scale_guard_aborted=') ||
    !partialHtml.history.includes('id="hNS" viewBox="82 82 204 204"') ||
    !historyTypesIo.includes('NO_SCALE_GUARD_ABORTED') ||
    !codeIncludes(runtimeJs, "confirm:'CLEAR_HISTORY'") ||
    !codeIncludes(runtimeJs, "'/api/v1/history/delete'") ||
    !codeIncludes(runtimeJs, "'/api/v1/history/clear'") ||
    !codeIncludes(runtimeJs, 'const toggleHistoryDir=()=>{') ||
    !codeIncludes(runtimeJs, "sendUiOperation({op:'history',on:true,fetch:true,request:historyFetchRequest,offset:historyFetchOffset,}") ||
    codeIncludes(runtimeJs, 'exportShotsCsv') && codeIncludes(runtimeJs, 'historyCsv') ||
    network.includes('parseHistoryPageQuery') ||
    network.includes('historyHandler') ||
    !network.includes('sendHistoryStream') ||
    !networkHeader.includes('copyHistoryPage') ||
    !networkHeader.includes('historyEpoch') ||
    !networkHeader.includes('sendHistoryStream') ||
    !network.includes('historyClearHandler') ||
    !network.includes('historyDeleteHandler') ||
    !networkHeader.includes('HISTORY_PAGE_DEFAULT') ||
    !network.includes('"CLEAR_HISTORY"') ||
    !network.includes('"HISTORY_CLEAR_NOT_CONFIRMED"') ||
    !network.includes('"HISTORY_RECORD_NOT_FOUND"') ||
    !networkHeader.includes('deleteHistoryRecord') ||
    !networkHeader.includes('clearHistoryLog') ||
    !firmwareCore.includes('historyLog.append(record, false)') ||
    !firmwareCore.includes('appendActivationHistory') ||
    !firmwareCore.includes('brewEndIsAbandonedStart(reason)') ||
    !historyTypesIo.includes('historyTypeFromCycle') ||
    !historyTypesIo.includes('HISTORY_CAPACITY = 1000') ||
    !historyTypesIo.includes('durationMs > protectionMs') ||
    !historyIo.includes('HISTORY_FLASH_SLOT_BYTES = 16384') ||
    !historyIo.includes('"history"') ||
    !activationStoresIo.includes('class ActivationStores') ||
    !activationStoresIo.includes('TaskLockGuard(shotStoreMutex)')) {
  throw new Error('Activation history must page 20 records over the owned WebSocket with sort direction, clear, delete, and no CSV export');
}
// The embedded ui object ends with '}}' and no separator of its own, so every
// call site must append the ',' before the next frame member. A missing comma
// once produced invalid JSON on every history snapshot and an unbounded
// client resync loop ("Unable to load view").
const recordUiCallSites = [...network.matchAll(
    /appendRecordPageUi\(control, &used, override\)/g)];
if (recordUiCallSites.length !== 2 ||
    recordUiCallSites.some(
        site => !network.slice(site.index, site.index + 240)
            .includes('statusJsonAppend(&used, ",")'))) {
  throw new Error(
      'Every record-page ui embed must be followed by a member separator');
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
    !statsSection[1].includes('<strong>Avg yield</strong>') ||
    !statsSection[1].includes('<strong>Daily shots</strong>') ||
    !statsSection[1].includes('<strong>Avg BBW error</strong>') ||
    !statsSection[1].includes('<strong>Avg flow</strong>') ||
    !statsSection[1].includes('id="statsAvgDur"') ||
    !statsSection[1].includes('id="statsAvgWeight"') ||
    !statsSection[1].includes('id="statsAvgDaily"') ||
    !statsSection[1].includes('id="statsAvgErr"') ||
    !statsSection[1].includes('id="statsAvgFlow"') ||
    !statsSection[1].includes('class="fieldHint"') ||
    !statsSection[1].includes('Last 10 shots. BBW: 10 target cuts.') ||
    !statsSection[1].includes('id="statsDurChart"') ||
    !codeIncludes(runtimeJs, 'function renderStatsDurChart(') ||
    !codeIncludes(viewJs.stats, 'R.renderStatsDurChart()') ||
    !codeIncludes(runtimeJs, 'statsDurChartPlot') ||
    !codeIncludes(runtimeJs, 'shotSparkHost') ||
    !codeIncludes(runtimeJs, 'fill-opacity') ||
    !codeIncludes(runtimeJs, 'statsDurSparkY') ||
    !codeIncludes(runtimeJs, 'renderStatsDurChart()') ||
    !codeIncludes(runtimeJs, 'const BIN=0.5,tMax=6e4/1e3,tLow=28,tHigh=32') ||
    !codeIncludes(js, "fillChartTicks(host.lastChild,[[0,'0 s'],[tLow,L(tLow,'s')],[tHigh,L(tHigh,'s')],[tMax,L(tMax,'s')],],tMax") ||
    !css.includes('#statsDurChart{margin-top:') ||
    !css.includes('.shotCurve .shotSparkHost,#statsDurChartPlot{display:grid;') ||
    !codeIncludes(runtimeJs, 'function renderShotStats(){') ||
    !codeIncludes(runtimeJs, 's.avgDurationS') ||
    !codeIncludes(runtimeJs, 'shotStats=d.stats') ||
    !codeIncludes(runtimeJs, 'renderShotStats();') ||
    codeIncludes(runtimeJs, 'slice(0,20)') ||
    codeIncludes(runtimeJs, "shotsUrl(0,SHOTS_PAGE_SIZE,'date','desc')") ||
    !css.includes('grid-template-areas:"dur dur dur actual actual actual" "goal goal err err avgflow avgflow"') ||
    css.includes('.shotCard:has(') ||
    css.includes('#shotStatsPanel') ||
    css.includes('#statsAvgDur') ||
    css.includes('statsAvgDaily') ||
    network.includes('shotLogComputeAverages') ||
    network.includes('\\"avgDailyShots\\"') ||
    network.includes('/api/v1/stats/stats') ||
    shotLogTypes.includes('shotLogComputeAverages') ||
    !shotLogTypes.includes('SHOT_LOG_STATS_WINDOW') ||
    shotLogTypes.includes('updateShotLogStats') ||
    !shotLogTypes.includes('shotLogBbwErrorEligible')) {
  throw new Error(
      'Stats view must be a 2+3 shotCard, duration histogram, and firmware-computed stats aggregate (no client recompute or second fetch)');
}

{
  if (!statsSection[1].includes('class="panelWrap"') ||
      statsSection[1].includes('id="shotStatsState"') ||
      !css.includes('.panelWrap{position:relative}') ||
      css.includes('.panelState.isOver{') ||
      codeIncludes(runtimeJs, 'settlePanel(')) {
    throw new Error('Stats must use page loading without section waves');
  }
  const assert = require('assert').strict, vm = require('vm');
  (async () => {
    // Stats rides the owned socket: split pages assemble by rowBase until
    // more:false, snapshot frames hydrate UI state before rows render,
    // one-shot fetches append, the CSV export window resolves without
    // touching view state, and invalid frames fail closed.
    {
      const events = [], sent = [];
      const ui = {firmwareVersion: '2.0', configMutable: true, webUiOverrideActive: false,
        compatibilityMode: false, timeUtcSec: 1790809185, lastCommand: {requestId: 7, state: 'PERSISTED'},
        config: {revision: 8, timezoneId: 'America/Santiago', appliedTimezoneOffsetMinutes: -180,
          timezoneAutomatic: true, timezoneInitialized: true}};
      const row = id => ({id, bootId: 9, endedAtMs: 30000 + id, hasWallTime: true,
        endedAtLocalSec: 1790794385, endedAtUnixSec: 1790809185,
        timezoneOffsetMinutesAtCommit: -180, durationS: 28.5,
        wCg: [100, 200], wAtMs: [1000, 2000], wBreakBefore: [], rating: 0});
      const context = vm.createContext({activeView: 'stats',
        viewReady: Promise.resolve(),
        shotHistory: {bootId: 0, total: 2, hasMore: true, shots: [{id: 2}, {id: 1}]}, shotsLoaded: false,
        shotStatsViewActive: () => true, webUiPollingActive: () => true,
        shotWs: {readyState: 1, send: op => sent.push(JSON.parse(op))},
        sendUiOperation: op => sent.push(JSON.parse(JSON.stringify(op))),
        statusPageOk: (page, state) => !!state, shotSort: 'date', shotSortDir: 'desc',
        SHOTS_PAGE_SIZE: 10, SHOTS_EXPORT_LIMIT: 100,
        setTimeout: () => 0, clearTimeout() {},
        developmentMode: false, compatMode: false, controlsMutable: false, firmwareVersion: '', bootId: 0,
        lastCommandStatus: null, configRevision: 0, dateTimeDirty: false, statusUtcAnchorSec: 0,
        statusUtcAnchorAt: 0, statusTimezoneOffsetMinutes: 0, performance: {now: () => 123},
        setMutable: value => {context.controlsMutable = value;}, $: () => null,
        syncTimezone: () => {}, checkFirmwareReload: () => {}, applyMachineTypeUi() {}, applyDiagnosticNavigation() {},
        applyCommonStatus: () => events.push('state'),
        applyShotPage: (data, mode) => {context.shotsLoaded = true; events.push('records:' + mode);},
        renderShots: () => events.push('render:' + context.controlsMutable),
        updateHeaderSignals() {}, updateFirmwareFooter() {}, noteReachOk() {},
        __WEBUI_TEXT__: key => key});
      const stream = rawRuntimeJs.slice(rawRuntimeJs.search(/let\s+statsStreamWanted\s*=/),
          rawRuntimeJs.search(/document\.addEventListener\(\s*['"]visibilitychange['"]/)) + '\n' +
          blockAt(rawRuntimeJs, 'const loadMoreShots') +
          '\nthis.loadMoreShots=loadMoreShots;';
      vm.runInContext(stream, context);
      const loading = context.startStatsStream();
      await Promise.resolve();
      assert.deepEqual(sent.at(-1),
          {op: 'stats', on: true, request: 1, offset: 0, limit: 10, sort: 'date', dir: 'desc'},
          'Entering the view subscribes with the standing window');
      const frame = extras => Object.assign({v: 1, type: 'stats', boot: 9, snapshot: true,
        epoch: 5, request: 1, seq: 1, ui, stats: {avgDurationS: 28}, total: 2, offset: 0,
        limit: 10, hasMore: false, rows: [row(2)], rowBase: 0, more: true}, extras);
      context.applyStatsStream(context.statsStreamFrame(frame()));
      assert.deepEqual(events, ['state'], 'A split page applies nothing until more:false');
      context.applyStatsStream(context.statsStreamFrame(
          frame({snapshot: false, rows: [row(1)], rowBase: 1, more: false})));
      assert.deepEqual(events, ['state', 'records:replace', 'render:false'],
          'The completed page applies UI state before rows');
      assert.equal(await loading, true, 'The view load settles on the first completed replace page');
      events.length = 0;
      context.applyStatsStream(context.statsStreamFrame(
          frame({snapshot: false, epoch: 6, seq: 2, rows: [row(2), row(1)], more: false})));
      assert.deepEqual(events, ['records:replace', 'render:false'],
          'An epoch push replaces stale rows from older pages');
      events.length = 0;
      context.loadMoreShots();
      assert.deepEqual(sent.at(-1),
          {op: 'stats', on: true, fetch: true, request: 2, offset: 2, limit: 10, sort: 'date', dir: 'desc'},
          'The sentinel asks for the next window without disturbing the subscription');
      context.applyStatsStream(context.statsStreamFrame(
          frame({snapshot: false, epoch: 6, request: 2, seq: 3, offset: 2, rows: [], more: false})));
      assert.deepEqual(events, ['records:append', 'render:false'],
          'One-shot fetches append the requested window');
      events.length = 0;
      const exported = context.statsFrameWindow(0, 100, 'date', 'desc', 50);
      assert.deepEqual(sent.at(-1),
          {op: 'stats', on: true, fetch: true, request: 3, offset: 0, limit: 100, sort: 'date', dir: 'desc'},
          'The export asks for its own date/desc window');
      context.applyStatsStream(context.statsStreamFrame(
          frame({snapshot: false, epoch: 8, request: 3, seq: 4, limit: 100, hasMore: true, rows: [row(1)], more: false})));
      assert.deepEqual(events, [], 'The export window must not touch view state');
      assert.equal((await exported).length, 1, 'The export resolves with its assembled rows');
      await Promise.resolve();
      context.loadMoreShots();
      const pendingRequest = sent.at(-1).request;
      context.applyStatsStream(context.statsStreamFrame(frame({snapshot:false,epoch:9,
        request:pendingRequest,seq:5,offset:2,rows:[],more:false})));
      assert.equal(sent.at(-1).fetch, undefined, 'a changed epoch must restart the standing window');
      assert(sent.at(-1).request > pendingRequest, 'replacement replies need a new request identity');
      const interrupted = context.statsFrameWindow(0,100,'date','desc',50);
      const exportRequest = sent.at(-1).request;
      context.loadMoreShots();
      assert.equal(sent.at(-1).request,exportRequest,'scrolling must not preempt export');
      context.statsSendSubscribe();
      assert.equal(await interrupted,null,'sorting settles an interrupted export');
      const standingRequest = sent.at(-1).request;
      context.loadMoreShots();
      assert(vm.runInContext('statsFetchMark',context));
      context.applyStatsStream(context.statsStreamFrame(frame({snapshot:false,epoch:10,
        request:standingRequest,seq:6,rows:[row(2),row(1)],more:false})));
      assert.equal(vm.runInContext('statsFetchMark',context),null,
          'A standing replacement invalidates scroll offsets from the previous list');
      context.shotStatsViewActive = () => false;
      events.length = 0;
      context.applyStatsStream(context.statsStreamFrame(
          frame({snapshot: false, epoch: 10, seq: 7, hasMore: true, rows: [row(2)], more: false})));
      assert.deepEqual(events, ['records:replace'],
          'Frames on another view refresh the cached rows without rendering them');
      context.shotStatsViewActive = () => true;
      for (const bad of [frame({snapshot: false, boot: 10, seq: 6, rows: [row(2)], more: false}),
        frame({snapshot: false, seq: 1, rows: [row(2)], more: false}),
        frame({snapshot: false, epoch: 9, seq: 7, rows: [row(2)], rowBase: 1, more: false}),
        frame({snapshot: false, epoch: 9, seq: 8, rows: [{...row(3), wCg: [1.5]}], more: false}),
        frame({snapshot: false, epoch: 9, seq: 9, rows: [row(2), row(1)], hasMore: true, more: false}),
        frame({epoch: 9, seq: 10, ui: null, rows: [row(2)], more: false})]) {
        assert.throws(() => context.statsStreamFrame(bad),
            'invalid stats frames must fail closed');
      }
    }
    // History rides the owned socket: snapshot frames hydrate UI state and
    // replace the page, one-shot fetch frames append, epoch pushes replace, and
    // invalid frames fail closed.
    {
      const events = [], resolved = [];
      const ui = {firmwareVersion: '2.0', configMutable: true, webUiOverrideActive: false,
        compatibilityMode: false, timeUtcSec: 1790809185, lastCommand: {requestId: 7, state: 'PERSISTED'},
        config: {revision: 8, timezoneId: 'America/Santiago', appliedTimezoneOffsetMinutes: -180,
          timezoneAutomatic: true, timezoneInitialized: true}};
      const context = vm.createContext({activeView: 'history', viewSeq: 1,
        historyLoaded: false, historyData: {bootId: 0, total: 0, hasMore: false, records: []},
        historyFetchOffset: -1, historyStreamBoot: 0, historyStreamEpoch: 0, historyFetchRequest: 2,
        historyResolve: value => resolved.push(value), historyViewActive: () => true,
        statusPageOk: (page, state) => !!state,
        developmentMode: false, compatMode: false, controlsMutable: false, firmwareVersion: '', bootId: 0,
        lastCommandStatus: null, configRevision: 0, dateTimeDirty: false, statusUtcAnchorSec: 0,
        statusUtcAnchorAt: 0, statusTimezoneOffsetMinutes: 0, performance: {now: () => 123},
        setMutable: value => {context.controlsMutable = value;}, $: () => null,
        syncTimezone: config => events.push('timezone:' + config.timezoneId),
        checkFirmwareReload: () => {}, applyMachineTypeUi() {}, applyDiagnosticNavigation() {},
        applyCommonStatus: () => events.push('state'),
        applyHistoryPage: (data, mode) => {context.historyLoaded = true; events.push('records:' + mode);},
        renderHistory: () => events.push('render:' + context.controlsMutable),
        updateHeaderSignals() {}, updateFirmwareFooter() {}, noteReachOk() {},
        __WEBUI_TEXT__: key => key});
      const stream = rawRuntimeJs.slice(rawRuntimeJs.indexOf('function historyStreamFrame('),
          rawRuntimeJs.search(/document\.addEventListener\(\s*['"]visibilitychange['"]/));
      vm.runInContext(stream, context);
      const frame = extras => Object.assign({v: 1, type: 'history', boot: 9, snapshot: true,
        epoch: 5, request: 1, ui, total: 2, offset: 0, limit: 20, hasMore: false,
        records: [
          {id: 2, type: 'shot', durationS: 28.5, hasWallTime: true,
           endedAtUnixSec: 1790809185, endedAtLocalSec: 1790794385},
          {id: 1, type: 'rinse', durationS: 4, hasWallTime: false,
           endedAtUnixSec: 0, endedAtLocalSec: 0}]}, extras);
      context.applyHistoryStream(context.historyStreamFrame(frame()));
      assert.deepEqual(events, ['state', 'records:replace', 'render:false'],
          'A snapshot must hydrate UI state before rendering rows');
      assert.deepEqual(resolved, [true], 'The view load settles on the first replace frame');
      events.length = 0;
      context.applyHistoryStream(context.historyStreamFrame(frame({snapshot: false})));
      assert.deepEqual(events, ['records:replace', 'render:false'],
          'A standing update cannot retain deleted or reordered tail rows');
      events.length = 0;
      context.historyFetchOffset = 2;
      context.applyHistoryStream(context.historyStreamFrame(frame({snapshot: false,
        request: 2, offset: 2, hasMore: false, records: []})));
      assert.equal(context.historyFetchOffset, -1, 'A served fetch clears its in-flight marker');
      assert.deepEqual(events, ['records:append', 'render:false']);
      events.length = 0;
      context.historyViewActive = () => false;
      context.applyHistoryStream(context.historyStreamFrame(frame({snapshot: false})));
      assert.deepEqual(events, ['records:replace'],
          'Frames on another view refresh the cached records without rendering them');
      context.historyViewActive = () => true;
      for (const bad of [frame({snapshot: false, boot: 10}),
        frame({snapshot: false, hasMore: true}),
        frame({snapshot: false, offset: 1, records: [
          {id: 3, type: 'shot', durationS: 1, hasWeight: true, hasWallTime: false,
           endedAtUnixSec: 1, endedAtLocalSec: 1},
          {id: 4, type: 'shot', durationS: 1, hasWeight: true, hasWallTime: false,
           endedAtUnixSec: 2, endedAtLocalSec: 2}]}),
        frame({records: [{id: 0, type: 'shot', durationS: 1, hasWeight: true,
          hasWallTime: false, endedAtUnixSec: 1, endedAtLocalSec: 1}]}),
        frame({records: [{id: 5, type: 'shot', durationS: Infinity, hasWeight: true,
          hasWallTime: false, endedAtUnixSec: 1, endedAtLocalSec: 1}]}),
        frame({ui: null})]) {
        assert.throws(() => context.historyStreamFrame(bad),
            'invalid history frames must fail closed');
      }
    }
    const state = network.slice(network.indexOf('bool ShotStopperNetwork::appendRecordPageUi'),
        network.indexOf('esp_err_t ShotStopperNetwork::shotsClearHandler'));
    assert.ok(network.includes('requestPendingNetworkConfirm()'),
        'Accepted socket ops keep confirming pending station settings');
    assert.ok(state.includes('webUiOverrideAllowed') || state.includes('override'),
        'The record UI state reflects the override flag');
    assert.ok(state.includes('controlAllowsConfiguration(control)'));
    for (const unused of ['copyPresetBank', 'copyHomeShot', 'shotCurve', 'cupPresence', 'preferredMac']) {
      assert.equal(state.includes(unused), false, 'Record UI state must omit Home data: ' + unused);
    }
  })().catch(error => {console.error(error); process.exitCode = 1;});
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
  global.shotStats = {durationsS: [30.2, 30.2, 27.1, 40]};
  global.fillChartTicks = () => {};
  helpers.renderStatsDurChart();
  const filled = (histPlot.innerHTML.match(/fill-opacity=".22"/g) || []).length;
  if (filled !== 3) {
    throw new Error('Duration histogram must render every eligible shot type');
  }
  if (!histPlot.innerHTML.includes('statsDurSparkY">2<')) {
    throw new Error('Duration histogram Y axis must scale to the max bin count');
  }
  global.shotStats = {};
  helpers.renderStatsDurChart();
  if ((histPlot.innerHTML.match(/fill-opacity=".22"/g) || []).length !== 0 ||
      !histPlot.innerHTML.includes('statsDurSparkY">0<')) {
    throw new Error(
        'Duration histogram must paint an empty shell before shots load');
  }
  delete global.$;
  delete global.shotStats;
  delete global.fillChartTicks;
}

if (!codeIncludes(ui, 'id="firmwareFooter"') ||
    !codeIncludes(ui, 'id="inactiveFirmware"') ||
    !codeIncludes(ui, 'firmwareVersion') ||
    !codeIncludes(ui, 'updateFirmwareFooter()') ||
    !codeIncludes(ui, "const inactive=$('inactiveFirmware')") ||
    !css.includes('body.homeAdminActions #view-home:not(.hidden)~.pageFooter{margin-bottom:calc(7rem + env(safe-area-inset-bottom))}') ||
    css.includes('body.homeAdminActions #view-home:not(.hidden)~.pageFooter{display:none}') ||
    !css.includes('#actionsPanel{position:fixed;left:0;right:0') ||
    !css.includes('@media(min-width:700px){#actionsPanel{left:1rem;right:1rem') ||
    !network.includes('delta.field("firmwareVersion"') ||
    !network.includes('\\"bootId\\":%lu') ||
    !network.includes('FW_VERSION')) {
  throw new Error('Firmware version must remain exposed in status API, page footers, and Diagnostic');
}
if (!shellHtml.includes('https://github.com/Cheerpipe/AcaiaArduinoBLE') ||
    !shellHtml.includes('https://github.com/Cheerpipe') ||
    !shellHtml.includes('Hecho por') ||
    !shellHtml.includes('>Cheerpipe</a>') ||
    !shellHtml.includes('class="pageFooter"') ||
    shellHtml.indexOf('class="pageFooter"') < shellHtml.indexOf('id="app"')) {
  throw new Error('Web UI page footer must credit the GitHub repo and Cheerpipe');
}
if (!shellHtml.includes('id="message"') ||
    !shellHtml.includes('id="messageText"') ||
    !shellHtml.includes('id="messageClose"') ||
    !codeIncludes(runtimeJs, 'function clearMessage(') ||
    !codeIncludes(runtimeJs, 'b.onclick=clearMessage') ||
    !codeIncludes(runtimeJs, "kind==='ok'?5e3") ||
    !codeIncludes(runtimeJs, "kind==='warn'&&!e.querySelector('button:not(#messageClose)')?15e3") ||
    !codeIncludes(runtimeJs, 'setTimeout(clearMessage,ms)') ||
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
    !codeIncludes(ui, 'startLogStream()') ||
    !codeIncludes(ui, 'function logStreamFrame(') ||
    !(codeIncludes(ui, "name==='diagnostic'") || codeIncludes(ui, "name === 'diagnostic'")) ||
    !codeIncludes(ui, 'id="view-diagnostic"') ||
    !codeIncludes(ui, 'data-route="/diagnostic"') ||
    !html.includes('<span>Diagnostic</span></a>') ||
    !codeIncludes(ui, 'id="logLevelFilter"') ||
    !codeIncludes(ui, 'e.level') ||
    !codeIncludes(ui, 'value="boot"') ||
    html.indexOf('id="ringRetainLogLevel"') < html.indexOf('id="view-diagnostic"') ||
    html.indexOf('id="ringRetainLogLevel"') >
        html.indexOf('</section>', html.indexOf('id="view-diagnostic"')) ||
    html.indexOf('id="serialLogLevel"') < html.indexOf('id="view-diagnostic"') ||
    html.indexOf('id="serialLogLevel"') >
        html.indexOf('</section>', html.indexOf('id="view-diagnostic"')) ||
    html.indexOf('id="ringRetainLogLevel"') > html.indexOf('id="serialLogLevel"') ||
    !html.includes('id="serialLogLevel" class="diagCtl"') ||
    !html.includes('id="ringRetainLogLevel" class="diagCtl"') ||
    !codeIncludes(runtimeJs, "e.classList.contains('diagCtl')") ||
    html.includes('id="navLogWrap"') ||
    codeIncludes(ui, 'function ringLogEnabled(') ||
    codeIncludes(ui, 'function updateLogNavVisibility(') ||
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
    !codeIncludes(ui, 'logBootId') ||
    !codeIncludes(ui, 'Missed while disconnected') ||
    !codeIncludes(ui, 'm.cursorInvalid')) {
  throw new Error('Diagnostic log must distinguish history rotation from unread and serial loss');
}
if (!codeIncludes(ui, 'id="factoryResetButton"') ||
    !codeIncludes(ui, "confirm('Restore all factory settings?") ||
    !codeIncludes(ui, "confirm:'ERASE_ALL_SETTINGS'") ||
    !network.includes('FACTORY_RESET_NOT_CONFIRMED') ||
    !network.includes('resetAllDurableStores(next)') ||
    !network.includes('ensureFactoryResetIntent(') ||
    !network.includes('releaseNvsSpaceForFactoryReset') ||
    !codeIncludes(ui, 'id="restartPanel"') ||
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
    !codeIncludes(js, 'const nv=s.nvs||{}') ||
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
    !css.includes('#shotLogPanel .glassBar,#historyPanel .glassBar{') ||
    !css.includes('#shotLogPanel .glassBar,#historyPanel .glassBar{position:sticky;top:var(--hdr);z-index:6;margin:0 0 .75rem') ||
    css.includes('#shotLogPanel .btnBar{position:sticky;top:var(--hdr);z-index:6;background:var(--sf);margin:0 0 .65rem;border:1px solid var(--ln);border-radius:var(--r);overflow:hidden}')) {
  throw new Error('Action buttons must be separate with a gap; btnDanger must not share invert fill');
}
if (html.includes('id="debugPanel"') ||
    html.includes('id="view-debug"') ||
    html.includes('data-route="/debug"') ||
    codeIncludes(ui, 'function debugBuzzer(') ||
    codeIncludes(ui, 'function debugBookoo(') ||
    codeIncludes(ui, '/api/v1/control/buzzer') ||
    codeIncludes(ui, '/api/v1/control/bookoo') ||
    network.includes('buzzerHandler') ||
    network.includes('bookooHandler') ||
    network.includes('/api/v1/status/debug') ||
    network.includes('"/debug"') ||
    network.includes('StatusPage::Debug')) {
  throw new Error('Debug tab/API must be removed from Web UI and network handlers');
}

{
  // Cup-minimum setup guidance: a live platform weight between 0.5 g and
  // the configured minimum must surface the hint with both placeholders
  // substituted; everything else keeps it hidden without crashing.
  const assert = require('assert');
  let minimumCupWeightG = 10;
  const hint = {hidden: true, textContent: ''};
  const guidance = new Function('$', 'R',
    blockAt(viewJs.settings, 'function updateCupMinGuidance(') +
    ';return updateCupMinGuidance;')(
    id => id === 'cupMinGuidance' ? hint : null,
    {number: () => minimumCupWeightG});
  guidance({cupSetup: {observedG: 4.21}});
  assert(!hint.hidden && hint.textContent.includes('4.2') &&
      hint.textContent.includes('10.0') &&
      !hint.textContent.includes('{x}') && !hint.textContent.includes('{n}'));
  for (const status of [{}, {cupSetup: {}}, {cupSetup: {observedG: null}},
    {cupSetup: {observedG: 0.4}}, {cupSetup: {observedG: 12}}]) {
    guidance(status);
    assert(hint.hidden, 'Guidance must hide for absent, tiny and over-minimum cups');
  }
  minimumCupWeightG = Number.NaN;
  guidance({cupSetup: {observedG: 4.2}});
  assert(hint.hidden, 'An unparseable minimum must keep the guidance hidden');
  assert(settingsHtml.includes('id="cupMinGuidance" class="fieldHint" hidden'));
}
