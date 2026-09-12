if (!statusSection || !statusSection[1].includes('class="statusColumn"') ||
    statusSection[1].includes('class="row"') ||
    (statusSection[1].match(/class="metric"/g) || []).length !== 2 ||
    !statusSection[1].includes('<strong>Machine</strong>') ||
    !statusSection[1].includes('<strong>Brew</strong>') ||
    statusSection[1].includes('<strong>Cup</strong>') ||
    statusSection[1].includes('data-label="Machine"') ||
    !statusSection[1].includes('id="machineState"') ||
    !statusSection[1].includes('id="state"') ||
    statusSection[1].includes('id="cupState"') ||
    statusSection[1].includes('id="paddle"') ||
    statusSection[1].includes('id="relay"') ||
    statusSection[1].includes('id="safety"') ||
    statusSection[1].includes('id="statusExtractionGuard"') ||
    !scaleSection || !scaleSection[1].includes('class="statusColumn"') ||
    (scaleSection[1].match(/class="metric"/g) || []).length !== 4 ||
    !scaleSection[1].includes('<strong>Status</strong>') ||
    !scaleSection[1].includes('<strong>Preferred</strong>') ||
    !scaleSection[1].includes('<strong>Weight</strong>') ||
    !scaleSection[1].includes('<strong>Timer</strong>') ||
    scaleSection[1].includes('data-label="Status"') ||
    !scaleSection[1].includes('id="scale"') ||
    !scaleSection[1].includes('id="preferredScale"') ||
    !scaleSection[1].includes('id="scaleWeight"') ||
    !scaleSection[1].includes('id="scaleTimer"') ||
    !ui.includes('s.physicalActivatorOn?') ||
    !ui.includes('s.relayClosed?') || !ui.includes('ON') || !ui.includes('OFF') ||
    !ui.includes('function formatScaleWeight(') ||
    !ui.includes('function formatScaleStatus(') ||
    !ui.includes('function formatScaleTimer(') ||
    !ui.includes('function formatMachineState(') ||
    !ui.includes('CONFIRMED_OFF:') || !ui.includes('Idle') ||
    !ui.includes('ASSUMED_ON:') || !ui.includes('Assumed on') ||
    !ui.includes('CONFIRMED_ON:') || !ui.includes('Confirmed on') ||
    !ui.includes('ASSUMED_OFF:') || !ui.includes('Assumed off') ||
    !ui.includes('function formatCupState(') ||
    !ui.includes('lastDisconnectReasonName') ||
    !ui.includes('Stale') || !ui.includes('No sample') ||
    !ui.includes('formatScaleStatus(s)') ||
    !ui.includes('id="preferredScale"') ||
    !ui.includes('id="preferredScaleSelect"') ||
    !ui.includes('id="preferredScalePauseHint"') ||
    !ui.includes('id="preferredScaleBootstrapHint"') ||
    !ui.includes('id="scalePreference"') ||
    !ui.includes('id="forgetPairedScale"') ||
    !ui.includes('Scale preference') ||
    !ui.includes('First available') ||
    !ui.includes('First detected') ||
    !ui.includes('Prefer selected') ||
    !ui.includes('Preferred only') ||
    !ui.includes('Preferred scale') ||
    !ui.includes('Clear preferred') ||
    !ui.includes('scaleMacCacheMode') ||
    !ui.includes('/api/v1/scale/preferred/clear') ||
    !ui.includes('/api/v1/scale/preferred/select') ||
    !ui.includes('function formatPreferredScale(') ||
    !ui.includes('function updatePreferredScaleSelect(') ||
    !ui.includes('function updateScalePreferenceOptions(') ||
    !ui.includes("if(!preferred){const first=document.createElement('option')") ||
    !ui.includes("first.textContent=mode==='only'||mode==='prefer'?") ||
    !ui.includes('empty.textContent=bootstrap?') ||
    !ui.includes('First detected') || !ui.includes('No preferred') ||
    ui.includes("msg:'Select a preferred scale first.'") ||
    !ui.includes('<option value="only" selected>Preferred only</option>') ||
    ui.includes('<option value="first" selected>First available</option>') ||
    ui.includes("if(!canPrefer&&(sel.value==='prefer'||sel.value==='only'))") ||
    ui.includes("o.disabled=!canPrefer||!controlsMutable") ||
    // Regression: missing ';' after `prev` concatenated into
    // `prevupdateScalePreferenceOptions` and broke Settings status refresh.
    ui.includes(':prevupdateScalePreferenceOptions') ||
    !ui.includes(':prev;updateScalePreferenceOptions()') ||
    !ui.includes('preferredScaleSelectSyncing') ||
    !ui.includes("mac===(sel.dataset.applied||'')") ||
    !ui.includes("scaleMacCacheMode:(()=>{const el=$('scalePreference')") ||
    !ui.includes("const v=el?el.value:'only';return['first','prefer','only'].includes(v)?v:'only'") ||
    ui.includes("const v=el?el.value:'first';return['first','prefer','only'].includes(v)?v:'first'") ||
    !ui.includes("el.id==='preferredScaleSelect'") ||
    ui.includes('id="alwaysUseThisScale"') ||
    ui.includes('Always use this scale') ||
    !ui.includes('function selectPreferredScale(') ||
    !ui.includes('function forgetPairedScale(') ||
    !ui.includes('Saved scale history is kept') ||
    !ui.includes('formatPreferredScale(s)') ||
    !ui.includes('macCachePauseRemainingMs>0') ||
    ui.includes('id="preferredScaleSettings"') ||
    ui.includes('id="scaleMacCacheMode"') ||
    ui.includes('id="clearPreferredScale"') ||
    ui.includes('id="scaleMacCacheFullWarn"') ||
    ui.includes('Use scale MAC cache') ||
    ui.includes('Paired scale') ||
    ui.includes('Forget this scale') ||
    !network.includes('preferredScaleClearHandler') ||
    !network.includes('preferredScaleSelectHandler') ||
    !network.includes('/api/v1/scale/preferred/clear') ||
    !network.includes('/api/v1/scale/preferred/select') ||
    !network.includes('Scale preference must be first, prefer, or only.') ||
    network.includes('Always use this scale must be on or off.') ||
    network.includes('scaleMacCacheMode must be disabled or full.') ||
    !network.includes('scaleMacCacheMode must be first, prefer, or only.') ||
    firmwareCore.includes('scaleMacCacheModeRequiresPreferred(candidate.scaleMacCacheMode)') ||
    scaleWorker.includes('coerceScalePreferenceModeToFirst') ||
    !scaleWorker.includes('First detected scale adopted: %s — %s') ||
    !network.includes('The paired scale cannot be forgotten while a cycle') ||
    !network.includes('\\"history\\"') ||
    network.includes('Preferred scale cache cannot be cleared') ||
    !network.includes('\\"timerMs\\"')) {
  throw new Error('Home Status must show Machine/Brew/Cup and a Scale panel with one value per label');
}
if (!ui.includes('id="shotPanel"') ||
    !ui.includes('id="shotBar"') ||
    !ui.includes('id="shotBarFast"') ||
    !ui.includes('id="shotBarTicks"') ||
    !partialHtml.home.includes('id="shotBarTicks"') ||
    !partialHtml.home.includes('<legend>Last/Current shot</legend>') ||
    !partialHtml.home.includes('class="ruleChartLabel">Weight (g)</div>') ||
    partialHtml.home.includes('id="shotIdle"') ||
    css.includes('content:"Weight (g)"') ||
    css.includes('#shotIdle') ||
    ui.includes('shotMark') ||
    !ui.includes('Math.max(goal,wt)') ||
    !css.includes('.shotTrack{position:relative;height:1rem;background:var(--ln);border-radius:.5rem;overflow:hidden}') ||
    !css.includes('.shotTrack #shotBarFast{background:#d97706}') ||
    css.includes('.shotMark') ||
    css.includes('max-width:150%') ||
    !ui.includes('id="shotCard"') ||
    !html.includes('id="shotSparkHost"') ||
    !css.includes('.shotSparkHost') ||
    !css.includes('.shotSpark{') ||
    !css.includes('.shotSparkY{') ||
    !css.includes('.shotSparkHost .ruleChartTicks') ||
    !css.includes('.hidden,[hidden]{display:none!important}') ||
    !css.includes('#shotPanel .shotSparkHost{min-height:4.05rem;margin:.55rem 0 .1rem') ||
    !css.includes('#shotPanel{position:relative}') ||
    css.includes('#shotPanel{position:relative;padding-right:3.4rem') ||
    !css.includes('#shotPanel .shotDel{top:-.55rem;right:.15rem') ||
    !css.includes('#shotTable tr.noSpark{') ||
    !css.includes('.shotSpark{grid-area:plot;display:block;width:100%;height:100%;color:var(--ok);overflow:visible}') ||
    !ui.includes('function renderShotSpark(') ||
    !runtimeJs.includes('function buildShotSparkModel(') ||
    !runtimeJs.includes('function axisLabel(') ||
    !runtimeJs.includes('function fillChartTicks(') ||
    runtimeJs.includes('s[a=') ||
    !runtimeJs.includes('style.left=') ||
    runtimeJs.includes("style=\"left:") ||
    !runtimeJs.includes('function shotDisplayFlowGS(') ||
    !runtimeJs.includes('lastCurveWeightG(w)===null') ||
    !runtimeJs.includes('model.firstDropS>0&&dur>0') ||
    !runtimeJs.includes("'1st '+L(") ||
    !runtimeJs.includes('fillChartTicks($(\'shotBarTicks\')') ||
    !runtimeJs.includes('raw.sort(') ||
    runtimeJs.includes('shotIdle') ||
    runtimeJs.includes("last?'Last shot.'") ||
    !runtimeJs.includes('stroke="\'+cN+\'"') ||
    runtimeJs.includes('stroke="currentColor"') ||
    !runtimeJs.includes("if(spark.hidden)row.classList.add('noSpark')") ||
    !css.includes('.shotCard{') ||
    !css.includes('.metric,.shotCard > *{') ||
    !css.includes('.metric strong,.shotCard strong{') ||
    !css.includes('.metric > div,.shotCard > * > div,.swS{') ||
    css.includes('#diagnosticsPanel .metric,#statusPanel .metric,#scalePanel .metric,.shotCard > *{') ||
    css.includes('#statusPanel .metric::before,#scalePanel .metric::before,.shotCard > *::before{') ||
    css.includes('font-size:1rem;font-weight:700;color:var(--mu)') ||
    !css.includes('.shotCard .shotDur > div,.shotCard .shotActual > div') ||
    !css.includes('grid-template-areas:"dur dur dur actual actual actual" "goal goal flow flow drop drop" "err err shot shot ended ended" "rate rate rate rate rate rate"') ||
    !ui.includes('id="shotElapsed"') ||
    !ui.includes('id="shotFirstDrop"') ||
    !ui.includes('id="shotCurrentWeight"') ||
    !partialHtml.home.includes('<strong>Weight</strong>') ||
    !partialHtml.home.includes('<strong>Dur</strong>') ||
    partialHtml.home.includes('data-label=') ||
    html.includes('data-label="Actual"') ||
    !ui.includes('id="shotGoalWeight"') ||
    !ui.includes('id="shotErr"') ||
    !ui.includes('id="shotFlow"') ||
    !ui.includes('id="shotEnded"') ||
    !ui.includes('id="shotType"') ||
    ui.includes('id="shotRetare"') ||
    ui.includes('id="shotScale"') ||
    ui.includes('id="shotGuard"') ||
    ui.includes('id="shotPct"') ||
    ui.includes('class="shotHero"') ||
    !ui.includes('function updateShot(') ||
    !network.includes('firstDropElapsedMs') ||
    !network.includes('retarePerformed') ||
    !network.includes('shotType') ||
    !network.includes('scaleProtocol') ||
    !network.includes('safeScaleProtocol') ||
    !ui.includes('remoteReady&&relayStartReady&&canControl') ||
    ui.includes('Remote machine control disabled by policy') ||
    !network.includes('\\"remoteControlEnabled\\"') ||
    !network.includes('\\"lastCommand\\"') ||
    !network.includes('\\"maintenance\\"') ||
    !network.includes('\\"persistPending\\"') ||
    !network.includes('\\"persistFailed\\"') ||
    !ui.includes('persistFailed') ||
    !ui.includes('Saving...') ||
    !network.includes('\\"cycle\\"') ||
    !network.includes('extractionExtended') ||
    !ui.includes('updateShot(s)')) {
  throw new Error('Web UI must enforce remote policy, maintenance, durable command state, and live shot status');
}
if (!ui.includes('id="autoToManualGuardEnabled"') ||
    !ui.includes('id="autoToManualGuardLimitMode"') ||
    !ui.includes('id="autoToManualGuardBaselineS"') ||
    !ui.includes('id="scaleTimerStopExtraDelayMs"') ||
    !html.includes('Waits this extra time before stopping the scale') ||
    html.includes('Added after measured scale start lag') ||
    html.includes('Added after the scale timer catches up to circuit whole seconds') ||
    !ui.includes('id="dripDelayS" type="number" min="0" max="10" step="0.1"') ||
    !ui.includes('id="autoToManualGuardManualLimitS"') ||
    !ui.includes('id="autoToManualGuardTrendS"') ||
    !ui.includes('id="resetGuardSamplesButton"') ||
    html.indexOf('id="autoToManualGuardLimitMode"') >
        html.indexOf('id="autoToManualGuardManualLimitS"') ||
    html.indexOf('id="autoToManualGuardManualLimitS"') >
        html.indexOf('id="autoToManualGuardTrendS"') ||
    html.indexOf('id="autoToManualGuardTrendS"') >
        html.indexOf('id="autoToManualGuardBaselineS"') ||
    html.indexOf('id="autoToManualGuardBaselineS"') >
        html.indexOf('id="resetGuardSamplesButton"') ||
    !ui.includes('Reset A→M samples to baseline') ||
    !ui.includes('id="homeAtmSub"') ||
    !ui.includes('id="homeNoScaleSub"') ||
    !ui.includes('id="noScaleBbwMode"') ||
    !ui.includes('id="lastShotCooldownMin"') ||
    !ui.includes('When BBW has no scale') ||
    !ui.includes('Protection returns after') ||
    !ui.includes('id="noScaleBbwMode"') ||
    !ui.includes('value="warn_once"') ||
    !ui.includes('value="require_scale"') ||
    !ui.includes('function formatNoScaleGuard(') ||
    !ui.includes('function formatSlowExtractionGuard(') ||
    !ui.includes("setHomeSub('homeSlowSub',formatSlowExtractionGuard(") ||
    !ui.includes('function updateStatusGuards(') ||
    ui.includes('function updateNoScaleGuard(') ||
    html.includes('id="shotAtmGuard"') ||
    html.includes('id="shotNoScaleGuard"') ||
    html.includes('id="statusExtractionGuard"') ||
    html.includes('id="statusSlowExtractionGuard"') ||
    html.includes('id="statusAtmGuard"') ||
    html.includes('id="statusNoScaleGuard"') ||
    html.indexOf('<legend>Machine and scale</legend>') >
        html.indexOf('<summary>Paddle</summary>') ||
    html.indexOf('<summary>Paddle</summary>') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('<summary>No-scale BBW</summary>') >
        html.indexOf('<summary>Quick rinse</summary>') ||
    html.indexOf('id="noScaleBbwMode"') <
        html.indexOf('<legend>Machine and scale</legend>') ||
    html.indexOf('id="noScaleBbwMode"') >
        html.indexOf('id="lastShotCooldownMin"') ||
    html.indexOf('id="lastShotCooldownMin"') <
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="lastShotCooldownMin"') >
        html.indexOf('<summary>Quick rinse</summary>') ||
    html.indexOf('id="paddleMode"') <
        html.indexOf('<summary>Paddle</summary>') ||
    html.indexOf('id="paddleMode"') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="noScaleBbwMode"') <
        html.indexOf('id="saveBrewPresetButton"') ||
    !network.includes('avoidBbwShotWithoutScale') ||
    !network.includes('noScaleBbwMode') ||
    !network.includes('warn_once') ||
    !network.includes('require_scale') ||
    !network.includes('not both') ||
    !network.includes('lastShotCooldownMs') ||
    !network.includes('serialDebugOutput') ||
    !network.includes('ringRetainLogLevel') ||
    !network.includes('noScaleShotGuard') ||
    !network.includes('noScaleShotGuardEnabled') ||
    !network.includes('noScaleShotGuardArmed') ||
    !network.includes('cupPresence') ||
    !network.includes('control.cupPresent') ||
    !network.includes('machineState') ||
    !network.includes('machineRunStateName') ||
    !network.includes('cupPresenceStateName') ||
    !firmware.includes('last.noScaleShotGuardEnabled') ||
    !firmware.includes('last.noScaleShotGuardArmed') ||
    !firmware.includes('next.cupPresent') ||
    !firmware.includes('next.machineRunState') ||
    !firmware.includes('next.cupPresenceState') ||
    !firmware.includes('cupPresenceState() == CupPresenceState::PRESENT') ||
    !domain.includes('bool cupPresent = false') ||
    !domain.includes('MachineRunState machineRunState') ||
    !domain.includes('CupPresenceState cupPresenceState') ||
    !ui.includes('A→M ·') ||
    !ui.includes('function updateHomeGuardSubs(') ||
    !ui.includes('updateHomeGuardSubs(s,live)') ||
    !ui.includes("setHomeSub('homeBbwSub'") ||
    !ui.includes("setHomeSub('homeCupSub'") ||
    ui.includes("setHomeSub('homeAlertsSub'") ||
    !ui.includes('function formatCupProtection(') ||
    ui.includes('function formatAlertsChannel(') ||
    !/Can\\?'t brew — no cup/.test(ui) ||
    !ui.includes('Shot aborted') ||
    !ui.includes('Brew allowed') ||
    !ui.includes("c.state==='PRESENT'||c.present?") ||
    !ui.includes('Present') || !ui.includes('Absent') ||
    !network.includes('shotLogStopDetailName(') ||
    !html.includes('option value="scale_priority">Scale priority') ||
    !css.includes('.swS') ||
    !css.includes('.homeSwitchGrid .swS') ||
    !css.includes('.homeGuardGrid{') ||
    !css.includes('grid-template-columns:repeat(2,minmax(0,1fr))') ||
    !ui.includes('actual_weight_source') ||
    !network.includes('autoToManualGuardEnabled') ||
    !network.includes('autoToManualGuardBaselineMs') ||
    !network.includes('scaleTimerStopExtraDelayMs') ||
    !network.includes('dripDelayMs') ||
    !network.includes('autoToManualGuardTrendMs') ||
    !network.includes('autoToManualGuardEnforced') ||
    !network.includes('autoToManualGuardArmed') ||
    !network.includes('actualWeightSource') ||
    !network.includes('reset-guard-samples') ||
    !network.includes('AUTO_TO_MANUAL_GUARD') ||
    !network.includes('UNCONFIRMED_START') ||
    !network.includes('cupProtectionEnabled') ||
    !network.includes('stopIfCupRemoved') ||
    !network.includes('requireCupToStart') ||
    !network.includes('cupRemovedWeightG') ||
    !ui.includes('cupProtectionEnabled:$(\'cupProtectionEnabled\')') ||
    !ui.includes('stopIfCupRemoved:$(\'stopIfCupRemoved\')') ||
    !ui.includes('requireCupToStart:$(\'requireCupToStart\')') ||
    !ui.includes("cupRemovedWeightG:number('cupRemovedWeightG')")) {
  throw new Error('Auto-to-manual time guard must be wired in config UI, live panel, shots API, and routes');
}
if (!network.includes('copyShotRecords(&homeLastShot, 1)') ||
    !network.includes('newestCurve.shotId == homeLastShot.id') ||
    !network.includes('static_cast<unsigned long>(homeLastShot.id)') ||
    !ui.includes('rateLastShotValue(ls.shotLogId,n)') ||
    !ui.includes("deleteOneShot(+$('clearLastShotButton').dataset.shotId)")) {
  throw new Error('Home last shot must read and manage the newest Stats record');
}
const paddleHelp = html.slice(html.indexOf('<summary>Paddle</summary>'),
    html.indexOf('</details>', html.indexOf('<summary>Paddle</summary>')));
