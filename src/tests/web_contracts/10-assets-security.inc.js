if (!htmlMatch) throw new Error('Embedded HTML raw string not found');
const shellHtml = htmlMatch[1];
const VIEW_NAMES = webUi.VIEW_NAMES;
const partialHtml = {};
for (const name of VIEW_NAMES) {
  partialHtml[name] = fs.readFileSync(
      path.join(sketchDir, 'web', 'html', name + '.html'), 'utf8');
}
const allHtml = shellHtml.replace(
    /<section id="view-([a-z]+)" class="view" data-view="\1"><\/section>/g,
    (_, name) => {
      if (!partialHtml[name]) {
        throw new Error('Missing partial for shell placeholder: ' + name);
      }
      return `<section id="view-${name}" class="view" data-view="${name}">${
          partialHtml[name]}</section>`;
    });
const appJsSource = fs.readFileSync(path.join(sketchDir, 'web', 'app.js'), 'utf8');
const runtimeJs = fs.readFileSync(path.join(sketchDir, 'web', 'js', 'runtime.js'), 'utf8');
const otaImageJs = fs.readFileSync(
  path.join(sketchDir, 'web', 'js', 'ota-image.js'), 'utf8');
const viewJs = {};
for (const name of VIEW_NAMES) {
  viewJs[name] = fs.readFileSync(
      path.join(sketchDir, 'web', 'js', name + '.js'), 'utf8');
}
const allJs = [appJsSource, runtimeJs, otaImageJs,
  ...VIEW_NAMES.map((n) => viewJs[n])].join('\n');
const css = fs.readFileSync(path.join(sketchDir, 'web', 'app.css'), 'utf8');
// Most wiring checks look across shell + partials + all JS modules.
const html = allHtml;
const js = allJs;
const ui = allHtml + '\n' + allJs;
if (/<details\b[^>]*\bopen\b/i.test(allHtml)) {
  throw new Error('All collapsible <details> groups must start collapsed (no open attribute)');
}
if (css.includes('.brandLogo') || allHtml.includes('logo.svg') || allHtml.includes('brandLogo')) {
  throw new Error('Web UI must not embed a logo asset or .brandLogo styles');
}
if (!css.includes('.brand') || !css.includes('inline-flex') ||
    !css.includes('.brandMark') || !shellHtml.includes('class="brandMark"') ||
    !shellHtml.includes('<svg') || !shellHtml.includes('<small>Advanced</small>Shot Stopper') ||
    shellHtml.includes('logo.svg')) {
  throw new Error('Brand lockup must use inline SVG mark plus HTML wordmark');
}
if (!shellHtml.includes('class="pageNav"') ||
    !shellHtml.includes('id="navToggle"') ||
    shellHtml.indexOf('class="pageNav"') > shellHtml.indexOf('id="app"') ||
    shellHtml.indexOf('class="topBar"') > shellHtml.indexOf('class="pageNav"') ||
    !css.includes('@media(min-width:700px)') ||
    !css.includes('.navToggle{display:none}') ||
    !appJsSource.includes("matchMedia('(min-width: 700px)')")) {
  throw new Error('Desktop Web UI must show a top nav instead of the hamburger');
}
if (!shellHtml.includes('type="module"') ||
    !shellHtml.includes('src="/app.js?v=__FW_VERSION__"') ||
    /<script(?![^>]*\bsrc=)[^>]*>\s*\S/i.test(shellHtml)) {
  throw new Error('Web UI must load same-origin /app.js as a module (no inline script body)');
}
for (const name of VIEW_NAMES) {
  if (!shellHtml.includes('id="view-' + name + '"') ||
      !shellHtml.includes('data-view="' + name + '"') ||
      !shellHtml.includes(`<section id="view-${name}" class="view" data-view="${name}"></section>`)) {
    throw new Error('Shell must keep empty placeholder for view: ' + name);
  }
}

