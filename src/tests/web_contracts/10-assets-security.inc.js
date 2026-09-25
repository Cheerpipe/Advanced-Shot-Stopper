const VIEW_NAMES = webUi.VIEW_NAMES;
const rawPartialHtml = {};
for (const name of VIEW_NAMES) {
  rawPartialHtml[name] = fs.readFileSync(
      path.join(sketchDir, 'web', 'html', name + '.html'), 'utf8');
}
const rawAppJsSource = fs.readFileSync(path.join(sketchDir, 'web', 'app.js'), 'utf8');
const rawRuntimeJs = fs.readFileSync(path.join(sketchDir, 'web', 'js', 'runtime.js'), 'utf8');
const rawOtaImageJs = fs.readFileSync(
  path.join(sketchDir, 'web', 'js', 'ota-image.js'), 'utf8');
const rawViewJs = {};
for (const name of VIEW_NAMES) {
  rawViewJs[name] = fs.readFileSync(
      path.join(sketchDir, 'web', 'js', name + '.js'), 'utf8');
}
const runtimeConsumers = rawAppJsSource + '\n' + Object.values(rawViewJs).join('\n');
const runtimeReferences = new Set([...runtimeConsumers.matchAll(
  /\bR\.([A-Za-z_$][\w$]*)(?![\w$])/g)].map((match) => match[1]));