if (!html.includes('<summary>Paddle</summary>') ||
    !html.includes('id="paddleMode"') ||
    !html.includes('<option value="auto">Auto</option>') ||
    !html.includes('<option value="natural">Natural</option>') ||
    !html.includes('<option value="original">Original</option>') ||
    html.indexOf('<option value="auto">Auto</option>') >
        html.indexOf('<option value="natural">Natural</option>') ||
    html.indexOf('<option value="natural">Natural</option>') >
        html.indexOf('<option value="original">Original</option>') ||
    !paddleHelp.includes('<strong>Natural:</strong>') ||
    !paddleHelp.includes('<strong>Original:</strong>') ||
    !paddleHelp.includes('<strong>Auto:</strong>') ||
    paddleHelp.indexOf('<strong>Natural:</strong>') >
        paddleHelp.indexOf('<strong>Original:</strong>') ||
    paddleHelp.indexOf('<strong>Original:</strong>') >
        paddleHelp.indexOf('<strong>Auto:</strong>') ||
    !html.includes('ON starts and OFF stops') ||
    !html.includes('promotes that shot to Natural') ||
    !html.includes('Until the first OFF, automatic stopping waits') ||
    !html.includes('OFF after the rinse window demotes the current shot to Original-style hands-off automation') ||
    !html.includes('Neither transition stops early') ||
    !html.includes('early ON→OFF demotes the tentative shot to a rinse') ||
    !html.includes('Without weight control, OFF stops normally') ||
    !ui.includes("paddleMode:['auto','natural','original']") ||
    !ui.includes("if($('paddleMode'))$('paddleMode').value=") ||
    !network.includes('"paddleMode"') ||
    !network.includes('paddleMode must be auto, natural or original.') ||
    !network.includes('jsonPaddleMode') ||
    !firmware.includes('machineApplyWorkflowConfig') ||
    !firmwareCore.includes('machineApplyWorkflowConfig(candidate, command.config)') ||
    !firmware.includes('dst.paddleMode = src.paddleMode') ||
    firmwareCore.includes('candidate.paddleMode') ||
    !firmware.includes('machineHidesPhysicalStop()') ||
    !firmware.includes('machineAllowsAutomationStop()') ||
    !firmware.includes('machineBeginCycle') ||
    domain.includes('enum class PaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('enum class PaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('NATURAL = 0') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('ORIGINAL = 1') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('AUTO = 2') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('enum class PaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('parsePaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('enum class MachineType') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddlePolicy.h'), 'utf8')
         .includes('machinePollIntention()') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8')
        .includes('enum class PaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('PaddleMode::') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('paddleMode')) {
  throw new Error('Machine Paddle mode must expose Auto/Natural/Original in UI, API, and APPLY_CONFIG');
}
const brewHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8');
const scaleSenseHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleSense.h'), 'utf8');
const paddlePolicyHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachinePaddlePolicy.h'), 'utf8');
const machineTypesHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8');
const momentaryInputHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryInput.h'), 'utf8');
const momentaryConfigHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryConfig.h'), 'utf8');
const momentaryControlHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryControl.h'), 'utf8');
const momentaryOnlyHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryOnlyState.h'), 'utf8');
const relayHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineRelay.h'), 'utf8');
if (brewHeader.includes('machinePollIntention') ||
    brewHeader.includes('getScaleLinkSnapshot') ||
    brewHeader.includes('cupPresenceState(') ||
    brewHeader.includes('scaleAvailable(')) {
  throw new Error('Brew/guards must consume GuardInputs from the stopper — not poll machine/scale/cup');
}
if (scaleSenseHeader.includes('onFirstDropsDetected') ||
    scaleSenseHeader.includes('notifyCupPresenceTare') ||
    scaleSenseHeader.includes('holdCupPresenceTransitions')) {
  throw new Error('Scale sense must return events; the stopper applies brew/cup effects');
}
if (momentaryInputHeader.includes('session.active') ||
    momentaryConfigHeader.includes('session.active') ||
    momentaryControlHeader.includes('session.active') ||
    momentaryOnlyHeader.includes('session.active') ||
    momentaryOnlyHeader.includes('currentWeight') ||
    relayHeader.includes('session.automaticEnabled')) {
  throw new Error('Machine momentary/relay must not read session or live scale globals');
}
if (firmwareCore.includes('readRawActivatorOn()') ||
    firmwareCore.includes('pinMode(RELAY_GPIO') ||
    firmwareCore.includes('#if SHOT_STOPPER_MACHINE_TYPE') ||
    firmwareCore.includes('MACHINE_USES_MOMENTARY') ||
    firmwareCore.includes('stopPulseTenMs') ||
    firmwareCore.includes('maxSinglePressHundredMs') ||
    !firmware.includes('pinMode(RELAY_GPIO, OUTPUT)') ||
    !firmware.includes('machineBootActivatorHeldStably()') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineActivatorSample.h'), 'utf8')
         .includes('bool rawActivatorOn') ||
    firmwareCore.includes('bool rawActivatorOn')) {
  throw new Error(
      'Machine GPIO, activator sample BSS, and boot debounce must live in machine headers — not shotStopper.cpp');
}
if (!html.includes('<summary>Switch</summary>') ||
    !html.includes('id="stopPulseMs"') ||
    !html.includes('id="maxSinglePressMs"') ||
    !html.includes('id="momentaryStartEdge"') ||
    !html.includes('id="reedConfirmTimeoutS"') ||
    !html.includes('id="assumeIdleWhenScaleConnects"') ||
    !html.includes('id="shotReactTimeoutS"') ||
    !html.includes('class="reedOnly"') ||
    !html.includes('class="switchOnly"') ||
    !html.includes('class="cfgGroup momentaryOnly"') ||
    !html.includes('Auto-stop pulse') ||
    !html.includes('Single-press limit') ||
    !html.includes('Start/stop on') ||
    !html.includes('Reed confirm timeout') ||
    !html.includes('Button press') ||
    !html.includes('Button release') ||
    !html.includes('<strong>Button press:</strong>') ||
    !html.includes('<strong>Button release:</strong>') ||
    !html.includes('shot state start or stop when the button is pressed') ||
    !html.includes('demotes the tentative shot to a rinse') ||
    !html.includes('only after a short release') ||
    !html.includes('starts a rinse directly, never a shot') ||
    !html.includes('machine sensor to confirm that water started or stopped') ||
    !html.includes('the displayed state follows the sensor') ||
    !html.includes('Longer holds remain manual') ||
    html.includes('<summary>Momentary</summary>') ||
    html.indexOf('<summary>Paddle</summary>') >
        html.indexOf('<summary>Switch</summary>') ||
    html.indexOf('<summary>Switch</summary>') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="stopPulseMs"') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    !html.includes(
        'How long Shot Stopper holds the machine button when stopping automatically') ||
    !ui.includes('stopPulseMs:number(') ||
    !ui.includes("if($('stopPulseMs'))$('stopPulseMs').value=") ||
    !ui.includes('Auto-stop pulse') ||
    !ui.includes('Single-press limit') ||
    !ui.includes('Reed confirm timeout') ||
    !ui.includes('momentaryStartEdge:') ||
    !ui.includes('reedConfirmTimeoutMs:') ||
    !ui.includes('assumeIdleWhenScaleConnects:') ||
    !ui.includes('shotReactTimeoutS:') ||
    !ui.includes('id="overrideIdleLink"') ||
    !ui.includes('id="overrideBrewingLink"') ||
    !ui.includes('id="machineStateValue"') ||
    !ui.includes('/api/v1/control/state-override') ||
    !ui.includes('updateHomeAdminActions') ||
    !ui.includes('function updateHomeAdminActions(unlocked,remoteEnabled){const panel=$(') ||
    !ui.includes('show=!!unlocked&&!!remoteEnabled') ||
    !ui.includes('syncAdminSessionUi(admin,remoteReady)') ||
    ui.includes('id="overrideIdleButton"') ||
    ui.includes('id="overrideBrewingButton"') ||
    ui.includes("d.classList.contains('momentaryMachine')&&!d.classList.contains('reedMachine')") ||
    !html.includes('Assume idle when the scale connects') ||
    !html.includes('marks the machine as idle without pressing its button') ||
    !html.includes('Shot reaction timeout') ||
    !html.includes(
        'If the scale shows no coffee for this long after Start') ||
    !html.includes('Override idle') ||
    !html.includes('Override brewing') ||
    !css.includes('html.reedMachine .switchOnly') ||
    !css.includes('html:not(.momentaryMachine) .switchOnly') ||
    !css.includes('#machineStateValue .switchOnly') ||
    !css.includes('#machineStateValue .switchOnly,#machineStateValue a{font-size:.78rem;font-weight:400}') ||
    !css.includes('.momentaryOnly') ||
    !css.includes('html:not(.reedMachine) .reedOnly') ||
    !network.includes('"stopPulseMs"') ||
    !network.includes('"momentaryStartEdge"') ||
    !network.includes('"reedConfirmTimeoutMs"') ||
    !network.includes('stopPulseMs must be an integer from 50 to 1000.') ||
    !network.includes('maxSinglePressMs must be an integer from 100 to 5000.') ||
    !network.includes('momentaryStartEdge must be press or release.') ||
    !network.includes(
        'reedConfirmTimeoutMs must be an integer from 200 to 5000.') ||
    !network.includes(
        'Shot reaction timeout must be 0 (compiled default) or from 3 to 30 s.') ||
    !network.includes(
        'shotReactTimeoutS must be 0 (compiled default) or an integer from 3 to 30.') ||
    !network.includes('jsonStopPulseMs') ||
    !network.includes('jsonMomentaryStartEdge') ||
    !network.includes('jsonReedConfirmTimeoutMs') ||
    !network.includes('jsonAssumeIdleWhenScaleConnects') ||
    !network.includes('jsonShotReactTimeoutS') ||
    !network.includes('stateOverrideHandler') ||
    !network.includes('/api/v1/control/state-override') ||
    !firmware.includes('machineApplyWorkflowConfig') ||
    !firmware.includes('dst.stopPulseTenMs = src.stopPulseTenMs') ||
    !momentaryControlHeader.includes(
        'dst.momentaryStartOnPress = src.momentaryStartOnPress') ||
    !momentaryControlHeader.includes(
        'dst.reedConfirmTimeoutHundredMs = src.reedConfirmTimeoutHundredMs') ||
    !momentaryControlHeader.includes(
        'dst.assumeIdleWhenScaleConnects = src.assumeIdleWhenScaleConnects') ||
    !momentaryControlHeader.includes(
        'dst.shotReactTimeoutS = src.shotReactTimeoutS') ||
    paddlePolicyHeader.includes('momentaryStartOnPress') ||
    paddlePolicyHeader.includes('reedConfirmTimeoutHundredMs') ||
    paddlePolicyHeader.includes('assumeIdleWhenScaleConnects') ||
    paddlePolicyHeader.includes('shotReactTimeoutS') ||
    machineTypesHeader.includes('parseMomentaryStartEdge') ||
    machineTypesHeader.includes('momentaryStartEdgeId') ||
    machineTypesHeader.includes('PaddleMode') ||
    machineTypesHeader.includes('parsePaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('parsePaddleMode') ||
    !momentaryConfigHeader.includes('parseMomentaryStartEdge') ||
    !momentaryConfigHeader.includes('momentaryStartEdgeId') ||
    !domain.includes('uint8_t stopPulseTenMs') ||
    !domain.includes('bool momentaryStartOnPress') ||
    !domain.includes('setRuntimeStopPulseMs') ||
    !domain.includes('setRuntimeReedConfirmTimeoutMs') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8')
        .includes('stopPulseTenMs') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('stopPulseTenMs')) {
  throw new Error(
      'Momentary Switch timings must be wired in Settings, API, APPLY_CONFIG, and runtime config — not brew');
}
{
  const assert = require('assert'), vm = require('vm'), classes = new Set(['hidden']);
  const toggle = (name, on) => on ? classes.add(name) : classes.delete(name);
  const context = vm.createContext({$: () => ({classList: {toggle}}),
    document: {body: {classList: {toggle: () => {}}}}});
  vm.runInContext(runtimeJs.split('\n').find(line => line.startsWith('function updateHomeAdminActions(')), context);
  const visible = (admin, remote) => { vm.runInContext(`updateHomeAdminActions(${admin},${remote})`, context); return !classes.has('hidden'); };
  assert(!visible(false, false) && !visible(true, false) && !visible(false, true) && visible(true, true));
}
if (!html.includes('class="cfgGroup paddleOnly"><summary>Paddle</summary>') ||
    !html.includes('class="cfgGroup"><summary>Quick rinse</summary>') ||
    html.includes('class="cfgGroup paddleOnly"><summary>Quick rinse</summary>') ||
    !html.includes('id="rinseEnabled" type="checkbox"') ||
    html.indexOf('id="rinseEnabled"') >
        html.indexOf('id="rinseGestureS"') ||
    html.indexOf('<summary>Quick rinse</summary>') >
        html.indexOf('id="rinseEnabled"') ||
    !html.includes('id="rinseEnabled" type="checkbox"> Quick rinse') ||
    !html.includes('Lets you flush the group with a short paddle flip') ||
    !html.includes('Lets you flush the group with a long press from idle') ||
    !html.includes('How long to hold the switch from idle before a rinse starts') ||
    !html.includes('How long water runs after a Quick rinse begins') ||
    !html.includes('a long press is left to the machine') ||
    !html.includes('id="rinseButton" class="btnGlyph" title="Start rinse"') ||
    html.includes('id="rinseButton" class="btnGlyph paddleOnly"') ||
    !ui.includes('s.config.rinseEnabled===true') ||
    !ui.includes('rinseEnabled:$(\'rinseEnabled\').checked') ||
    !network.includes('"rinseEnabled"') ||
    !network.includes('rinseEnabled must be a boolean.') ||
    !firmwareCore.includes('candidate.rinseEnabled = command.config.rinseEnabled') ||
    !firmware.includes('uint32_t rinseBegin(') ||
    !firmware.includes('UserIntent::REQUEST_RINSE') ||
    !firmware.includes('machineBeginRinse') ||
    !html.includes('class="cfgGroup momentaryOnly"><summary>Switch</summary>') ||
    !html.includes('class="paddleOnly"><input id="paddleReturnReminderBeep"') ||
    html.includes('cfgGroup paddleOnly momentaryOnly') ||
    html.includes('cfgGroup momentaryOnly paddleOnly') ||
    !css.includes('html.momentaryMachine .paddleOnly') ||
    !css.includes('html:not(.momentaryMachine) .momentaryOnly') ||
    !css.includes('html:not(.reedMachine) .reedOnly') ||
    !ui.includes("classList.toggle('momentaryMachine',t!=='paddle')") ||
    !ui.includes("classList.toggle('reedMachine',t==='momentary_reed')")) {
  throw new Error(
      'Paddle and Momentary Settings groups must be mutually exclusive by compiled machine type; Quick rinse is shared');
}
const rinseHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperRinse.h'), 'utf8');
const brewTypesHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8');
if (rinseHeader.includes('session.') ||
    rinseHeader.includes('runtimeConfig') ||
    brewHeader.includes('runtimeConfig.rinseGestureMs') ||
    brewTypesHeader.includes('DEFAULT_RINSE_GESTURE_MS') ||
    brewTypesHeader.includes('DEFAULT_RINSE_DURATION_MS') ||
    brewTypesHeader.includes('ENTER_RINSE =') ||
    firmware.includes('machineRinseGestureMs') ||
    domainCore.includes('snapshot.rinseGestureMs') ||
    !firmwareCore.includes('session.rinseStartedAtMs = rinseBegin') ||
    !firmwareCore.includes(
        'blockedHoldTimeoutMs = runtimeConfig.rinseGestureMs') ||
    !brewHeader.includes('inputs.blockedHoldTimeoutMs')) {
  throw new Error(
      'Rinse clock must not write session; brew must not read rinseGestureMs; ShotStopper copies the accept anchor');
}
if (!ui.includes('id="learnedOffsetG"') ||
    !ui.includes('id="weightOffsetBaselineG"') ||
    !ui.includes('id="resetCalibrationButton"') ||
    html.indexOf('id="learnedOffsetG"') >
        html.indexOf('id="weightOffsetBaselineG"') ||
    html.indexOf('id="weightOffsetBaselineG"') >
        html.indexOf('id="resetCalibrationButton"') ||
    !ui.includes('Reset learned stop offset') ||
    ui.includes('Reset learned stop offset to baseline') ||
    !css.includes('.bbwLearning .btnBar{max-width:22rem}') ||
    css.includes('#bbwAlgorithm{width:100%}') ||
    !ui.includes('Save changed baseline values before resetting') ||
    !network.includes('weightOffsetBaselineG') ||
    !ui.includes('weightOffsetBaselineG')) {
  throw new Error('Learned stop offset baseline must be wired like A→M baseline reset');
}
if (!html.includes('<summary>Cup</summary>') ||
    !html.includes('<summary>Tare</summary>') ||
    html.indexOf('<summary>Cup</summary>') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="minimumCupWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="minimumCupWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="retareStabilitySamples"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="retareStabilitySamples"') >
        html.indexOf('<summary>Tare</summary>') ||
    !html.includes('<summary>Scales</summary>') ||
    html.includes('<summary>Scale & retare</summary>') ||
    html.indexOf('<summary>Tare</summary>') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('<summary>Scales</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    html.indexOf('id="autoTare"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="postTareBaselineGraceS"') <
        html.indexOf('id="autoTare"') ||
    html.indexOf('id="postTareBaselineGraceS"') >
        html.indexOf('id="autoRetare"') ||
    html.indexOf('id="autoRetare"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareWindowS"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="minimumCupWeightG"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilitySamples"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityToleranceG"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityMaxGapS"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityMinDurationS"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="scaleTimerStopExtraDelayMs"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="dripDelayS"') <
        html.indexOf('id="scaleTimerStopExtraDelayMs"') ||
    html.indexOf('id="scalePreference"') <
        html.indexOf('id="dripDelayS"') ||
    html.indexOf('id="scalePreference"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="preferredScaleSelect"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="forgetPairedScale"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('<summary>Bookoo</summary>') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="canTareStartTimer"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="canTareStartTimer"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('id="bookooMuteOnBuzzerOnly"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="bookooMuteOnBuzzerOnly"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('id="bookooConnectBeepLevel"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="bookooConnectBeepLevel"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<strong>Requires shot-start tare.</strong>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<strong>Requires shot-start tare.</strong>') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('Applies when <strong>Buzzer only</strong> is selected.') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('Applies when <strong>Buzzer only</strong> is selected.') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<strong>Scale only or Scale priority</strong>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<strong>Scale only or Scale priority</strong>') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<summary>Acaia</summary>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<summary>Felicita</summary>') <
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<summary>Felicita</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    !ui.includes("d.parentElement.closest('details')")) {
  throw new Error('Machine settings must split Tare and Scales, with Bookoo/Acaia/Felicita subgroups');
}
if (!html.includes('id="postTareBaselineGraceS" type="number" min="0.5" max="10" step="0.1"') ||
    !ui.includes("rangeCheck('postTareBaselineGraceS',0.5,10,'Post-tare grace',{unit:'s'})") ||
    !ui.includes("postTareBaselineGraceMs:sToMs('postTareBaselineGraceS')") ||
    !ui.includes("'postTareBaselineGrace'") ||
    !ui.includes("$(k+'S').value=String(c[k+'Ms']/1000)") ||
    !ui.includes("apply('tareOpt',!$('autoTare').checked)") ||
    !(ui.includes("$('autoTare').onchange=()=>{updateConfigGroups();markConfigDirty()}") ||
      ui.includes("$('autoTare').onchange=()=>{R.updateConfigGroups();R.markConfigDirty()}")) ||
    !ui.includes("typeof c.postTareBaselineGraceMs==='number'") ||
    !network.includes('\\"postTareBaselineGraceMs\\":%lu') ||
    !network.includes('Post-tare grace must be from 0.5 to 10 s.') ||
    !network.includes('candidate.postTareBaselineGraceMs') ||
    !firmware.includes('session.config.postTareBaselineGraceMs')) {
  throw new Error('Post-tare grace must be wired through Tare settings, status/settings, and firmware');
}
if (!ui.includes("rangeCheck('dripDelayS',0,10,'Drip delay',{unit:'s'})") ||
    !ui.includes("dripDelayMs:sToMs('dripDelayS')") ||
    !ui.includes("$('dripDelayS').value=String((c.dripDelayMs??3000)/1000)") ||
    !ui.includes('How long to wait after water stops before recording the final drink weight and learning from it.') ||
    !network.includes('\\"dripDelayMs\\":%lu') ||
    !network.includes('Drip delay must be from 0 to 10 s.') ||
    !network.includes('candidate.dripDelayMs')) {
  throw new Error('Drip delay must be wired through Settings, status/settings, and config validation');
}
if (!html.includes('<summary>AtomHeart Eclair</summary>') ||
    !html.includes('Eclair uses the regular tare and timer workflow') ||
    !html.includes('It has no extra volume or sound options') ||
    html.indexOf('<summary>AtomHeart Eclair</summary>') <
        html.indexOf('<summary>Felicita</summary>') ||
    html.indexOf('<summary>AtomHeart Eclair</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    html.includes('id="eclair')) {
  throw new Error('Machine settings must include an informational AtomHeart Eclair subgroup without settings');
}