const htmlBytes = Buffer.byteLength(allHtml, 'utf8');
const jsBytes = Buffer.byteLength(allJs, 'utf8');
// BBW help is condensed to fund the selector; source-only allowance adds 1 KiB
// for adaptive readback/CSV. Compressed assets and firmware budgets stay fixed.
if (htmlBytes > 54900) {
  throw new Error('Web UI HTML source exceeds the authoring budget');
}
// Resumable OTA hashes File slices incrementally in a lazy module so it never
// retains a full firmware image or charges the normal runtime path for it.
// Historical BLE disconnect/command diagnostics add display formatters. This
// source allowance does not change the compressed asset or firmware budgets.
if (jsBytes > 167100) {
  throw new Error('Web UI JS source exceeds the authoring budget');
}
if (htmlBytes + jsBytes > 222000) {
  throw new Error('Web UI HTML+JS source exceeds the combined authoring budget');
}
if (!/lang="en"/.test(html) || !ui.includes('role="switch"') ||
    !ui.includes('id="dActivator"') || !ui.includes('firstDropBeep') ||
    !ui.includes('paddleReturnReminderBeep') ||
    !ui.includes('buzzerScaleLostBeep') ||
    !ui.includes('buzzerAutoToManualGuardEndBeep') ||
    !ui.includes('buzzerManualNoScaleBeep') ||
    !ui.includes('buzzerScaleConnectedBeep') ||
    !ui.includes('scaleConnectedLed') ||
    !ui.includes('buzzerExtendedPulseRate') ||
    !ui.includes('buzzerSlowExtendedPulseRate') ||
    !html.includes('id="buzzerExtendedPulseRate"') ||
    !html.includes('id="buzzerSlowExtendedPulseRate"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt">Extended shot pulse<select id="buzzerExtendedPulseRate"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt">Slow extended pulse<select id="buzzerSlowExtendedPulseRate"') ||
    !css.includes('color-scheme:light dark') ||
    !css.includes('html,input,select,textarea{color-scheme:dark}') ||
    !css.includes('input[type=text],input[type=password]{-webkit-appearance:none;appearance:none}') ||
    !css.includes('min-height:2.5rem') ||
    !css.includes('background:var(--bg)') ||
    !css.includes('input:-webkit-autofill') ||
    !css.includes('-webkit-box-shadow:0 0 0 2.5rem var(--bg) inset') ||
    css.includes('input[type=text],input[type=password],select{-webkit-appearance:none') ||
    html.includes('id="staSsid" type="number"') ||
    html.includes('id="ntpServerCustom" type="number"') ||
    !html.includes('id="staSsid" type="text" maxlength="32" autocomplete="off"') ||
    !html.includes('id="ntpServerCustom" type="text" maxlength="63" placeholder="e.g. ntp.example.com" autocomplete="off"') ||
    !html.includes('> Scale lost<small class="fieldHint">Beeps when the scale disconnects') ||
    html.includes('Scale lost (BBW)') ||
    !html.includes('option value="fast" selected') ||
    !html.includes('option value="rapid">Rapid') ||
    html.includes('id="buzzerExtendedPulseBeep"') ||
    html.includes('20ms') ||
    html.includes('x segundo') ||
    ui.includes('querySelectorAll(\'.scaleIncapableOpt\').forEach(e=>{e.classList.toggle(\'fieldOff\',scaleOnly);e.querySelectorAll(\'input\').forEach') ||
    !ui.includes('alertOutputChannel') ||
    !ui.includes('buzzerSupported') ||
    !ui.includes('Output channel') ||
    !ui.includes('scale_priority') ||
    !ui.includes('Buzzer only') ||
    !ui.includes('class="fieldHint"') ||
    !ui.includes('id="bookooMuteOnBuzzerOnly"') ||
    !ui.includes('id="bookooConnectBeepLevel"') ||
    !html.includes('id="bookooMuteOnBuzzerOnly" type="checkbox" checked') ||
    !html.includes('id="buzzerScaleConnectedBeep" type="checkbox" checked') ||
    !html.includes('id="scaleConnectedLed" type="checkbox" checked') ||
    !html.includes('Blue LED while scale connected') ||
    !html.includes('</div><label><input id="scaleConnectedLed"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt"><input id="buzzerScaleConnectedBeep"') ||
    html.includes('buzzerOnlyOpt') ||
    !html.includes('option value="4" selected') ||
    !html.includes('keep the scale silent on connect') ||
    !html.includes('How loud the scale is when it connects. Used when sounds play on the scale (<strong>Scale only or Scale priority</strong>).') ||
    !html.includes('<strong>Requires shot-start tare.</strong>') ||
    !html.includes('Applies when <strong>Buzzer only</strong> is selected.')) {
  throw new Error('Web UI must show paddle state, scale beep options, and buzzer alerts');
}
if (!ui.includes('id="operationalWallS" type="number" min="5" max="60"') ||
    !ui.includes('Max BBW time (s)') ||
    !ui.includes('sToMs(') ||
    !ui.includes('rinseGestureMs:sToMs') ||
    !network.includes('Max BBW time must be from 5 to 60 s.')) {
  throw new Error('Max BBW time must be capped at 60 s in the UI and API messages');
}
if (!ui.includes('function rangeCheck(') ||
    !ui.includes('function showFieldError(') ||
    !ui.includes('samples × sample gap') ||
    !ui.includes('aria-live') ||
    !ui.includes('fieldError') ||
    !ui.includes('id="goalWeightG" type="number" min="10" max="200" step="1"') ||
    !ui.includes('validateNetworkClient') ||
    !ui.includes('validateDevicePasswordClient') ||
    !network.includes('Max recovery must be from 10 to 200 g.') ||
    !network.includes('Fast guard requires max recovery') ||
    !network.includes('SSID must be 1–32 characters.') ||
    !network.includes('configValidationErrorName(error)')) {
  throw new Error('UI/API must expose specific validation ranges, inline errors, and field-aware config errors');
}
if (!network.includes('"firstDropBeep"') ||
    !network.includes('"soundAlertsEnabled"') ||
    !network.includes('"paddleReturnReminderBeep"') ||
    !network.includes('"buzzerScaleLostBeep"') ||
    !network.includes('"buzzerAutoToManualGuardEndBeep"') ||
    !network.includes('"buzzerManualNoScaleBeep"') ||
    !network.includes('"buzzerScaleConnectedBeep"') ||
    !network.includes('"scaleConnectedLed"') ||
    !network.includes('"buzzerExtendedPulseRate"') ||
    !network.includes('"buzzerSlowExtendedPulseRate"') ||
    !network.includes('"alertOutputChannel"') ||
    !network.includes('"bookooMuteOnBuzzerOnly"') ||
    !network.includes('"bookooConnectBeepLevel"') ||
    !network.includes('"baseRevision"') ||
    !network.includes('jsonFieldPresent') ||
    !network.includes('CONFIG_REVISION_STALE') ||
    !network.includes('settingFieldCount') ||
    !network.includes('allowedCount > kSeenWords * 64U') ||
    !network.includes('uint64_t seen[kSeenWords]') ||
    !network.includes('WEB_UI_ETAG') ||
    !network.includes('\\"buzzerSupported\\"') ||
    !network.includes('BUZZER_SUPPORT_ENABLED') ||
    !network.includes('"autoRetare"') ||
    !network.includes('"retareWindowMs"') ||
    !network.includes('"minimumCupWeightG"') ||
    !network.includes('"retareStabilitySamples"') ||
    !network.includes('"retareStabilityToleranceG"') ||
    !network.includes('"retareStabilityMaxGapMs"') ||
    !network.includes('"retareStabilityMinDurationMs"') ||
    !network.includes('"bbwProtectionMs"') ||
    !network.includes('"fastExtractionGuardEnabled"') ||
    !network.includes('"avoidAccidentalTouchEnabled"') ||
    !network.includes('"maxRecoveryWeightG"') ||
    !network.includes('"minBbwBrewTimeMs"') ||
    !network.includes('"slowExtractionGuardEnabled"') ||
    !network.includes('"minRecoveryWeightG"') ||
    !network.includes('"maxBbwBrewTimeMs"') ||
    !network.includes('\\"extractionExtended\\"') ||
    !network.includes('\\"stopDetail\\"') ||
    !network.includes('"paddleReturnReminderIntervalMs"') ||
    !network.includes('"paddleReturnReminderMaxDurationMs"') ||
    !network.includes('"timezoneOffsetMinutes"') ||
    !network.includes('"ntpServerPreset"') ||
    !network.includes('"ntpServerCustom"') ||
    !network.includes('\\"time\\":{') ||
    !ui.includes('id="currentTime"') ||
    !ui.includes('id="ntpStatus"') ||
    !ui.includes('id="ntpServerPreset"') ||
    !ui.includes('id="ntpServerCustom"') ||
    !html.includes('id="staSsid" type="text"') ||
    !html.includes('id="ntpServerCustom" type="text"') ||
    !html.includes('id="presetRenameInput" type="text"') ||
    !ui.includes('id="syncTimeButton"') ||
    !ui.includes('id="autoRetare"') ||
    !ui.includes('id="fastExtractionGuardEnabled"') ||
    !ui.includes('id="avoidAccidentalTouchEnabled"') ||
    !ui.includes('id="maxRecoveryWeightG"') ||
    !ui.includes('id="minBbwBrewTimeS"') ||
    !ui.includes('Fast extraction guard') ||
    !ui.includes('id="slowExtractionGuardEnabled"') ||
    !ui.includes('id="minRecoveryWeightG"') ||
    !ui.includes('id="maxBbwBrewTimeS"') ||
    !ui.includes('Slow extraction guard') ||
    !ui.includes('id="retareWindowS"') ||
    !ui.includes('id="minimumCupWeightG"') ||
    !ui.includes('id="retareStabilitySamples"') ||
    !ui.includes('id="retareStabilityToleranceG"') ||
    !ui.includes('id="retareStabilityMaxGapS"') ||
    !ui.includes('id="retareStabilityMinDurationS"') ||
    !ui.includes('BBW protection (s)') ||
    !ui.includes('id="bbwProtectionS"') ||
    !ui.includes('Paddle reminder limit (min)') ||
    !ui.includes('id="paddleReturnReminderMaxDurationMin"') ||
    !ui.includes('paddleReturnReminderMaxDurationMs:Math.round(') ||
    ui.includes('Up to 120 shots') ||
    !ui.includes('/api/v1/time/sync') ||
    !firmware.includes('session.config.firstDropBeep') ||
    !firmware.includes('candidate.buzzerScaleConnectedBeep') ||
    !firmware.includes('candidate.scaleConnectedLed') ||
    !firmware.includes('candidate.buzzerSlowExtendedPulseRate') ||
    !firmware.includes('localBuzzer') ||
    !firmware.includes('BUZZER_SUPPORT_ENABLED') ||
    !firmware.includes('BUZZER_GPIO') ||
    !firmware.includes('machineServiceReminders')) {
  throw new Error('Scale beep settings must be configurable end-to-end');
}
if (firmware.includes('SHOT_STOPPER_ENABLE_ALED') ||
    firmware.includes('WS2812') ||
    firmware.includes('rgbLedWrite') ||
    firmware.includes('status_indicator') ||
    domain.includes('SHOT_STOPPER_ENABLE_ALED') ||
    domain.includes('BOOT_SUBSYSTEM_INDICATORS')) {
  throw new Error('WS2812B/ALED support must be fully removed');
}
if (!ui.includes('id="bullseyeMelodyEnabled"') ||
    !ui.includes('id="bullseyeRtttl" maxlength="500"') ||
    !ui.includes('Bullseye melody') ||
    !js.includes('bullseyeMelodyEnabled') ||
    !js.includes('bullseyeRtttl') ||
    !network.includes('"bullseyeMelodyEnabled"') ||
    !network.includes('"bullseyeRtttl"') ||
    !network.includes('stageBullseyeConfig') ||
    !firmwareCore.includes('serviceBullseyeMelody') ||
    !firmwareCore.includes('AlertOutputChannel::BUZZER_ONLY') ||
    !firmwareCore.includes('BULLSEYE_STABILITY_MS')) {
  throw new Error('Bullseye RTTTL alert must be configurable end-to-end');
}
{
  const start = network.indexOf(
      'esp_err_t ShotStopperNetwork::bullseyeTestHandler');
  const end = network.indexOf(
      'esp_err_t ShotStopperNetwork::preferredScaleClearHandler', start);
  const handler = start >= 0 && end > start ? network.slice(start, end) : '';
  const unlocks = handler.match(/self\.unlockJsonBody\(\)/g) || [];
  if (unlocks.length < 2) {
    throw new Error(
        'Bullseye test handler must release the shared PSRAM/JSON workspace on success and parse failure');
  }
}
if (!ui.includes("scaleConnectedLed:$('scaleConnectedLed').checked") ||
    !ui.includes("'scaleConnectedLed'") ||
    !network.includes('\\"scaleConnectedLed\\":%s') ||
    !firmware.includes('serviceScaleConnectedLed') ||
    !firmware.includes('SCALE_CONNECTED_LED_GPIO') ||
    !domain.includes('bool scaleConnectedLed = true')) {
  throw new Error('Scale-connected GPIO LED must be wired through Settings, status/settings, and firmware');
}

