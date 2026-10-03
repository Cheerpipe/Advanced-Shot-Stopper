{
  const assert = require('assert').strict, vm = require('vm');
  const elements = Object.fromEntries(['wifiSignal', 'bluetoothSignal'].map(id => [id, {
    dataset: {}, attributes: {}, setAttribute(name, value) { this.attributes[name] = value; },
  }]));
  const start = runtimeJs.indexOf('function updateHeaderSignals(');
  assert(start >= 0, 'Shared header must render cached signal status');
  const context = vm.createContext({$: id => elements[id]});
  vm.runInContext(runtimeJs.slice(start, runtimeJs.indexOf('\nlet homeBootDone=', start)), context);
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
  assert(rawRuntimeJs.includes('applyCommonStatus(s){updateHeaderSignals(s);'));
  assert(rawRuntimeJs.includes('noteReachFail(err,force){clearCupWeights();updateHeaderSignals();'));
  for (const file of ['network/ShotStopperStatus.inc', 'diagnostics/ShotStopperNetworkDiagnostics.inc']) {
    const source = fs.readFileSync(path.join(sketchDir, file), 'utf8');
    for (const field of ['wifiConnected', 'wifiRssi', 'bluetoothConnected', 'bluetoothRssi']) assert(source.includes('\\"' + field + '\\"'), file + ': ' + field);
  }
  const preview = require('../../scripts/preview_web_ui.js').renderHome();
  for (const kind of ['wifi', 'bluetooth']) {
    assert.equal((preview.match(new RegExp('id="' + kind + 'Signal"', 'g')) || []).length, 1, 'Preview must retain exactly one header');
    const svg = source => source.match(new RegExp('id="' + kind + 'Signal"[\\s\\S]*?(<svg[\\s\\S]*?</svg>)'))[1];
    assert.equal(svg(rawShellHtml), svg(preview), 'Firmware must ship the accepted option 1 drawing');
  }
}
