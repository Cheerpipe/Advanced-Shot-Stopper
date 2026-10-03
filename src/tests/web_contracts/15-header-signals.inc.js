{
  const assert = require('assert').strict, vm = require('vm');
  const events = {}, buttons = ['wifi', 'bluetooth'].map(kind => ({
    dataset: {}, attributes: {'aria-controls': kind + 'Details', 'aria-expanded': 'false'},
    setAttribute(name, value) { this.attributes[name] = value; },
    getAttribute(name) { return this.attributes[name]; },
    addEventListener(name, fn) { this[name] = fn; }, focus() { this.focused = true; },
  }));
  const elements = Object.fromEntries(['wifi', 'bluetooth'].flatMap((kind, i) => [
    [kind + 'Signal', buttons[i]], [kind + 'Details', {hidden: true}],
    [kind + 'Name', {}], [kind + 'Detail', {}],
  ]));
  const start = runtimeJs.indexOf('function updateHeaderSignals(');
  assert(start >= 0, 'Shared header must render cached signal status');
  const context = vm.createContext({$: id => elements[id],
    document: {querySelectorAll: () => buttons, addEventListener: (name, fn) => {events[name] = fn;}},
    window: {addEventListener: (name, fn) => {events[name] = fn;}},
  });
  vm.runInContext(runtimeJs.slice(start, runtimeJs.indexOf('\nlet homeBootDone=', start)).replace(/^export /gm, ''), context);
  for (const [rssi, level] of [[-128, '1'], [-81, '1'], [-80, '2'], [-61, '2'], [-60, '3'], [0, '3'], [null, 'unknown'], [NaN, 'unknown'], [Infinity, 'unknown'], [-129, 'unknown'], [1, 'unknown'], ['-52', 'unknown']]) {
    context.updateHeaderSignals({connections: {wifiConnected: true, wifiRssi: rssi, bluetoothConnected: true, bluetoothRssi: rssi}});
    for (const kind of ['wifi', 'bluetooth']) assert.equal(elements[kind + 'Signal'].dataset.level, level, String(rssi));
  }
  context.updateHeaderSignals({connections: {wifiConnected: true, wifiRssi: -52, bluetoothConnected: false, bluetoothRssi: -52}});
  assert.equal(elements.wifiSignal.dataset.level, '3');
  assert.equal(elements.bluetoothSignal.dataset.level, '0', 'Disconnect overrides an old reading');
  assert.equal(elements.bluetoothSignal.attributes['aria-label'], 'Bluetooth: Scale disconnected');
  context.updateHeaderSignals({connections: {wifiConnected: false, wifiRssi: -52}});
  assert.equal(elements.wifiSignal.dataset.level, '0', 'STA disconnected does not imply a strong AP signal');
  context.updateHeaderSignals({snapshotStale: true, connections: {wifiConnected: true, wifiRssi: -52, bluetoothConnected: true, bluetoothRssi: -52}});
  assert.equal(elements.wifiSignal.dataset.level, 'unknown');
  assert.equal(elements.bluetoothSignal.dataset.level, 'unknown');
  context.updateHeaderSignals();
  assert.equal(elements.wifiSignal.title, 'Wi-Fi: Signal unavailable', 'Transport failures clear the displayed measurement');
  assert.equal(elements.bluetoothSignal.title, 'Bluetooth: Signal unavailable');
  context.updateHeaderSignals({connections: {wifiConnected: true, wifiRssi: -60, bluetoothConnected: true, bluetoothRssi: -80}});
  assert.equal(elements.wifiSignal.title, 'Wi-Fi: Strong · -60 dBm');
  assert.equal(elements.bluetoothSignal.title, 'Bluetooth: Medium · -80 dBm');
  context.updateHeaderSignals({connections: {wifiConnected: true, wifiRssi: null, wifiName: 'Cafe "A"\\B', bluetoothConnected: true, bluetoothRssi: -52, bluetoothName: '<img src=x onerror=alert(1)>'}});
  assert.equal(elements.wifiName.textContent, 'Cafe "A"\\B', 'Connected names survive missing RSSI');
  assert.equal(elements.bluetoothName.textContent, '<img src=x onerror=alert(1)>', 'Identity is rendered as text');
  assert.equal(elements.wifiDetail.textContent, 'Signal unavailable');
  context.initHeaderSignals();
  buttons[0].click();
  assert.equal(elements.wifiDetails.hidden, false);
  assert.equal(elements.bluetoothDetails.hidden, true);
  assert.equal(buttons[0].attributes['aria-expanded'], 'true');
  events.click({target: {closest: () => elements.wifiDetails}});
  assert.equal(elements.wifiDetails.hidden, false, 'Clicks inside the popup retain it');
  buttons[1].click();
  assert.equal(elements.wifiDetails.hidden, true);
  assert.equal(buttons[0].attributes['aria-expanded'], 'false');
  assert.equal(elements.bluetoothDetails.hidden, false, 'Each icon opens its own popup');
  context.updateHeaderSignals({connections: {bluetoothConnected: false, bluetoothName: 'Old scale', bluetoothRssi: -52}});
  assert.equal(elements.bluetoothName.textContent, 'None');
  assert.equal(elements.bluetoothDetail.textContent, 'Scale disconnected', 'Open popup refreshes on disconnect');
  events.keydown({key: 'Escape'});
  assert.equal(elements.bluetoothDetails.hidden, true);
  assert.equal(buttons[1].focused, true, 'Escape returns focus to the trigger');
  buttons[0].click();buttons[0].click();
  assert.equal(elements.wifiDetails.hidden, true, 'Repeated tap closes the popup');
  buttons[0].click();events.click({target: {closest: () => null}});
  assert.equal(elements.wifiDetails.hidden, true, 'Outside click closes');
  buttons[0].click();events.popstate();
  assert.equal(elements.wifiDetails.hidden, true, 'History navigation closes');
  context.updateHeaderSignals({snapshotStale: true, connections: {wifiConnected: true, wifiName: 'Old SSID'}});
  assert.equal(elements.wifiName.textContent, '—');
  assert.equal(elements.wifiDetail.textContent, 'Signal unavailable');
  assert(appJsSource.includes('R.initHeaderSignals();'));
  assert(rawRuntimeJs.includes('applyCommonStatus(s){updateHeaderSignals(s);'));
  assert(rawRuntimeJs.includes('noteReachFail(err,force){clearCupWeights();updateHeaderSignals();'));
  for (const file of ['network/ShotStopperStatus.inc', 'diagnostics/ShotStopperNetworkDiagnostics.inc']) {
    const source = fs.readFileSync(path.join(sketchDir, file), 'utf8');
    for (const field of ['wifiConnected', 'wifiRssi', 'bluetoothConnected', 'bluetoothRssi', 'wifiName', 'bluetoothName']) assert(source.includes('\\"' + field + '\\"'), file + ': ' + field);
    assert(source.includes('escapeConnectedScaleName(control, g_work->scaleHistory,'), 'Both envelopes use the connected identity resolver');
    assert(source.includes('escapeJsonString(network.staState == StaState::CONNECTED ? network.staSsid : ""'), 'Preserve escaped SSID only while connected');
  }
  const network = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.cpp'), 'utf8');
  assert(network.includes('entry.friendlyName[0] ? entry.friendlyName : entry.name'), 'Friendly name precedes advertised BLE name');
  assert(network.includes('preferredScaleMacEqual(entry.mac, control.connectedScaleMac)'), 'Name must match the connected scale');
  const preview = require('../../scripts/preview_web_ui.js').renderHome();
  for (const kind of ['wifi', 'bluetooth']) {
    assert.equal((preview.match(new RegExp('id="' + kind + 'Signal"', 'g')) || []).length, 1, 'Preview must retain exactly one header');
    const svg = source => source.match(new RegExp('id="' + kind + 'Signal"[\\s\\S]*?(<svg[\\s\\S]*?</svg>)'))[1];
    assert.equal(svg(rawShellHtml), svg(preview), 'Firmware must ship the accepted option 1 drawing');
  }
}
{
  const assert = require('assert').strict, vm = require('vm'), hidden = new Set(['hidden']);
  const link = {classList: {toggle: (name, on) => on ? hidden.add(name) : hidden.delete(name)}};
  const context = vm.createContext({compatMode: false,
    document: {querySelector: selector => {assert.equal(selector, '[data-route="/diagnostic"]'); return link;}}});
  const start = rawRuntimeJs.indexOf('let diagnosticVisible=false;');
  vm.runInContext(rawRuntimeJs.slice(start, rawRuntimeJs.indexOf('function applyCompatibilityChrome(', start))
    .replace(/^export /gm, ''), context);
  context.applyDiagnosticNavigation();
  assert(hidden.has('hidden'), 'Diagnostic stays hidden before configuration arrives');
  context.applyDiagnosticNavigation({diagnosticPageVisible: true});
  assert(!hidden.has('hidden'), 'Admin enables the shared mobile/desktop Diagnostic link');
  context.applyDiagnosticNavigation({});
  assert(!hidden.has('hidden'), 'Unrelated status envelopes preserve visibility');
  context.compatMode = true; context.applyDiagnosticNavigation();
  assert(hidden.has('hidden'), 'Compatibility mode hides Diagnostic even when enabled');
  context.compatMode = false; context.applyDiagnosticNavigation();
  assert(!hidden.has('hidden'), 'Returning from compatibility mode restores saved visibility');
  context.applyDiagnosticNavigation({diagnosticPageVisible: false});
  assert(hidden.has('hidden'), 'Admin disables the same link without leaving a second copy');
  assert.equal((rawShellHtml.match(/data-route="\/diagnostic"/g) || []).length, 1);
  const nav = rawShellHtml.match(/<nav class="pageNav"[\s\S]*?<\/nav>/)[0];
  assert.equal((nav.match(/aria-hidden="true"/g) || []).length, 6, 'Every icon retains a translated text label');
  assert(nav.includes('data-route="/diagnostic" class="hidden"'), 'Hidden until device configuration is known');

  const values = {}, events = {}, mobile = {matches: true, addEventListener: (name, fn) => {events.resize = fn;}};
  const header = vm.createContext({document: {body: {style: {setProperty: (name, value) => {values[name] = value;}}}},
    window: {scrollY: 0, matchMedia: () => mobile, addEventListener: (name, fn) => {events[name] = fn;}}});
  const headerStart = appJsSource.indexOf('const mobileHeader=');
  vm.runInContext(appJsSource.slice(headerStart, appJsSource.indexOf("window.addEventListener('popstate'", headerStart)), header);
  for (const [scroll, progress] of [[0, 0], [30, .25], [60, .5], [120, 1], [300, 1], [-20, 0]]) {
    header.window.scrollY = scroll; events.scroll();
    assert.equal(values['--header-progress'], progress, 'Header shrinks continuously within scroll bounds');
  }
  mobile.matches = false; header.window.scrollY = 60; events.scroll();
  assert.equal(values['--header-progress'], 0, 'Desktop scrolling does not drive the mobile header');
  mobile.matches = true; events.resize();
  assert.equal(values['--header-progress'], .5, 'Switching to mobile applies the current scroll position');
}
{
  const assert = require('assert').strict, vm = require('vm');
  const root = {dataset: {}}, app = {clientWidth: 760}, callbacks = {};
  const links = [42, 34, 48, 60, 78, 46].map(width => ({
    hidden: false, suppressed: false, width, textContent: 'Section',
  }));
  const nav = {setAttribute() {}, querySelectorAll: () => links,
    get clientWidth() {return app.clientWidth - 2;},
    get scrollWidth() {
      const required = 4 + links.filter(link => !link.hidden && !link.suppressed)
        .reduce((width, link) => width + link.width + 16 + (root.dataset.navLayout === 'icons' ? 32 : 0), 0);
      return Math.max(this.clientWidth, required);
    },
  };
  const context = vm.createContext({pageNav: nav, __WEBUI_TEXT__: key => key,
    document: {documentElement: root, getElementById: id => {assert.equal(id, 'app'); return app;},
      fonts: {ready: {then: fn => {callbacks.fonts = fn;}}}},
    ResizeObserver: class {constructor(fn) {callbacks.resize = fn;} observe(target) {assert.equal(target, app);}},
    MutationObserver: class {constructor(fn) {callbacks.visibility = fn;} observe(target, options) {
      assert.equal(target, nav); assert.equal(options.subtree, true);
      assert.deepEqual(Array.from(options.attributeFilter), ['class', 'hidden']);
    }},
  });
  const start = appJsSource.indexOf('function updateNavigationLayout()');
  vm.runInContext(appJsSource.slice(start, appJsSource.indexOf('const msgEl=', start)), context);
  assert.equal(root.dataset.navLayout, 'icons', 'Wide header retains section icons and names');
  for (const [width, expected] of [[602, 'icons'], [601, 'text'], [410, 'text'], [409, 'bottom'], [760, 'icons']]) {
    app.clientWidth = width; callbacks.resize();
    assert.equal(root.dataset.navLayout, expected, 'No wrapping at width ' + width);
  }
  app.clientWidth = 390; callbacks.resize();
  assert.equal(root.dataset.navLayout, 'bottom');
  links[4].suppressed = true; callbacks.visibility();
  assert.equal(root.dataset.navLayout, 'text', 'Hidden Diagnostics frees room without a second navigation');
  links.forEach((link, index) => {link.suppressed = index !== 5;}); callbacks.visibility();
  assert.equal(root.dataset.navLayout, 'icons', 'Compatibility visibility measures only the Admin link');
  links.forEach(link => {link.suppressed = false;}); callbacks.visibility();
  assert.equal(root.dataset.navLayout, 'bottom', 'Restoring permissions restores the width requirement');
  links[4].hidden = true; callbacks.visibility();
  assert.equal(root.dataset.navLayout, 'text', 'The hidden attribute also removes a destination from measurement');
  links[0].width += 100; callbacks.fonts();
  assert.equal(root.dataset.navLayout, 'bottom', 'Changed font metrics are measured again');
  assert(css.includes('bottom:var(--nav-offset)'), 'Save and action bars follow the navigation clearance');
  assert(!css.includes('top:-3.85rem'), 'Desktop header remains visible as in the accepted mockup');
}