if (!ui.includes('id="soundAlertsEnabled"') ||
    ui.includes('id="homeSoundAlertsEnabled"') ||
    ui.includes('id="homeAlertsSub"') ||
    ui.includes("setHomeSub('homeAlertsSub'") ||
    ui.includes('function formatAlertsChannel(') ||
    ui.includes("persistHomeGuard('homeSoundAlertsEnabled'") ||
    !ui.includes('soundAlertsEnabled:$(\'soundAlertsEnabled\').checked') ||
    ui.includes('p.soundAlertsEnabled=$(\'homeSoundAlertsEnabled\').checked') ||
    ui.includes("k!=='soundAlertsEnabled'") ||
    js.includes("keys[0]==='soundAlertsEnabled'") ||
    !(ui.includes("typeof c.soundAlertsEnabled==='boolean'") ||
      ui.includes("'boolean'==typeof c.soundAlertsEnabled"))) {
  throw new Error('Sound alerts must be controlled from Settings, not Home Quick Settings');
}

if (html.indexOf('<summary>Brew by Weight</summary>') >
        html.indexOf('<summary>Cup protection</summary>') ||
    html.indexOf('<summary>Cup protection</summary>') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    !ui.includes('id="cupProtectionEnabled"') ||
    html.indexOf('id="cupProtectionEnabled"') >
        html.indexOf('id="stopIfCupRemoved"') ||
    html.indexOf('id="stopIfCupRemoved"') >
        html.indexOf('id="requireCupToStart"') ||
    html.indexOf('id="requireCupToStart"') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    !ui.includes('id="stopIfCupRemoved"') ||
    !ui.includes('id="requireCupToStart"') ||
    ui.includes('id="cupPresentWeightG"') ||
    html.includes('id="cupPresentWeightG"') ||
    html.indexOf('id="cupRemovedWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.includes('id="requireCupToStart" type="checkbox" checked') ||
    !ui.includes('id="cupProtectionEnabled" type="checkbox" checked> Cup protection') ||
    !ui.includes('cupProtectOpt') ||
    !ui.includes('place the cup after connect so it can be detected.') ||
    !ui.includes('id="homeCupProtectionEnabled"') ||
    html.indexOf('id="homeAvoidAccidentalTouchEnabled"') >
        html.indexOf('id="homeCupProtectionEnabled"') ||
    html.indexOf('id="homeCupProtectionEnabled"') >
        html.indexOf('id="homePresetBlock"') ||
    !ui.includes("persistHomeGuard('homeCupProtectionEnabled'") ||
    !ui.includes("'cupProtectionEnabled',1)") ||
    !ui.includes('cupProtectionEnabled:$(\'cupProtectionEnabled\')') ||
    !ui.includes('stopIfCupRemoved:$(\'stopIfCupRemoved\')') ||
    !ui.includes('requireCupToStart:$(\'requireCupToStart\')') ||
    !network.includes('cupProtectionEnabled') ||
    !network.includes('stopIfCupRemoved') ||
    !network.includes('requireCupToStart') ||
    !network.includes('cupRemovedWeightG')) {
  throw new Error('Cup protection master must precede Stop if cup is removed and Require cup to start; Home mirrors the master after accidental touch');
}

if (ui.includes('bleCompanionEnabled" type="checkbox" role="switch" checked>') ||
    !ui.includes('bleCompanionEnabled') ||
    !ui.includes('<legend>Bluetooth</legend>') ||
    !ui.includes('id="bleScanIntensity"') ||
    !ui.includes('Detection intensity') ||
    ui.includes('Aggressive 100%') ||
    ui.includes('Normal 50%') ||
    ui.includes('Light 25%') ||
    !ui.includes('How aggressively the stopper looks for a scale') ||
    !ui.includes("scanIntensity:wanted") ||
    !ui.includes('/api/v1/admin/ble-compat') ||
    !ui.includes("method:'PUT'") ||
    !ui.includes('active this boot') ||
    !ui.includes('restart required') ||
    !network.includes('bleCompanion') ||
    !network.includes('restartRequired') ||
    !network.includes('scanIntensity') ||
    !network.includes('WebCommandType::BLE_COMPAT_ENABLE') ||
    !network.includes('WebCommandType::BLE_SCAN_INTENSITY') ||
    !network.includes('command.type = WebCommandType::BLE_SCAN_INTENSITY') ||
    !firmwareCore.includes('bool persistBleScanIntensity') ||
    !networkHeader.includes('bleCompatHandler')) {
  throw new Error('Bluetooth Admin controls must keep Companion next-boot and live scan intensity');
}
{
  const persistStart = firmwareCore.indexOf(
      'bool persistBleScanIntensity(BleScanIntensity intensity) {');
  const persistEnd = firmwareCore.indexOf(
      'bool persistBleCompanionEnabled(bool enabled) {', persistStart + 1);
  const persist = persistStart >= 0 && persistEnd > persistStart
      ? firmwareCore.slice(persistStart, persistEnd)
      : '';
  if (!persist.includes('applyLiveBleScanIntensity') ||
      persist.includes('restartRequired')) {
    throw new Error(
        'PUT scanIntensity must apply live and must not set restartRequired');
  }
}
if (!domain.includes('DebugCode::SCALE_SCAN_STARTED') ||
    !domain.includes('DebugCode::SCALE_GATT_CONNECTING') ||
    !domain.includes('DebugCode::SCALE_CONNECT_ATTEMPT_FAILED') ||
    !domain.includes('DebugCode::SCALE_CONNECT_FAILED') ||
    !domain.includes('scale connect failed: %s (step=%s)') ||
    !firmware.includes('logScaleScanStarted') ||
    !firmware.includes('SCALE_GATT_CONNECTING') ||
    firmware.includes('addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_CONNECTING)')) {
  throw new Error(
      'Scale discovery debug must report scan, GATT connect, attempt, and fail reason');
}
if (!domain.includes('inline bool scaleHistoryIdentityEqual') ||
    !domain.includes('inline ScaleMacNvsAction decideScaleMacNvsAction') ||
    !firmwareCore.includes('decideScaleMacNvsAction(') ||
    !firmwareCore.includes(
        'scaleHistoryIdentityEqual(persistedSettings.scaleHistory') ||
    firmwareCore.includes(
        'memcmp(persistedSettings.scaleHistory, history, sizeof(history))') ||
    firmwareCore.includes(
        'memcmp(persistedSettings.scaleHistory, liveHistory')) {
  throw new Error(
      'Scale MAC NVS must ignore lastSeenSeq and must not write flash while GATT is live');
}
if (!domain.includes('BUZZER_SUPPORT_ENABLED = SHOT_STOPPER_ENABLE_BUZZER != 0') ||
    !domain.includes('SHOT_STOPPER_ENABLE_BUZZER must be 0 (off) or 1 (passive RTTTL)') ||
    !domain.includes('compiledBuzzerModeId') ||
    !domain.includes('DEFAULT_ALERT_OUTPUT_CHANNEL') ||
    !domain.includes(
        'BUZZER_SUPPORT_ENABLED ? AlertOutputChannel::BUZZER_ONLY') ||
    !domain.includes('static_cast<uint8_t>(DEFAULT_ALERT_OUTPUT_CHANNEL)') ||
    !buzzer.includes('requestTone') ||
    !buzzerPassive.includes('ledcAttach') ||
    !rtttlParser.includes('parseRtttl') ||
    !buzzerPatterns.includes('BUZZER_ECHO_INVERTED_NOTES') ||
    buzzerPatterns.includes('buzzerActiveSetTone') ||
    !buzzerRtttl.includes('RTTTL_TARE') ||
    !buzzerRtttl.includes('RTTTL_START_TIMER') ||
    !buzzerRtttl.includes('RTTTL_STOP_TIMER') ||
    !buzzerRtttl.includes('RTTTL_TARE_START') ||
    !buzzerRtttl.includes('RTTTL_FIRST_DROP') ||
    !buzzerRtttl.includes('RTTTL_PADDLE_OFF') ||
    !buzzerRtttl.includes('RTTTL_SHOT_END') ||
    !buzzerRtttl.includes('RTTTL_SCALE_CONNECTED') ||
    !buzzerRtttl.includes('RTTTL_NO_CUP') ||
    !buzzerRtttl.includes('RTTTL_EXTENDED_PULSE_SLOW') ||
    !buzzerRtttl.includes('RTTTL_EXTENDED_PULSE_MEDIUM') ||
    !buzzerRtttl.includes('RTTTL_EXTENDED_PULSE_FAST') ||
    !buzzerRtttl.includes('RTTTL_EXTENDED_PULSE_RAPID') ||
    buzzerRtttl.includes('ShotStopperDomain.h') ||
    buzzerRtttl.includes('rtttlForExtendedPulseRate') ||
    buzzerPassive.includes('ShotStopperBuzzerRtttl.h') ||
    !buzzerRtttl.includes('RTTTL_SCALE_LOST') ||
    !buzzerRtttl.includes('RTTTL_GUARD_STOP') ||
    !buzzerRtttl.includes('RTTTL_NO_SCALE') ||
    !buzzerRtttl.includes('RTTTL_RECOVERY_START') ||
    !buzzerRtttl.includes('RTTTL_NETWORK_RESET_OK') ||
    !buzzerRtttl.includes('RTTTL_FACTORY_RESET_OK') ||
    !buzzerRtttl.includes('RTTTL_RECOVERY_ERROR') ||
    !alertChannel.includes('selectAlertSink') ||
    !alertChannel.includes('AlertKind::Recovery') ||
    !alertChannel.includes('AlertKind::CommandImmediate') ||
    !alertChannel.includes('AlertKind::CommandFallback') ||
    !alertTone.includes('deriveBuzzerTone') ||
    !alertTone.includes('buzzerCueForAlertEvent') ||
    !alertTone.includes('rtttlForExtendedPulseRate') ||
    alertTone.includes('buzzerPatternForCue') ||
    !buzzer.includes('startRtttl') ||
    buzzer.includes('BUZZER_ACTIVE_DRIVE') ||
    !buzzer.includes('memcpy(rtttlBuf') ||
    !buzzer.includes('stopExtendedPulse') ||
    !domain.includes('ECHO_INVERTED') ||
    !firmware.includes('startExtendedPulseTrain') ||
    !firmware.includes('startPulseTrain') ||
    !firmware.includes('stopPulseTrains') ||
    !firmware.includes('serviceExtendedPulseAlert') ||
    firmwareCore.includes('rtttlForExtendedPulseRate') ||
    firmwareCore.includes('BuzzerCue::ABNORMAL') ||
    !domain.includes('DEFAULT_EXTENDED_PULSE_RATE') ||
    !firmware.includes('localBuzzer.request(command.buzzerPattern)') ||
    !kconfig.includes('0=off, 1=passive RTTTL')) {
  throw new Error('Local buzzer must be compile-time off (0) or passive RTTTL (1)');
}
if (!domainCore.includes('#ifndef SHOT_STOPPER_DEVELOPMENT') ||
    !domainCore.includes('#define SHOT_STOPPER_DEVELOPMENT 0') ||
    !domainCore.includes('DEVELOPMENT_BUILD = SHOT_STOPPER_DEVELOPMENT == 1') ||
    !domainCore.includes('SHOT_STOPPER_DEVELOPMENT must be 0 or 1') ||
    !domainCore.includes('CONFIG_SHOT_STOPPER_DEVELOPMENT') ||
    !kconfig.includes('config SHOT_STOPPER_DEVELOPMENT') ||
    !kconfig.includes('bypass WebUI admin unlock') ||
    !network.includes('SHOT_STOPPER_DEVELOPMENT == 1') ||
    !network.includes('adminUnlockAllowed') ||
    (network.match(/\\"development\\":%s/g) || []).length < 4 ||
    !js.includes('developmentMode') ||
    !js.includes("'development'in s") ||
    !js.includes('||developmentMode')) {
  throw new Error(
      'SHOT_STOPPER_DEVELOPMENT must default off, bypass adminUnlockAllowed when on, and unlock Web UI');
}
if (!domainCore.includes('#ifndef SHOT_STOPPER_ENABLE_JTAG') ||
    !domainCore.includes('#define SHOT_STOPPER_ENABLE_JTAG 0') ||
    !domainCore.includes('JTAG_SUPPORT_ENABLED = SHOT_STOPPER_ENABLE_JTAG == 1') ||
    !domainCore.includes(
        'SHOT_STOPPER_ENABLE_JTAG must be 0 (off) or 1 (USB Serial/JTAG)') ||
    !domainCore.includes('CONFIG_SHOT_STOPPER_ENABLE_JTAG') ||
    !domainCore.includes('enum class UsbSerialEnableSource') ||
    !domainCore.includes('OFF = 0') ||
    !domainCore.includes('COMPILE_FLAG = 1') ||
    !domainCore.includes('JUMPER = 2') ||
    domainCore.includes('DISABLED = 0') ||
    domainCore.includes('UsbSerialEnableSource::DISABLED') ||
    domainCore.includes('JTAG = 1') ||
    domainCore.includes('IO4 = 2') ||
    !domainCore.includes('usbSerialStateId') ||
    !domainCore.includes('usbConsoleIo4StateId') ||
    !kconfig.includes('config SHOT_STOPPER_ENABLE_JTAG') ||
    !kconfig.includes('USB Serial/JTAG (0=off, 1=on at boot)') ||
    !kconfig.includes('omitting the flag leaves JTAG off')) {
  throw new Error(
      'SHOT_STOPPER_ENABLE_JTAG must default off and map Kconfig onto the compile macro');
}
{
  const remoteKconfig = kconfig.slice(
      kconfig.indexOf('config SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL'),
      kconfig.indexOf('config SHOT_STOPPER_MACHINE_TYPE'));
  const cli = fs.readFileSync(
      path.resolve(sketchDir, '..', 'scripts', 'shotstopper_cli.sh'), 'utf8');
  const defaultFlags = cli.match(/SS_CLI_DEFAULT_FLAGS='([^']*)'/);
  const paddleHandler = network.slice(
      network.indexOf('esp_err_t ShotStopperNetwork::paddleHandler'),
      network.indexOf('esp_err_t ShotStopperNetwork::rinseHandler'));
  const rinseHandler = network.slice(
      network.indexOf('esp_err_t ShotStopperNetwork::rinseHandler'),
      network.indexOf('esp_err_t ShotStopperNetwork::stopHandler'));
  const remoteOn = firmwareCore.slice(
      firmwareCore.indexOf('case WebCommandType::REMOTE_ON:'),
      firmwareCore.indexOf('case WebCommandType::RINSE:'));
  const webRinse = firmwareCore.slice(
      firmwareCore.indexOf('case WebCommandType::RINSE:'),
      firmwareCore.indexOf('case WebCommandType::APPLY_CONFIG:'));
  const lockdownTest = fs.readFileSync(
      path.join(sketchDir, 'tests', 'remote_lockdown_host_test.cpp'), 'utf8');
  const hostTests = fs.readFileSync(
      path.join(sketchDir, 'tests', 'CMakeLists.txt'), 'utf8');
  if (remoteKconfig.length < 80 ||
      !remoteKconfig.includes('default n') ||
      /\bdefault y\b/.test(remoteKconfig) ||
      sdkconfigDefaults.includes(
          'CONFIG_SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=y') ||
      !defaultFlags ||
      defaultFlags[1].includes('SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL=1') ||
      !paddleHandler.includes('on && !REMOTE_MACHINE_CONTROL_ENABLED') ||
      !paddleHandler.includes('REMOTE_CONTROL_DISABLED') ||
      !rinseHandler.includes('!REMOTE_MACHINE_CONTROL_ENABLED') ||
      !rinseHandler.includes('REMOTE_CONTROL_DISABLED') ||
      !remoteOn.includes('!REMOTE_MACHINE_CONTROL_ENABLED') ||
      !webRinse.includes('!REMOTE_MACHINE_CONTROL_ENABLED') ||
      !lockdownTest.includes('#define SHOT_STOPPER_ENABLE_REMOTE_MACHINE_CONTROL 0') ||
      !lockdownTest.includes('WebCommandType::REMOTE_ON') ||
      !lockdownTest.includes('WebCommandType::RINSE') ||
      !hostTests.includes('remote_lockdown_host_test')) {
    throw new Error(
        'Remote machine control must stay opt-in (Kconfig/sdkconfig/CLI default off) with HTTP and processWebCommand guards');
  }
}
if (ui.includes('authenticatedOnly') ||
    ui.includes("s.setItem('shotStopperToken'") ||
    ui.includes('pageNav authenticatedOnly') ||
    ui.includes("authenticated()&&known") ||
    !ui.includes('function knownPath(') ||
    !ui.includes('class="brand"') ||
    !ui.includes('class="brandMark"') ||
    !ui.includes('<small>Advanced</small>Shot Stopper') ||
    !ui.includes('href="/" data-route="/"') ||
    !ui.includes("querySelectorAll('a[data-route]')") ||
    !ui.includes('ensureView') ||
    !ui.includes('/partials/') ||
    !network.includes('HTTPD_404_NOT_FOUND') ||
    !network.includes('notFoundHandler')) {
  throw new Error('Web UI must expose public SPA routes and redirect unknown paths to /');
}
if (html.includes('id="rememberMe"') ||
    js.includes('rememberMe:r') ||
    js.includes("s.setItem('shotStopperToken'") ||
    js.includes('function clearAuth()') ||
    network.includes('jsonBoolean(root, "rememberMe", rememberMe)') ||
    network.includes('createSession(token, csrf, rememberMe)') ||
    network.includes('uiAuthenticated') ||
    networkHeader.includes('SESSION_REMEMBER_MS')) {
  throw new Error(
      'Web UI must not use login tokens; exclusive WebUI claim owns the session');
}
const statusSection = html.match(/<fieldset[^>]*id="statusPanel"[^>]*><legend>Status<\/legend>([\s\S]*?)<\/fieldset>/) ||
    html.match(/<fieldset id="statusPanel"><legend>Status<\/legend>([\s\S]*?)<\/fieldset>/);
const scaleSection = html.match(/<fieldset[^>]*id="scalePanel"[^>]*><legend>Scale<\/legend>([\s\S]*?)<\/fieldset>/) ||
    html.match(/<fieldset id="scalePanel"><legend>Scale<\/legend>([\s\S]*?)<\/fieldset>/);