const runtimeExports = [
  ...rawRuntimeJs.matchAll(/export function\s+([A-Za-z_$][\w$]*)/g),
].map((match) => match[1]);
const runtimeExportBlock = rawRuntimeJs.match(/export\s*\{([\s\S]*?)\n\};/);
if (!runtimeExportBlock) throw new Error('Runtime module export block missing');
runtimeExports.push(...runtimeExportBlock[1].replace(/\s/g, '').split(',').filter(Boolean));
for (const name of runtimeExports) {
  if (!runtimeReferences.has(name)) {
    throw new Error(`Runtime export is not imported: ${name}`);
  }
}
if (rawAppJsSource.includes('htmlCache') ||
    !rawAppJsSource.includes('if(viewLoads.has(name))return viewLoads.get(name)') ||
    !rawAppJsSource.includes('viewLoads.set(name,load)') ||
    !rawAppJsSource.includes('finally{viewLoads.delete(name)}') ||
    !rawAppJsSource.includes("jsMods.has(name)&&htmlLoaded.has(name)")) {
  throw new Error('Partial loading must coalesce in flight and release text/promises after insertion');
}
const diagnosticDownload = rawViewJs.diagnostic.match(
  /const\s+([A-Za-z_$][\w$]*)=document\.createElement\('a'\)[\s\S]*?,([A-Za-z_$][\w$]*)=URL\.createObjectURL\(/);
const diagnosticClickAt = diagnosticDownload
  ? rawViewJs.diagnostic.indexOf(`${diagnosticDownload[1]}.click()`, diagnosticDownload.index)
  : -1;
const diagnosticHrefAt = diagnosticDownload
  ? rawViewJs.diagnostic.indexOf(
      `${diagnosticDownload[1]}.href=${diagnosticDownload[2]}`, diagnosticDownload.index)
  : -1;
const diagnosticRevokeAt = diagnosticDownload
  ? rawViewJs.diagnostic.indexOf(
      `URL.revokeObjectURL(${diagnosticDownload[2]})`, diagnosticDownload.index)
  : -1;
if (!diagnosticDownload || diagnosticHrefAt < 0 ||
    diagnosticClickAt < diagnosticHrefAt ||
    diagnosticRevokeAt < diagnosticClickAt) {
  throw new Error('Diagnostic download must revoke its object URL after use');
}
const rawCss = fs.readFileSync(path.join(sketchDir, 'web', 'app.css'), 'utf8');
const localizedSources = webUiLocale.renderSources([
  {file: webUi.sourcePath, type: 'html', content: rawShellHtml},
  ...VIEW_NAMES.map((name) => ({file: name + '.html', type: 'html',
    content: rawPartialHtml[name]})),
  {file: 'app.js', type: 'js', content: rawAppJsSource},
  {file: 'runtime.js', type: 'js', content: rawRuntimeJs},
  {file: 'ota-image.js', type: 'js', content: rawOtaImageJs},
  ...VIEW_NAMES.map((name) => ({file: name + '.js', type: 'js',
    content: rawViewJs[name]})),
  {file: 'app.css', type: 'css', content: rawCss},
], {language: 'en'}).sources;
let localizedAt = 0;
const shellHtml = localizedSources[localizedAt++].content;
const partialHtml = {};
for (const name of VIEW_NAMES) partialHtml[name] = localizedSources[localizedAt++].content;
const appJsSource = localizedSources[localizedAt++].content;
const runtimeJs = localizedSources[localizedAt++].content;
const otaImageJs = localizedSources[localizedAt++].content;
const viewJs = {};
for (const name of VIEW_NAMES) viewJs[name] = localizedSources[localizedAt++].content;
const css = localizedSources[localizedAt].content;
const allHtml = shellHtml.replace(
    /<section id="view-([a-z]+)" class="view" data-view="\1"><\/section>/g,
    (_, name) => {
      if (!partialHtml[name]) {
        throw new Error('Missing partial for shell placeholder: ' + name);
      }
      return `<section id="view-${name}" class="view" data-view="${name}">${
          partialHtml[name]}</section>`;
    });
let allJs = [appJsSource, runtimeJs, otaImageJs,
  ...VIEW_NAMES.map((n) => viewJs[n])].join('\n');
const singleQuoted = (value) => "'" + value.replace(/\\/g, '\\\\')
    .replace(/'/g, "\\'").replace(/\r/g, '\\r').replace(/\n/g, '\\n') + "'";
for (const value of [...new Set(Object.values(
  require('../web/locales/en.json').strings))].sort((a, b) => b.length - a.length)) {
  allJs = allJs.split(JSON.stringify(value)).join(singleQuoted(value));
}
// Most wiring checks look across shell + partials + all JS modules.
const html = allHtml;
const js = allJs;
const ui = allHtml + '\n' + allJs;
const settingsHtml = partialHtml.settings;
for (const field of settingsHtml.matchAll(
    /<(input|select|textarea)\b[^>]*\bid="([^"]+)"[^>]*>/g)) {
  const labelStart = settingsHtml.lastIndexOf('<label', field.index);
  const paragraphStart = settingsHtml.lastIndexOf('<p', field.index);
  const tag = labelStart > paragraphStart ? 'label' : 'p';
  const start = Math.max(labelStart, paragraphStart);
  const end = settingsHtml.indexOf(`</${tag}>`, field.index);
  const container = start >= 0 && end >= field.index
    ? settingsHtml.slice(start, end)
    : '';
  if (!container.includes('class="fieldHint')) {
    throw new Error(`Settings field ${field[2]} must have a localized field hint`);
  }
}
for (const [id, names] of Object.entries({
  bbwAlgorithm: ['Linear regression + offset correction:', 'Linear prediction + adaptive EWMA:'],
  autoToManualGuardLimitMode: ['Auto:', 'Manual:'], paddleMode: ['Natural:', 'Original:', 'Auto:'],
  momentaryStartEdge: ['Button press:', 'Button release:'],
  noScaleBbwMode: ['Allow manual brewing:', 'Warn once, then allow:', 'Require a scale:'],
  scalePreference: ['First available:', 'Prefer selected:', 'Preferred only:'],
  alertOutputChannel: ['Scale priority:', 'Buzzer only:', 'Scale only:'],
})) {
  const start = settingsHtml.indexOf(`id="${id}"`);
  const help = settingsHtml.slice(start, settingsHtml.indexOf('</label>', start));
  if (start < 0 || names.some((name) => !help.includes(`<strong>${name}</strong>`)))
    throw new Error(`Complex Settings selector ${id} must explain every option with a bold name`);
}
if (/<details\b[^>]*\bopen\b/i.test(allHtml)) {
  throw new Error('All collapsible <details> groups must start collapsed (no open attribute)');
}
if (css.includes('.brandLogo') || allHtml.includes('logo.svg') || allHtml.includes('brandLogo')) {
  throw new Error('Web UI must not embed a logo asset or .brandLogo styles');
}
if (!css.includes('.brand') || !css.includes('inline-flex') ||
    !css.includes('.brandMark') || !shellHtml.includes('class="brandMark"') ||
    !shellHtml.includes('<svg') ||    !shellHtml.includes('<small>Open</small>Brew by Weight') ||
    shellHtml.includes('logo.svg')) {
  throw new Error('Brand lockup must use inline SVG mark plus HTML wordmark');
}
if (shellHtml.split('M10 3h16v16H10z').length !== 2 ||
    shellHtml.split('class="brandMark"').length !== 4 ||
    !shellHtml.includes('<svg hidden><symbol id="brandMark" viewBox="0 0 36 48">') ||
    !shellHtml.includes('<use href="#brandMark"/>')) {
  throw new Error(
      'Brand mark must be defined once as an inline symbol sprite and referenced from the header, the loading view, and the inactive overlay');
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
if (!css.includes('.inactiveMain{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:1rem;max-width:24rem;width:100%;text-align:center}')) {
  throw new Error('Inactive Web UI must use a full-screen surface with centered content');
}
if (!shellHtml.includes('<div id="homeBoot" class="bootOverlay" role="status">') ||
    shellHtml.indexOf('id="homeBoot"') > shellHtml.indexOf('class="topBar"') ||
    !css.includes('.bootOverlay{position:fixed;inset:0;z-index:39') ||
    !css.includes('.inactiveOverlay{position:fixed;inset:0;z-index:40') ||
    !shellHtml.includes('id="homeBoot" class="bootOverlay" role="status"><div class="brand" aria-hidden="true">') ||
    !css.includes('.bootOverlay .brand,.inactiveOverlay .brand{') ||
    !css.includes('.bootOverlay .brandMark{width:2.85rem;height:3.8rem}') ||
    !css.includes('.brand span{display:flex;flex-direction:column;') ||
    !css.includes('.bootOverlay.isDone{opacity:0;pointer-events:none}') ||
    !css.includes('transition:opacity .25s ease') ||
    !shellHtml.includes('class="bootWave" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i>') ||
    !css.includes('.bootWave i{width:.55rem;height:100%;border-radius:.3rem;background:var(--ac);animation:bootWave 1.1s ease-in-out infinite}') ||
    !css.includes('@keyframes bootWave{0%,100%{transform:scaleY(.25)}50%{transform:scaleY(1)}}') ||
    !css.includes('.bootWave i{animation:none}') ||
    !runtimeJs.includes('let homeBootDone=false,fwReloading=false') ||
    !runtimeJs.includes('function hideHomeBoot(){if(homeBootDone||fwReloading)return;homeBootDone=true;const el=$(\'homeBoot\');if(!el)return;requestAnimationFrame(') ||
    !runtimeJs.includes('setTimeout(()=>el.classList.add(\'hidden\'),250)') ||
    !runtimeJs.includes('function message(text,kind=\'\'){hideHomeBoot();') ||
    !runtimeJs.includes('function applyHomeStatus(s){hideHomeBoot();') ||
    !runtimeJs.includes('function showInactiveOverlay(){const el=$(\'webUiInactive\');if(!el)return;hideHomeBoot();') ||
    !runtimeJs.includes('if(!reloaded){fwReloading=true;location.reload()}') ||
    !appJsSource.includes("if(view!=='home')R.hideHomeBoot();")) {
  throw new Error(
      'Home must boot behind a full-screen splash that paints before fading out in 250 ms once the first home status lands, never dismisses itself while a firmware reload is pending, hands the screen to the inactive overlay before it shows, stays below the inactive overlay, and never covers another view');
}
if (!shellHtml.includes('type="module"') ||
    !shellHtml.includes('src="/app.js?v=__FW_VERSION__"') ||
    /<script(?![^>]*\bsrc=)[^>]*>\s*\S/i.test(shellHtml)) {
  throw new Error('Web UI must load same-origin /app.js as a module (no inline script body)');
}
if (!shellHtml.includes('rel="manifest" href="/manifest.webmanifest"') ||
    !shellHtml.includes('rel="apple-touch-icon"') ||
    !shellHtml.includes('href="/icons/icon-192.png?v=__FW_VERSION__"') ||
    !shellHtml.includes('name="theme-color"') ||
    !shellHtml.includes('name="apple-mobile-web-app-capable"')) {
  throw new Error('Shell head must declare the PWA manifest, icon, and home-screen metadata');
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
// Complete, human-readable settings help is part of the UI contract. Adding a
// setting must raise this allowance when needed; hints must not be cut to fit it.
// The activation-history view (nav link, partial, runtime helpers) adds
// ~1.4 KB of HTML source allowance.
// The grouped Admin Power management panel (power policy, Wi-Fi sleep, BLE
// scan mode, idle scan backoff) replaces two JS-built controls with static
// markup and fuller hints, adding ~1.7 KB of HTML source allowance.
// The Admin Network device-name label and hint add ~0.1 KB of HTML source
// allowance.
// Profile-gated Linea Micra cloud account selection, per-preset temperature,
// and read-only machine-state diagnostics add labeled setup help. Wake-gesture
// recognition adds one default-on machine option. Scale-triggered shutdown
// adds one default-off machine option plus a grace-delay select.
// The Home boot splash adds one full-screen status surface plus its label to
// the shell markup; no partial, view, or control markup changes.
// The Home loading view reuses the header brand mark; the repeated inline
// artwork adds ~0.5 KB of shell source but only bytes of compressed payload.
// Scale↔machine power linkage adds two default-off Linea Micra options with
// setup help: power-on with the scale and scale-off with the machine.
// The Diagnostic standalone tare button adds 200 bytes of HTML allowance;
// compressed asset and firmware budgets remain unchanged.
// The Admin BLE master switch's rendered help needs 67 more HTML bytes than
// its original allowance; retain the complete wording.
// Continuous loop timing adds a phase table and a Reset control to Diagnostics.
if (htmlBytes > 72059) {
  throw new Error(`Web UI HTML source exceeds the authoring budget (${htmlBytes} > 72059)`);
}
// Resumable OTA hashes File slices incrementally in a lazy module so it never
// retains a full firmware image or charges the normal runtime path for it.
// Historical BLE disconnect/command diagnostics add display formatters. This
// source allowance does not change the compressed asset or firmware budgets.
// Opt-in power control/activity leases add 1 KiB of authoring allowance after
// sharing the Admin toggle persistence path.
// Dirty save-button feedback adds 300 bytes through one shared state helper.
// Zero baselines and the first-drop icon add 1,000 bytes of source allowance.
// Fixed chart grids, adaptive axes, and derived peak flow add 1,200 bytes.
// Continuous smoothed flow-rate curves add 273 source bytes.
// PWA manifest/icon head metadata and the cached-shell version self-heal add
// ~1.1 KB of combined source allowance.
// Activation-history paging, sorting, clear, and per-card delete add
// ~6 KB of JS source allowance.
// The Admin idle-scan backoff select and its save helper add 900 bytes; the
// fallback option that keeps an API-set value visible adds 100 more.
// The Admin machine-use scan boost select, save helper, and status fill add
// ~700 bytes of combined source allowance.
// Activation-history type icons (inline coffee/rinse SVG paths, coordinates
// rounded to one decimal) and their card wiring add ~3.2 KB of JS+combined
// source allowance.
// The Admin device-name field adds client validation, the no-reconnect
// preference save, and the .local status suffix: ~0.7 KB of JS and ~1 KB of
// combined source allowance.
// Humanized shot/activation time labels (relative day ladder plus seven
// locale strings) add ~0.4 KB of JS and combined source allowance.
// The second shot icon (outlined cup for shots without registered weight,
// split out of the coffee/rinse inline set) adds ~2.4 KB of JS+combined
// source allowance.
// Weekday names, short dates, and the hover <time> wrapper add ~0.2 KB of
// combined source allowance.
// Linea Micra account connection, machine selection, settings, refresh, and
// browser-side state expiry add the profile-gated cloud workflow.
// The Home boot splash one-shot hide helper and its non-home boot route keep
// the added runtime and shell logic inside 200 bytes. Painting the splash
// before it fades, holding it through a pending firmware reload, and releasing
// it to the inactive overlay raise that allowance to 194500.
// Machine-type-exclusive builds strip other types' markup at generation, so
// the runtime guards every read, write, validation, and save of a stripped
// element: ~1 KB of null-safe JS that ships in every variant.
// Crash-archive download and confirmed deletion add a bounded browser-only
// path without changing the normal polling payload or firmware compressor.
// The Admin BLE master switch checkbox adds its save handler, status sync,
// and revert-on-error path to the runtime module.
// Continuous loop timing renders nine phase rows and the peak-gap breakdown.
if (jsBytes > 198710) {
  throw new Error(`Web UI JS source exceeds the authoring budget (${jsBytes} > 198710)`);
}
// Sharing the brand wordmark selectors between the header, the loading view,
// and the inactive overlay pays for the added shell markup.
// The loop timing view adds only source allowance; compressed limits stay fixed.
if (htmlBytes + jsBytes > 270769) {
  throw new Error(`Web UI HTML+JS source exceeds the combined authoring budget (${htmlBytes + jsBytes} > 270769)`);
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
    !css.includes('--in:#fbfaf8') ||
    !css.includes('input:not([type=file],[type=checkbox]),select,textarea{') ||
    !css.includes('input[type=text],input[type=email],input[type=password],input[type=url]{-webkit-appearance:none;appearance:none}') ||
    !css.includes('min-height:3rem') ||
    !css.includes('background:var(--in)') ||
    !css.includes('input:-webkit-autofill') ||
    !css.includes('-webkit-box-shadow:0 0 0 3rem var(--in) inset') ||
    css.includes('input[type=text],input[type=password],select{-webkit-appearance:none') ||
    html.includes('id="staSsid" type="number"') ||
    html.includes('id="ntpServerCustom" type="number"') ||
    !html.includes('id="staSsid" type="text" maxlength="32" autocomplete="off"') ||
    !html.includes('id="ntpServerCustom" type="text" maxlength="63" placeholder="e.g. ntp.example.com" autocomplete="off"') ||
    !html.includes('> Scale lost<small class="fieldHint">When on, the built-in buzzer warns whenever the scale disconnects') ||
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
    !html.includes('silences the Bookoo speaker on its first connection') ||
    !html.includes('Sets the Bookoo speaker volume when alerts use the scale. Disabled silences the scale but does not change the built-in buzzer (<strong>Scale only or Scale priority</strong>).') ||
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
    !ui.includes('If the cup was already tared before connection, lift it and place it again.') ||
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

if (ui.includes('bleCompanionEnabled') ||
    ui.includes('bleCompanionPanel') ||
    ui.includes('/api/v1/admin/ble-compat') ||
    ui.includes('Companion characteristics') ||
    ui.includes('BLE companion') ||
    ui.includes('ensureBleScanPanel') ||
    network.includes('bleCompanion') ||
    network.includes('WebCommandType::BLE_COMPAT') ||
    networkHeader.includes('bleCompatHandler') ||
    firmwareCore.includes('persistBleCompanionEnabled') ||
    !ui.includes('<legend>') || !ui.includes('Power management') ||
    !ui.includes('bleScanIntensity') ||
    !ui.includes('BLE scan mode') ||
    ui.includes('Aggressive 100%') ||
    ui.includes('Normal 50%') ||
    ui.includes('Light 25%') ||
    !ui.includes('How much radio time is spent searching for Bluetooth espresso scales') ||
    !ui.includes("scanIntensity:wanted") ||
    !ui.includes('bleScanBackoff') ||
    !ui.includes('Idle scan backoff') ||
    !ui.includes("b.disabled=!webUiOwner||i.value==='relaxed'") ||
    !ui.includes("backoffMin:wanted") ||
    !ui.includes('bleScanBoost') ||
    !ui.includes('Scan boost on machine use') ||
    !ui.includes("boostMin:wanted") ||
    !ui.includes('bleEnabled') ||
    !ui.includes('Enable Bluetooth') ||
    !ui.includes("enabled:wanted") ||
    !ui.includes('Master switch that enables or disables Bluetooth connections to scales.') ||
    !network.includes('BleScanCommandPayload::ENABLED') ||
    !firmwareCore.includes('void persistBleScanEnabled') ||
    !firmwareCore.includes('applyLiveBleEnabled') ||
    !ui.includes('/api/v1/admin/ble-scan') ||
    !ui.includes("method:'PUT'") ||
    !network.includes('scanIntensity') ||
    !network.includes('backoffMin') ||
    !network.includes('boostMin') ||
    !network.includes('WebCommandType::BLE_SCAN_INTENSITY') ||
    !network.includes('command.type = WebCommandType::BLE_SCAN_INTENSITY') ||
    !firmwareCore.includes('void persistBleScanIntensity') ||
    !firmwareCore.includes('void persistBleScanBackoff') ||
    !firmwareCore.includes('void persistBleScanBoost') ||
    !networkHeader.includes('bleScanHandler')) {
    throw new Error('Power management Admin controls must keep live scan mode, idle backoff, machine-use boost, and the BLE master switch without Companion');
}
{
  const persistStart = firmwareCore.indexOf(
      'void persistBleScanIntensity(BleScanIntensity intensity) {');
  const persistEnd = persistStart >= 0
      ? firmwareCore.indexOf('\n}', persistStart)
      : -1;
  const persist = persistStart >= 0 && persistEnd > persistStart
      ? firmwareCore.slice(persistStart, persistEnd + 2)
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
    !firmware.includes('static_cast<int32_t>(discoveryScanIntensity())') ||
    !firmware.includes('SCALE_GATT_CONNECTING') ||
    firmware.includes('addDebugEvent(DebugCategory::SCALE, DebugCode::SCALE_CONNECTING)')) {
  throw new Error(
      'Scale discovery debug must report the applied scan duty, GATT connect, attempt, and fail reason');
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
    // Every status page (home, admin, diagnostic, settings) reports the dev
    // flag so the UI can react on first poll; at most two raw JSON emissions
    // per page may share the literal (e.g. nested objects). A formatting
    // refactor must not turn this into a count of source lines.
    Object.values(network.match(/\\"development\\":%s/g) || [])
        .length > 8 ||
    !js.includes('developmentMode') ||
    !js.includes("'development'in s") ||
    !js.includes('if(!developmentMode)syncAdminSessionUi(false)') ||
    !js.includes('if(developmentMode||$(\'uiOverridePanel\'))return;') ||
    !js.includes("classList.toggle('devBuild',developmentMode)") ||
    // Session visibility is driven by the actual unlock state, and
    // development builds treat administration as always unlocked; assert the
    // behavior (visible when either condition holds), not minified names.
    !/const\s+on\s*=\s*!!unlocked\s*\|\|\s*developmentMode/.test(js) ||
    !js.includes("if(R.developmentActive())return;") ||
    !rawShellHtml.includes('class="{{webui-meta:development-class}}"') ||
    !css.includes('body.devBuild #adminLockPanel') ||
    !css.includes('body.devBuild #diagnosticLockPanel')) {
  throw new Error(
      'SHOT_STOPPER_DEVELOPMENT must default off, make administration public by compile flag, and never flash the locked admin UI');
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
    !ui.includes('<small>Open</small>Brew by Weight') ||
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
