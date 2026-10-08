const VIEW_NAMES = webUi.VIEW_NAMES;

(async () => {
  const assert = require('assert').strict, generated = await webUi.generate();
  const runtime = 'data:text/javascript,' + encodeURIComponent('export const $=()=>null;');
  const code = generated.secondaryJs.replace(/(['"])\/js\/runtime\.js\?[^'"]*\1/, JSON.stringify(runtime));
  const {views} = await import('data:text/javascript,' + encodeURIComponent(code));
  assert.deepEqual(Object.keys(views).sort(), [...webUi.SECONDARY_VIEWS].sort());
  for (const name of webUi.SECONDARY_VIEWS) {
    assert.equal(typeof views[name].init, 'function', name + ': lazy view must initialize');
  }
})().catch(error => {console.error(error); process.exitCode = 1;});
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
// Structural contracts match authoring sources with whitespace-insensitive
// comparison so the readable formatting style cannot break them. Semicolons
// and quote characters are stripped from both haystack and needle: formatters
// add statement terminators the historic minified needles omit and normalize
// string quotes.
const compactSource = (source) => source.replace(/[\s;'"]/g, '');
const codeIncludes = (source, needle) =>
  compactSource(source).includes(compactSource(needle));
// Extract a top-level declaration block from an authoring source by cutting
// at the next column-0 closing brace; valid for the formatted style whose
// top-level closers start a line.
const blockAt = (source, marker) => {
  const at = source.indexOf(marker);
  return at < 0 ? '' : source.slice(at, source.indexOf('\n}', at) + 2);
};
if (codeIncludes(rawAppJsSource, 'htmlCache') ||
    !compactSource(rawAppJsSource).includes(
        'if(viewLoads.has(name))returnviewLoads.get(name)') ||
    !compactSource(rawAppJsSource).includes('viewLoads.set(name,load)') ||
    !compactSource(rawAppJsSource).includes('finally{viewLoads.delete(name)') ||
    !compactSource(rawAppJsSource)
        .includes('jsMods.has(name)&&htmlLoaded.has(name)')) {
  throw new Error('Partial loading must coalesce in flight and release text/promises after insertion');
}
const diagnosticCompact = compactSource(rawViewJs.diagnostic);
const diagnosticDownload = diagnosticCompact.match(
  /const([A-Za-z_$][\w$]*)=document\.createElement\(a\)[\s\S]*?,([A-Za-z_$][\w$]*)=URL\.createObjectURL\(/);
const diagnosticClickAt = diagnosticDownload
  ? diagnosticCompact.indexOf(`${diagnosticDownload[1]}.click()`, diagnosticDownload.index)
  : -1;
const diagnosticHrefAt = diagnosticDownload
  ? diagnosticCompact.indexOf(
      `${diagnosticDownload[1]}.href=${diagnosticDownload[2]}`, diagnosticDownload.index)
  : -1;
const diagnosticRevokeAt = diagnosticDownload
  ? diagnosticCompact.indexOf(
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
// Localized values keep their emitted quote style: codeIncludes compares
// needles quote-insensitively, and a text-wide double-to-single rewrite would
// corrupt markup that embeds unescaped quotes (e.g. values like " class=").
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
    shellHtml.includes('id="navToggle"') ||
    shellHtml.indexOf('class="pageNav"') > shellHtml.indexOf('id="app"') ||
    shellHtml.indexOf('class="topBar"') > shellHtml.indexOf('class="pageNav"') ||
    !css.includes('@media(min-width:700px)') ||
    !css.includes('[data-nav-layout="text"] .pageNav svg{display:none}') ||
    !codeIncludes(appJsSource, 'function updateNavigationLayout()') ||
    !codeIncludes(appJsSource, "matchMedia('(max-width: 699px)')")) {
  throw new Error('Web UI must adapt a single navigation bar between header icons, header text and bottom icons');
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
    !css.includes('transition:opacity .25s}') ||
    !shellHtml.includes('class="bootWave" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i>') ||
    !css.includes('.bootWave i{width:.55rem;height:100%;border-radius:.3rem;background:var(--ac);animation:bootWave 1.1s ease-in-out infinite}') ||
    !css.includes('@keyframes bootWave{0%,100%{transform:scaleY(.25)}50%{transform:scaleY(1)}}') ||
    !css.includes('.bootWave i{animation:none}') ||
    !codeIncludes(runtimeJs, 'let homeBootDone=false,fwReloading=false') ||
    !codeIncludes(runtimeJs, 'function hideHomeBoot(seq=bootSeq){') ||
    !codeIncludes(runtimeJs, "bootTimer=setTimeout(()=>{if(seq===bootSeq)") ||
    !codeIncludes(runtimeJs, "function message(text,kind=''){if(kind==='error')hideHomeBoot();") ||
    codeIncludes(runtimeJs, 'function applyHomeStatus(s){hideHomeBoot();') ||
    !codeIncludes(runtimeJs, "function showInactiveOverlay(){const el=$('webUiInactive');if(!el)return;hideHomeBoot();") ||
    !codeIncludes(runtimeJs, "location.replace(location.pathname+'?fw='+version)") ||
    !codeIncludes(appJsSource, 'boot=R.showPageBoot();R.stopViewPolls();') ||
    !codeIncludes(appJsSource, 'if(ok){R.hideHomeBoot(boot);R.ensureBackgroundRecordStreams();}')) {
  throw new Error(
      'Every view must reuse the Home splash and fade immediately after data loads, preserving firmware reload and inactive overlay handoff');
}

{
  const assert = require('assert').strict, vm = require('vm');
  const deferred = () => {
    let resolve, reject;
    const promise = new Promise((yes, no) => {resolve = yes; reject = no;});
    return {promise, resolve, reject};
  };
  const flush = async () => {for (let i = 0; i < 12; i++) await Promise.resolve();};
  const overlaySource = rawRuntimeJs.slice(rawRuntimeJs.indexOf('function showPageBoot('),
    rawRuntimeJs.indexOf('function setOverlayOutOfReach('));
  const routeSource = appJsSource.slice(appJsSource.indexOf('function startView('),
    appJsSource.indexOf('function navigate('));
  const hooksSource = appJsSource.slice(appJsSource.indexOf('R.setViewPollHooks('),
    appJsSource.search(/document\.querySelectorAll\(\s*['"]a\[data-route\]['"]\s*\)/));
  (async () => {
    const classes = new Set(['hidden', 'isDone']), bodyClasses = new Set(), timers = [];
    const classList = set => ({add: (...names) => names.forEach(n => set.add(n)),
      remove: (...names) => names.forEach(n => set.delete(n)),
      toggle: (name, enabled) => enabled ? set.add(name) : set.delete(name)});
    let layouts = 0;
    const overlay = {style: {}, classList: classList(classes), setAttribute() {},
      get offsetWidth() {assert.equal(bodyClasses.has('pageLoading'), true); layouts++; return 1;}};
    const context = vm.createContext({bootSeq: 0, homeBootDone: false,
      bootTimer: 0, fwReloading: false,
      document: {body: {classList: classList(bodyClasses)}},
      $: id => {assert.equal(id, 'homeBoot', 'Do not query or await control animations'); return overlay;},
      requestAnimationFrame: () => {throw Error('Fade must not wait for render frames');},
      setTimeout: (fn, ms) => {assert.equal(ms, 250); timers.push(fn); return timers.length;},
      clearTimeout() {}});
    vm.runInContext(overlaySource, context);
    const token = context.showPageBoot();
    assert.equal(overlay.style.zIndex, '39', 'URL entry covers the header and content');
    assert.deepEqual([...classes], [], 'Page loading must appear immediately');
    assert.equal(bodyClasses.has('pageLoading'), true, 'Suppress control animations before loading');
    context.hideHomeBoot(token);
    assert.equal(classes.has('isDone'), true, 'Start fading synchronously when data is ready');
    assert.equal(layouts, 1, 'Apply static control positions before fading');
    assert.equal(bodyClasses.has('pageLoading'), true, 'Keep controls static throughout the fade');
    context.showPageBoot(); timers.shift()();
    assert.deepEqual([...classes], [], 'An old fade timer must not hide the next route');
    const headerZ = Number(css.match(/\.topBar\{[^}]*z-index:(\d+)/)[1]);
    assert.ok(Number(overlay.style.zIndex) < headerZ, 'Navigation leaves the complete responsive header and menu above loading');
    assert.equal(bodyClasses.has('pageLoading'), true, 'An old timer must not restore animations');
    context.hideHomeBoot(token);
    assert.deepEqual([...classes], [], 'An old route must not fade the current loading view');
    context.hideHomeBoot(context.bootSeq); timers.shift()();
    assert.equal(classes.has('hidden'), true);
    assert.equal(bodyClasses.has('pageLoading'), false, 'Restore normal animations after the fade');
    context.showPageBoot();
    context.fwReloading = true;
    await context.hideHomeBoot();
    assert.deepEqual([...classes], [], 'Keep loading visible while firmware reloads');

    const catalog = deferred(), preview = deferred(), statusMarkup = deferred(), applied = [];
    const statusContext = vm.createContext({activeView: 'admin', statusBusy: false,
      viewSeq: 1, viewReady: statusMarkup.promise,
      document: {hidden: false}, webUiPollingActive: () => true,
      statusPageOk: () => true,
      api: async () => ({adminUnlocked: true}), applyCommonStatus() {},
      viewStatusHandlers: {admin: () => applied.push('status')},
      timezoneCatalogPromise: catalog.promise, timezonePreviewLoad: preview.promise,
      noteReachOk: () => applied.push('ready'), noteReachFail() {}, armStatusTimer() {}});
    vm.runInContext(blockAt(rawRuntimeJs, 'async function loadStatus('), statusContext);
    const adminLoading = statusContext.loadStatus(); await flush();
    assert.deepEqual(applied, [], 'Fetch status immediately but defer hydration until markup is initialized');
    statusMarkup.resolve(); await flush();
    assert.deepEqual(applied, ['status']);
    catalog.resolve(); await flush();
    assert.deepEqual(applied, ['status'], 'Admin must still wait for its timezone preview');
    preview.resolve(); assert.equal(await adminLoading, true);
    assert.deepEqual(applied, ['status', 'ready']);
    const outdated = deferred(); statusContext.api = () => outdated.promise;
    const oldStatus = statusContext.loadStatus(); statusContext.activeView = 'settings'; statusContext.viewSeq++;
    outdated.resolve({adminUnlocked: true});
    assert.equal(await oldStatus, false, 'Discard a status response from the previous view');

    let rendered = 0;
    const logContext = vm.createContext({logBootId: 3, bootId: 3, logEvents: [],
      lastLog: 9, logMissed: 0, LOG_EVENTS_CAPACITY: 500, activeView: 'diagnostic',
      updateLogHealth() {}, renderLog: () => {rendered++;}, updateFirmwareFooter() {},
      noteReachOk() {}, noteReachFail() {}});
    const logFns = rawRuntimeJs.slice(rawRuntimeJs.indexOf('function logStreamFrame('),
        rawRuntimeJs.indexOf('function diagSnapshotOk('));
    vm.runInContext(logFns, logContext);
    const event = sequence => ({sequence, atMs: 1, wallSec: 0, localSec: 0,
      level: 'info', category: 'web', message: 'm'});
    const frame = {v: 1, type: 'log', bootId: 3, historyOverwritten: 0,
      missedEvents: 2, serialDropped: 0, hasMore: false,
      cursorInvalid: false, events: [event(10), event(11)]};
    logContext.applyLogFrame(logContext.logStreamFrame(frame));
    for (const bad of [{...frame, missedEvents: null}, {...frame, events: [{...event(12), message: 4}]}])
      assert.throws(() => logContext.logStreamFrame(bad));
    assert.equal(rendered, 1);
    assert.equal(logContext.lastLog, 11);
    assert.equal(logContext.logMissed, 2);
    logContext.applyLogFrame(logContext.logStreamFrame({...frame, bootId: 4,
      missedEvents: 0, events: [event(1)]}));
    assert.equal(logContext.logEvents.length, 1,
        'a boot change must clear the rendered log');
    assert.equal(logContext.lastLog, 1);
    assert.equal(rendered, 2);

    const slotSource = ['function drainDeviceSlots(', 'function acquireDeviceSlot(',
      'function releaseDeviceSlot(', 'async function api(']
        .map((marker) => blockAt(rawRuntimeJs, marker)).join('\n');
    for (const view of ['stats', 'history', 'home', 'settings', 'admin', 'diagnostic']) {
      const requests = [], responses = [];
      const slots = vm.createContext({activeView: view, deviceInFlight: 0, deviceWaiters: [],
        DEVICE_MAX_INFLIGHT: 1, webUiPollingActive: () => true,
        WEB_UI_CLIENT_HEADER: 'X-WebUI-Client', webUiClientId: 'test', webUiPowerSeconds: () => 0,
        AbortController, setTimeout: () => 1, clearTimeout() {}, __WEBUI_TEXT__: key => key,
        fetch: path => {const response = deferred(); requests.push(path); responses.push(response); return response.promise;}});
      vm.runInContext(slotSource, slots);
      const records = view === 'history' ? '/api/v1/log' : '/api/v1/status/settings';
      const one = slots.api(records), two = slots.api(records),
        three = slots.api(records);
      const finish = () => responses.shift().resolve({ok: true, text: async () => '{}'});
      await flush();
      assert.equal(requests.length, 1, view + ': one request at a time');
      const command = slots.api('/api/v1/config', {method: 'POST', body: '{}'});
      finish(); await flush();
      assert.equal(requests.length, 2, 'Release the request slot without exceeding its limit');
      assert.equal(requests.includes('/api/v1/config'), false, 'Commands wait for all reads');
      finish(); await flush(); finish(); await flush();
      assert.equal(requests.at(-1), '/api/v1/config');
      assert.equal(slots.deviceInFlight, 1, 'Command runs alone');
      const following = slots.api(records); await flush();
      assert.equal(requests.at(-1), '/api/v1/config', 'Reads cannot overlap a command');
      finish(); await flush(); finish();
      await Promise.all([one, two, three, command, following]);
      assert.equal(slots.deviceInFlight, 0);
      assert.equal(slots.deviceWaiters.length, 0);
      const failure = slots.api(records); await flush();
      responses.shift().reject(new Error('Unavailable'));
      await assert.rejects(failure);
      assert.equal(slots.deviceInFlight, 0, 'Network failure must release admission');
    }

    // Record pages have no REST reads left; the remaining per-view fetches
    // are the claim-gated status pages, exercised by the lifecycle suites.
    assert(!codeIncludes(rawRuntimeJs, '/api/v1/stats?offset=') &&
           !codeIncludes(rawRuntimeJs, '/api/v1/history?offset='),
        'record pages must subscribe over the owned WebSocket, not read REST');

    const assets = {stats: deferred(), history: deferred()}, secondary = deferred(),
      assetEvents = [], initialized = [], sections = {};
    for (const name of ['stats', 'history']) sections['view-' + name] = {
      set innerHTML(html) {assetEvents.push('insert-' + name);},
    };
    const assetContext = vm.createContext({jsMods: new Map(), htmlLoaded: new Set(['home']),
      viewLoads: new Map(), SECONDARY: new Set(['stats', 'history']), secondaryViews: null,
      assetQuery: '?v=test', document: {getElementById: id => sections[id]},
      __WEBUI_TEXT__: key => key,
      loadPartial: name => {assetEvents.push('html-' + name); return assets[name].promise;},
      importModule: () => {assetEvents.push('js'); return secondary.promise;}});
    const ensureSource = appJsSource.slice(appJsSource.indexOf('async function ensureView('),
      appJsSource.indexOf('R.setEnsureViewHook')).replace(/import\(/g, 'importModule(');
    vm.runInContext(ensureSource, assetContext);
    const firstStats = assetContext.ensureView('stats'), againStats = assetContext.ensureView('stats'),
      firstHistory = assetContext.ensureView('history');
    assert.deepEqual(assetEvents, ['html-stats', 'js', 'html-history'], 'HTML and shared module download together and deduplicate');
    secondary.resolve({views: Object.fromEntries(['stats', 'history'].map(name => [name, {
      init: () => {assert.equal(assetEvents.includes('insert-' + name), true); initialized.push(name);},
    }]))});
    await flush(); assert.deepEqual(initialized, [], 'Module must not initialize before its markup');
    assets.stats.resolve('<div>Stats</div>'); assets.history.resolve('<div>History</div>');
    await Promise.all([firstStats, againStats, firstHistory]);
    await assetContext.ensureView('stats'); await assetContext.ensureView('history');
    assert.deepEqual(initialized, ['stats', 'history'], 'Revisits reuse each initialized view');
    assert.equal(assetEvents.filter(event => event === 'js').length, 1);

    for (const view of ['home', 'settings', 'stats', 'history', 'diagnostic', 'admin']) {
      const events = [], markup = deferred(), status = deferred(), data = deferred(), bodyClasses = new Set();
      const path = view === 'home' ? '/' : '/' + view;
      const r = vm.createContext({activeView: '', routeSeq: 0, logTimer: 0,
        jsMods: new Map(),
        ROUTES: {[path]: view}, knownPath: () => path, viewToPath: () => path,
        location: {pathname: path}, history: {}, stopExtraPolls() {}, placePill() {}, markNavActive() {},
        ensureView: () => {events.push('markup'); return markup.promise;},
        document: {hidden: false, querySelectorAll: () => [], body: {classList: classList(bodyClasses)}}, setInterval: () => 1,
        __WEBUI_TEXT__: key => key,
        R: {showPageBoot: () => {events.push('show'); return 1;}, stopViewPolls() {},
          api: async () => {throw new Error('record views must not probe REST status');}, compatibilityModeOn: () => false,
          withPollGate: fn => fn(), webUiPollingActive: () => true,
          setActiveView() {}, armStatusTimer() {}, applyDiagnosticNavigation() {},
          startDiagnosticStream() {}, stopDiagnosticStream() {},
          startStatsStream: () => {events.push('data'); return data.promise;},
          startHistoryStream: () => {events.push('data'); return data.promise;},
          ensureBackgroundRecordStreams() {},
          loadStatus: () => {events.push('status'); return status.promise;},
          startLogStream: () => {events.push('data'); return data.promise;},
          hideHomeBoot: () => events.push('fade'), message: () => events.push('error')}});
      vm.runInContext(routeSource, r);
      const loading = r.renderRoute(path); await flush();
      const paired = ['stats', 'history'].includes(view);
      assert.deepEqual(events, paired ? ['show', 'markup', 'data'] :
        ['show', 'markup', 'status'], view + ': fetch data while lazy markup loads');
      markup.resolve(); await flush();
      assert.equal(bodyClasses.has('homeView'), view === 'home', 'Only Home uses the scrolling desktop header');
      assert.equal(events.includes('fade'), false, view + ': wait for initial data');
      status.resolve(true); await flush();
      if (['stats', 'history', 'diagnostic'].includes(view)) {
        assert.deepEqual(events, paired ? ['show', 'markup', 'data'] : ['show', 'markup', 'status', 'data']);
        data.resolve(true);
      }
      await loading;
      assert.equal(events.at(-1), 'fade', view + ': fade only after all initial data');

      r.ensureView = async () => {throw new Error('Markup unavailable');};
      const beforeFailure = events.length;
      await r.renderRoute(path);
      assert.equal(events.slice(beforeFailure).includes('error'), true, view + ': expose lazy-load failure');
      assert.equal(events.slice(beforeFailure).includes('fade'), false, view + ': failed markup cannot reveal the page');
      r.ensureView = async () => {};
      const loadName = view === 'stats' ? 'startStatsStream' : view === 'history' ? 'startHistoryStream' : 'loadStatus';
      r.R[loadName] = async () => false;
      await r.renderRoute(path);
      assert.equal(events.at(-1), 'error', view + ': expose initial data failure');
      const old = deferred(); r.R[loadName] = () => old.promise;
      const superseded = r.renderRoute(path); await flush();
      r.routeSeq++; old.resolve(true); await superseded;
      assert.notEqual(events.at(-1), 'fade', view + ': ignore superseded route readiness');
      r.R[loadName] = async () => true;
      r.ensureView = async () => {events.push('markup');};
      r.R.setViewPollHooks = hooks => {r.viewHooks = hooks;};
      vm.runInContext(hooksSource, r);
      const beforeReload = events.length;
      await r.viewHooks.start(view);
      assert.equal(events[beforeReload], 'show', view + ': Reload re-enters loading lifecycle');
      assert.equal(events[beforeReload + 1], 'markup', view + ': retry lazy markup after failure');
      assert.equal(events.at(-1), 'fade');
      let queued;
      r.R.withPollGate = fn => {queued = fn; return Promise.resolve();};
      r.startView(view); r.routeSeq++;
      const beforeQueued = events.length;
      assert.equal(await queued(), false);
      assert.equal(events.length, beforeQueued, view + ': discard queued work for an earlier visit to the same page');
    }
  })().catch(error => {console.error(error); process.exitCode = 1;});
}
{
  const fade = Number(css.match(/\.bootOverlay\{[^}]*transition:opacity ([\d.]+)s\}/)[1]);
  const suppression = css.match(/body\.pageLoading \.view[^}]*\}/)?.[0] || '';
  for (const suffix of ['', ' *', '::before', '::after', ' *::before', ' *::after']) {
    if (!suppression.includes('body.pageLoading .view' + suffix)) {
      throw new Error('Loading must suppress view and control pseudo-element animations');
    }
  }
  if (!suppression.includes('transition:none!important;animation:none!important') ||
      suppression.includes('bootWave')) {
    throw new Error('Loading must suppress control motion while preserving the loading wave');
  }
  for (const selector of ['.ktrack', '.kknob']) {
    const rule = css.slice(css.indexOf(selector + '{')).split('}')[0];
    if (Number(rule.match(/transition:([\d.]+)s/)[1]) !== .2) {
      throw new Error('Normal toggle track and thumb animations must remain available after loading');
    }
  }
  if (fade !== .25 ||
      !codeIncludes(runtimeJs, "document.body.classList.remove('pageLoading')}}," + Math.round(fade * 1000) + ')') ||
      !css.includes('transition:.01ms!important')) {
    throw new Error('Splash removal must follow the immediate 250 ms fade and restore control motion');
  }
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
// The grouped Admin Power management panel (ESP32 power management, Wi-Fi
// sleep, BLE scan mode) replaces two JS-built controls with static markup
// and fuller hints, adding ~1.7 KB of HTML source allowance.
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
// Last NTP sync adds one timestamp cell to the Diagnostics grid.
// The Settings Preferred scale row gains its own (rename) link, matching
// Home and Diagnostic: +128 bytes of HTML allowance; compressed asset and
// firmware budgets remain unchanged.
// The scale command inventory adds a small two-column Diagnostic table.
// Per-section revert buttons beside every dirty-tracked save bar (Settings
// brew/machine/Micra, Admin network/date-time/webhooks/device password) add
// ~1.6 KB of HTML source allowance; compressed asset budgets stay fixed.
// Save+revert bars for the Admin BLE, Power, and Frontend groups add ~1 KB
// of HTML source allowance; compressed asset budgets stay fixed.
// The Admin Automatic date & time toggle (NTP sync master switch) adds
// ~0.2 KB of HTML source allowance; compressed asset budgets stay fixed.
// The Admin Firmware master-switch section adds ~1 KB of HTML source
// allowance; compressed asset and firmware budgets stay unchanged.
// Shot stats gain a Tare time and a Scale field: two Home card fields plus
// two Stats table headers add ~0.1 KB of HTML source allowance; the
// compressed asset and firmware budgets stay effectively unchanged.
// Six La Marzocco Cloud diagnostic fields add 0.8 KB of HTML allowance.
// The Stats loading overlay reuses the panel wave and adds ~0.2 KB of HTML source.
// The Scale profiling section adds ~1 KB of HTML allowance (state rows, four
// controls, and the replacement hint); compressed asset budgets stay fixed.
// Independent wireless popups add their two identity and signal rows.
// The supplied four-path Backflush symbol adds 2 KiB of HTML source allowance.
// Stats duration/yield metric icons and the Diagnostic Scale timer metric add
// ~1 KB of HTML source allowance; compressed asset budgets stay fixed.
// Firmware image, memory-region and OTA partition limits remain unchanged.
// The Home full-screen shot card adds two icon buttons, the hero body wrapper,
// and their labels: ~0.8 KB of HTML source allowance; compressed asset and
// firmware budgets stay fixed.
// The Home quick target-weight sheet adds the hero pencil button and the
// inert dialog markup (grab row, stacked preset/label header, value row with
// Reset, tick-scale zone): ~1.4 KB of HTML source allowance; compressed asset
// and firmware budgets stay fixed.
if (htmlBytes > 87200) {
  throw new Error(`Web UI HTML source exceeds the authoring budget (${htmlBytes} > 87200)`);
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
// The removed Admin idle-scan backoff and machine-use scan boost selects
// once added ~1.7 KB of combined source allowance.
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
// The No Scale Guard abort icon adds 600 bytes of History HTML, 100 bytes of
// runtime wiring, and 700 combined authoring bytes. Flash budgets stay fixed.
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
// Matching recent/lifetime gap columns replace the peak-gap label breakdown.
// Rendering the current scale command inventory adds a bounded status update.
// IANA zone loading, selected-zone preview, and first-use detection add ~3 KiB.
// Per-section revert (discard unsaved changes) baselines, snapshot helpers,
// handlers, and button wiring across Settings and Admin add ~5 KB of JS
// source allowance.
// Deferred Admin BLE/Power/Frontend saving moves theme persistence behind
// Save and adds dirty flags, guarded hydration, and save/revert handlers for
// ~4 KB of JS source allowance.
// The firmware master switch adds the Admin Firmware section wiring plus
// compatibility-mode tab/section gating for ~2.5 KB of JS source allowance.
// Mid-upload image guarding records per-4 KiB-block digests during the OTA
// identity scan and re-verifies every range before it is sent, adding
// ~1.1 KB of JS source allowance.
// Shot stats gain Tare time and Scale: the tare chart marker, two table
// columns, CSV columns, and last-shot wiring add ~1.1 KB of JS source
// allowance.
// Cloud call rendering uses 0.5 KB beyond the previous JS allowance.
// Serialized requests and deferred hydration share one record-page loader.
// Allow 1 KiB of source wiring; compressed assets and firmware limits stay fixed.
// Scale profiling adds ~3.4 KB of JS source allowance (state-matrix
// rendering plus the streamed TXT download); compressed assets and firmware
// limits stay fixed.
// Backflush state/history labels and live safety diagnostics add 1.5 KiB source.
// Preferred-scale draft/readback joins Machine Save/Revert (+300 measured bytes).
// Floating header geometry adds 256 bytes of source allowance; gzip limits stay fixed.
// The header theme mode button adds ~0.5 KB of JS source allowance: the cycle
// handler, glyph mode state, and localized label wiring. Compressed asset and
// firmware limits stay fixed.
// The hero curve gains the prototype's solid area and baseline (+250 bytes).
// Home field deltas, initial/reconnect recovery and socket liveness add 6 KiB
// of authoring allowance. Firmware image and OTA limits stay fixed.
// The view-scoped diagnostic WebSocket (frame merge validation, subscribe
// lifecycle, and the shared States/Machine I/O/Scale live renderer) adds
// ~1.5 KB of JS source allowance; compressed asset and firmware limits stay
// fixed.
// The no-scale local clock adds 1.5 KB of source allowance; compressed
// per-asset, combined flash, firmware image and OTA limits remain fixed.
// The no-scale finish transition and early paddle edge add 2.1 KB of source allowance.
// The full-screen shot card state machine (enter/exit/back-consume, session
// keep-alive during a live shot, and the router popstate hook) adds ~3.3 KB of
// JS source allowance; compressed per-asset and firmware limits stay fixed.
// The view-scoped history WebSocket (op subscription and replay, one-shot
// append fetches, strict frame validation, and the navigation visibility move
// onto Home deltas) adds ~2.6 KB of JS source allowance while deleting the
// REST pull plumbing; compressed asset and firmware limits stay fixed.
// The Stats stream (continuation assembly, export window, row validation)
// adds ~4 KB more; compressed asset and firmware limits stay fixed.
// The record-view UI freshness on Home deltas and the single-flight export
// add ~0.6 KB more; compressed asset and firmware limits stay fixed.
// The backpressure-paced owned socket (module keepalive/resync helpers, the
// visibilitychange resync, and the raised frame guard) adds ~0.5 KB more;
// compressed asset and firmware limits stay fixed.
// The Diagnostic full-WebSocket migration (log stream subscription and
// validation, the snapshot-readiness wait, and the local clock/age ticker)
// adds ~0.7 KB of JS source allowance while deleting the REST status and
// log pull plumbing; compressed asset and firmware limits stay fixed.
// Web UI JS sources are authored in formatted, human-readable style since the
// 2026-10 reformat; this allowance was re-measured once for that re-baselining
// (332.1 KB of localized JS) with fixed headroom, and generated assets stayed
// byte-identical modulo the cache-buster tag. New features raise it again per
// the entries below.
// The Home quick target-weight sheet adds the pointer-drag controller with
// velocity-adaptive acceleration, the tick-scale painter, grab dismiss,
// keyboard stepping, and the hydrate→goal→save commit path in home.js plus
// the hero pencil gate in runtime.js: ~6.3 KB of JS source allowance;
// compressed asset and firmware limits stay fixed.
if (jsBytes > 343000) {
  throw new Error(`Web UI JS source exceeds the authoring budget (${jsBytes} > 343000)`);
}
// Sharing the brand wordmark selectors between the header, the loading view,
// and the inactive overlay pays for the added shell markup.
// The loop timing view adds only source allowance; compressed limits stay fixed.
// The per-section revert buttons and their wiring add ~6.7 KB of combined
// source allowance.
// The Linea Micra connect spinner (glyph spans plus the phase-gated busy
// toggle) adds ~0.25 KB of combined source allowance.
// Deferred Admin BLE/Power/Frontend saving adds ~5 KB of combined source
// allowance (bars in HTML, dirty/save/revert logic in JS).
// The Admin Automatic date & time toggle adds ~0.4 KB of combined source
// allowance (checkbox row in HTML, payload/load/dirty hooks in JS).
// The Admin Automatic date & time toggle adds ~0.4 KB of combined source
// allowance (checkbox row in HTML, payload/load/dirty hooks in JS).
// The firmware master switch adds ~6.5 KB of combined source allowance
// (Firmware section in HTML; section wiring and compatibility-mode gating
// in JS).
// Mid-upload OTA image guarding adds ~1.1 KB of combined JS source allowance.
// Shot stats Tare time and Scale add ~1.2 KB of combined source allowance
// (card fields and table headers in HTML; marker, columns, and wiring in JS).
// Cloud diagnostics add 1.3 KB of combined source allowance.
// Paired reads and deferred hydration add 1 KiB of source allowance only.
// Scale profiling adds ~4.4 KB of combined source allowance (diagnostic
// section in HTML; matrix rendering and download in JS).
// Selectable Micra transport and bounded WS diagnostics add 1.5 KB after
// sharing option payloads and diagnostic row creation. Flash limits stay fixed.
// Backflush contributes the 3.5 KiB source allowances described above.
// Include the same 256-byte floating-header source allowance.
// The theme button adds ~1.1 KB of combined source: three glyph states in the
// shell markup plus the JS cycle, state, and label wiring described above.
// The hero solid curve area and baseline add the same 250-byte JS allowance.
// The diagnostic WebSocket (frame validation, view-scoped subscription, and
// the shared live renderer) plus the Stats metric icons add ~1.3 KB of
// combined source allowance; compressed budgets stay fixed.
// Include the same 1.5 KB local-clock source allowance.
// Include the same 2.1 KB finish-transition source allowance.
// Include the same ~3.4 KB full-screen shot card allowance (buttons and hero
// wrapper in HTML; state machine, session keep-alive, and popstate hook in JS).
// Include the same ~2.6 KB history WebSocket allowance described above; the
// deleted REST probe and pull plumbing return part of it.
// Include the same ~4 KB Stats stream allowance described above.
// Include the same ~0.5 KB owned-socket pacing allowance described above.
// Include the same ~0.7 KB Diagnostic full-WebSocket allowance described above.
// The 2026-10 JS reformat re-baselining described above also applies here:
// 417.0 KB measured, headroom fixed at +6 KB.
// The Home quick target-weight sheet contributes the same ~6.3 KB of JS and
// ~1.4 KB of HTML source allowance described above; compressed budgets stay
// fixed.
if (htmlBytes + jsBytes > 431500) {
  throw new Error(`Web UI HTML+JS source exceeds the combined authoring budget (${htmlBytes + jsBytes} > 431500)`);
}
if (!/lang="en"/.test(html) || !codeIncludes(ui, 'role="switch"') ||
    !codeIncludes(ui, 'id="dActivator"') || !codeIncludes(ui, 'firstDropBeep') ||
    !codeIncludes(ui, 'paddleReturnReminderBeep') ||
    !codeIncludes(ui, 'buzzerScaleLostBeep') ||
    !codeIncludes(ui, 'buzzerAutoToManualGuardEndBeep') ||
    !codeIncludes(ui, 'buzzerManualNoScaleBeep') ||
    !codeIncludes(ui, 'buzzerScaleConnectedBeep') ||
    !codeIncludes(ui, 'scaleConnectedLed') ||
    !codeIncludes(ui, 'buzzerExtendedPulseRate') ||
    !codeIncludes(ui, 'buzzerSlowExtendedPulseRate') ||
    !html.includes('id="buzzerExtendedPulseRate"') ||
    !html.includes('id="buzzerSlowExtendedPulseRate"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt">Extended shot pulse<select id="buzzerExtendedPulseRate"') ||
    !html.includes('class="buzzerOpt scaleIncapableOpt">Slow extended pulse<select id="buzzerSlowExtendedPulseRate"') ||
    !css.includes('color-scheme:light dark') ||
    !css.includes('html,input,select,textarea{color-scheme:dark}') ||
    !css.includes('--in:#fafafa') ||
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
    codeIncludes(ui, 'querySelectorAll(\'.scaleIncapableOpt\').forEach(e=>{e.classList.toggle(\'fieldOff\',scaleOnly);e.querySelectorAll(\'input\').forEach') ||
    !codeIncludes(ui, 'alertOutputChannel') ||
    !codeIncludes(ui, 'buzzerSupported') ||
    !codeIncludes(ui, 'Output channel') ||
    !codeIncludes(ui, 'scale_priority') ||
    !codeIncludes(ui, 'Buzzer only') ||
    !codeIncludes(ui, 'class="fieldHint"') ||
    !codeIncludes(ui, 'id="bookooMuteOnBuzzerOnly"') ||
    !codeIncludes(ui, 'id="bookooConnectBeepLevel"') ||
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
if (!codeIncludes(ui, 'id="operationalWallS" type="number" min="5" max="60"') ||
    !codeIncludes(ui, 'Max BBW time (s)') ||
    !codeIncludes(ui, 'sToMs(') ||
    !codeIncludes(ui, 'rinseGestureMs:sToMs') ||
    !network.includes('Max BBW time must be from 5 to 60 s.')) {
  throw new Error('Max BBW time must be capped at 60 s in the UI and API messages');
}
if (!codeIncludes(ui, 'function rangeCheck(') ||
    !codeIncludes(ui, 'function showFieldError(') ||
    !codeIncludes(ui, 'samples × sample gap') ||
    !codeIncludes(ui, 'aria-live') ||
    !codeIncludes(ui, 'fieldError') ||
    !codeIncludes(ui, 'id="goalWeightG" type="number" min="10" max="200" step="1"') ||
    !codeIncludes(ui, 'validateNetworkClient') ||
    !codeIncludes(ui, 'validateDevicePasswordClient') ||
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
    !network.includes('"timezoneId"') ||
    !network.includes('"ntpServerPreset"') ||
    !network.includes('"ntpServerCustom"') ||
    !network.includes('\\"time\\":{') ||
    !codeIncludes(ui, 'id=ut') ||
    !codeIncludes(ui, 'id="ntpStatus"') ||
    !codeIncludes(ui, 'id="ntpServerPreset"') ||
    !codeIncludes(ui, 'id="ntpServerCustom"') ||
    !html.includes('id="staSsid" type="text"') ||
    !html.includes('id="ntpServerCustom" type="text"') ||
    !html.includes('id="presetRenameInput" type="text"') ||
    !codeIncludes(ui, 'id="syncTimeButton"') ||
    !codeIncludes(ui, 'id="autoRetare"') ||
    !codeIncludes(ui, 'id="fastExtractionGuardEnabled"') ||
    !codeIncludes(ui, 'id="avoidAccidentalTouchEnabled"') ||
    !codeIncludes(ui, 'id="maxRecoveryWeightG"') ||
    !codeIncludes(ui, 'id="minBbwBrewTimeS"') ||
    !codeIncludes(ui, 'Fast extraction guard') ||
    !codeIncludes(ui, 'id="slowExtractionGuardEnabled"') ||
    !codeIncludes(ui, 'id="minRecoveryWeightG"') ||
    !codeIncludes(ui, 'id="maxBbwBrewTimeS"') ||
    !codeIncludes(ui, 'Slow extraction guard') ||
    !codeIncludes(ui, 'id="retareWindowS"') ||
    !codeIncludes(ui, 'id="minimumCupWeightG"') ||
    !codeIncludes(ui, 'id="retareStabilitySamples"') ||
    !codeIncludes(ui, 'id="retareStabilityToleranceG"') ||
    !codeIncludes(ui, 'id="retareStabilityMaxGapS"') ||
    !codeIncludes(ui, 'id="retareStabilityMinDurationS"') ||
    !codeIncludes(ui, 'BBW protection (s)') ||
    !codeIncludes(ui, 'id="bbwProtectionS"') ||
    !codeIncludes(ui, 'Paddle reminder limit (min)') ||
    !codeIncludes(ui, 'id="paddleReturnReminderMaxDurationMin"') ||
    !codeIncludes(ui, 'paddleReturnReminderMaxDurationMs:Math.round(') ||
    codeIncludes(ui, 'Up to 120 shots') ||
    !codeIncludes(ui, '/api/v1/time/sync') ||
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
if (!codeIncludes(ui, 'id="bullseyeMelodyEnabled"') ||
    !codeIncludes(ui, 'id="bullseyeRtttl" maxlength="500"') ||
    !codeIncludes(ui, 'Bullseye melody') ||
    !codeIncludes(js, 'bullseyeMelodyEnabled') ||
    !codeIncludes(js, 'bullseyeRtttl') ||
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
if (!codeIncludes(ui, "scaleConnectedLed:$('scaleConnectedLed').checked") ||
    !codeIncludes(ui, "'scaleConnectedLed'") ||
    !network.includes('\\"scaleConnectedLed\\":%s') ||
    !firmware.includes('serviceScaleConnectedLed') ||
    !firmware.includes('SCALE_CONNECTED_LED_GPIO') ||
    !domain.includes('bool scaleConnectedLed = true')) {
  throw new Error('Scale-connected GPIO LED must be wired through Settings, status/settings, and firmware');
}

if (!codeIncludes(ui, 'id="soundAlertsEnabled"') ||
    codeIncludes(ui, 'id="homeSoundAlertsEnabled"') ||
    codeIncludes(ui, 'id="homeAlertsSub"') ||
    codeIncludes(ui, "setHomeSub('homeAlertsSub'") ||
    codeIncludes(ui, 'function formatAlertsChannel(') ||
    codeIncludes(ui, "persistHomeGuard('homeSoundAlertsEnabled'") ||
    !codeIncludes(ui, 'soundAlertsEnabled:$(\'soundAlertsEnabled\').checked') ||
    codeIncludes(ui, 'p.soundAlertsEnabled=$(\'homeSoundAlertsEnabled\').checked') ||
    codeIncludes(ui, "k!=='soundAlertsEnabled'") ||
    codeIncludes(js, "keys[0]==='soundAlertsEnabled'") ||
    !(codeIncludes(ui, "typeof c.soundAlertsEnabled==='boolean'") ||
      codeIncludes(ui, "'boolean'==typeof c.soundAlertsEnabled"))) {
  throw new Error('Sound alerts must be controlled from Settings, not Home Quick Settings');
}

if (html.indexOf('<summary>Brew by Weight</summary>') >
        html.indexOf('<summary>Cup protection</summary>') ||
    html.indexOf('<summary>Cup protection</summary>') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    !codeIncludes(ui, 'id="cupProtectionEnabled"') ||
    html.indexOf('id="cupProtectionEnabled"') >
        html.indexOf('id="stopIfCupRemoved"') ||
    html.indexOf('id="stopIfCupRemoved"') >
        html.indexOf('id="requireCupToStart"') ||
    html.indexOf('id="requireCupToStart"') >
        html.indexOf('<summary>Fast extraction guard</summary>') ||
    !codeIncludes(ui, 'id="stopIfCupRemoved"') ||
    !codeIncludes(ui, 'id="requireCupToStart"') ||
    codeIncludes(ui, 'id="cupPresentWeightG"') ||
    html.includes('id="cupPresentWeightG"') ||
    html.indexOf('id="cupRemovedWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.includes('id="requireCupToStart" type="checkbox" checked') ||
    !codeIncludes(ui, 'id="cupProtectionEnabled" type="checkbox" checked> Cup protection') ||
    !codeIncludes(ui, 'cupProtectOpt') ||
    !codeIncludes(ui, 'If the cup was already tared before connection, lift it and place it again.') ||
    !codeIncludes(ui, 'id="homeCupProtectionEnabled"') ||
    html.indexOf('id="homeAvoidAccidentalTouchEnabled"') >
        html.indexOf('id="homeCupProtectionEnabled"') ||
    html.indexOf('id="homeCupProtectionEnabled"') >
        html.indexOf('id="homePresetBlock"') ||
    !codeIncludes(ui, "persistHomeGuard('homeCupProtectionEnabled'") ||
    !codeIncludes(ui, "'cupProtectionEnabled',1") ||
    !network.includes('cupProtectionEnabled') ||
    !network.includes('stopIfCupRemoved') ||
    !network.includes('requireCupToStart') ||
    !network.includes('cupRemovedWeightG')) {
  throw new Error('Cup protection master must precede Stop if cup is removed and Require cup to start; Home mirrors the master after accidental touch');
}

if (codeIncludes(ui, 'bleCompanionEnabled') ||
    codeIncludes(ui, 'bleCompanionPanel') ||
    codeIncludes(ui, '/api/v1/admin/ble-compat') ||
    codeIncludes(ui, 'Companion characteristics') ||
    codeIncludes(ui, 'BLE companion') ||
    codeIncludes(ui, 'ensureBleScanPanel') ||
    network.includes('bleCompanion') ||
    network.includes('WebCommandType::BLE_COMPAT') ||
    networkHeader.includes('bleCompatHandler') ||
    firmwareCore.includes('persistBleCompanionEnabled') ||
    !codeIncludes(ui, '<legend>') || !codeIncludes(ui, 'Power management') ||
    !codeIncludes(ui, 'ESP32 power management') ||
    !codeIncludes(ui, 'bleScanIntensity') ||
    !codeIncludes(ui, 'BLE scan mode') ||
    codeIncludes(ui, 'Aggressive 100%') ||
    codeIncludes(ui, 'Normal 50%') ||
    codeIncludes(ui, 'Light 25%') ||
    !codeIncludes(ui, 'How much radio time is spent searching for Bluetooth espresso scales') ||
    !codeIncludes(ui, "scanIntensity:wanted") ||
    codeIncludes(ui, 'bleScanBackoff') ||
    codeIncludes(ui, 'Idle scan backoff') ||
    codeIncludes(ui, "backoffMin:wanted") ||
    codeIncludes(ui, 'bleScanBoost') ||
    codeIncludes(ui, 'Scan boost on machine use') ||
    codeIncludes(ui, "boostMin:wanted") ||
    !codeIncludes(ui, 'bleEnabled') ||
    !codeIncludes(ui, 'Enable Bluetooth') ||
    !codeIncludes(ui, "enabled:wanted") ||
    !codeIncludes(ui, 'Master switch that enables or disables Bluetooth connections to scales.') ||
    !network.includes('BleScanCommandPayload::ENABLED') ||
    !firmwareCore.includes('void persistBleScanEnabled') ||
    !firmwareCore.includes('applyLiveBleEnabled') ||
    !codeIncludes(ui, '/api/v1/admin/ble-scan') ||
    !codeIncludes(ui, "method:'PUT'") ||
    !network.includes('scanIntensity') ||
    network.includes('backoffMin') ||
    network.includes('boostMin') ||
    !network.includes('WebCommandType::BLE_SCAN_INTENSITY') ||
    !network.includes('command.type = WebCommandType::BLE_SCAN_INTENSITY') ||
    !firmwareCore.includes('void persistBleScanIntensity') ||
    firmwareCore.includes('void persistBleScanBackoff') ||
    firmwareCore.includes('void persistBleScanBoost') ||
    !networkHeader.includes('bleScanHandler')) {
    throw new Error('Power management Admin controls must keep live scan mode and the BLE master switch with the timed backoff/boost controls removed');
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
    !codeIncludes(js, 'developmentMode') ||
    !codeIncludes(js, "'development'in s") ||
    !codeIncludes(js, 'if(!developmentMode)syncAdminSessionUi(false)') ||
    !codeIncludes(js, 'if(developmentMode||$(\'uiOverridePanel\'))return;') ||
    !codeIncludes(js, "classList.toggle('devBuild',developmentMode)") ||
    // Session visibility is driven by the actual unlock state, and
    // development builds treat administration as always unlocked; assert the
    // behavior (visible when either condition holds), not minified names.
    !/const\s+on\s*=\s*!!unlocked\s*\|\|\s*developmentMode/.test(js) ||
    !codeIncludes(js, "if(R.developmentActive())return;") ||
    !rawShellHtml.includes('class="{{webui-meta:development-class}}"') ||
    !css.includes('body.devBuild #adminLockPanel')) {
  throw new Error(
      'SHOT_STOPPER_DEVELOPMENT must default off, make administration public by compile flag, and never flash the locked admin UI');
}
if (!domainCore.includes('#ifndef SHOT_STOPPER_ENABLE_JTAG') ||
    !domainCore.includes('#define SHOT_STOPPER_ENABLE_JTAG 0') ||
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
if (codeIncludes(ui, 'authenticatedOnly') ||
    codeIncludes(ui, "s.setItem('shotStopperToken'") ||
    codeIncludes(ui, 'pageNav authenticatedOnly') ||
    codeIncludes(ui, "authenticated()&&known") ||
    !codeIncludes(ui, 'function knownPath(') ||
    !codeIncludes(ui, 'class="brand"') ||
    !codeIncludes(ui, 'class="brandMark"') ||
    !codeIncludes(ui, '<small>Open</small>Brew by Weight') ||
    !codeIncludes(ui, 'href="/" data-route="/"') ||
    !codeIncludes(ui, "querySelectorAll('a[data-route]')") ||
    !codeIncludes(ui, 'ensureView') ||
    !codeIncludes(ui, '/partials/') ||
    !network.includes('HTTPD_404_NOT_FOUND') ||
    !network.includes('notFoundHandler')) {
  throw new Error('Web UI must expose public SPA routes and redirect unknown paths to /');
}
if (html.includes('id="rememberMe"') ||
    codeIncludes(js, 'rememberMe:r') ||
    codeIncludes(js, "s.setItem('shotStopperToken'") ||
    codeIncludes(js, 'function clearAuth()') ||
    network.includes('jsonBoolean(root, "rememberMe", rememberMe)') ||
    network.includes('createSession(token, csrf, rememberMe)') ||
    network.includes('uiAuthenticated') ||
    networkHeader.includes('SESSION_REMEMBER_MS')) {
  throw new Error(
      'Web UI must not use login tokens; exclusive WebUI claim owns the session');
}
const statusSection = html.match(/<details id="machineRow" class="lampRow">([\s\S]*?)<\/details>/);
const scaleSection = html.match(/<details id="scaleRow" class="lampRow">([\s\S]*?)<\/details>/);
