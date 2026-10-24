// The Settings page rides the owned socket as a view-scoped subscription
// (D19) carrying a thin overlay over Home's core state stream (D17): the
// harness executes the real runtime trio against synthetic frames and a
// standing Home model — cold entry subscribes and waits, the painters see
// the composed {...home, ...settings} model, stale request ids and
// off-view frames never reach the model, a dirty form absorbs its own
// save's echo without writing, leaving the view unsubscribes, and the
// Admin poll is untouched.
{
  const assert = require('assert').strict, vm = require('vm');
  const validator = runtimeJs.slice(runtimeJs.indexOf('function statusPageOk('),
    runtimeJs.indexOf('async function loadStatus('));
  const source = 'var formRev = 0, configLoaded = false, homeAccPendingId = 0, presetServerActiveId = 0;\n' + runtimeStateDecl +
    'let homeFrame=null, homeStale=false, homeReady=Promise.resolve(true), homeResolve=()=>{};' +
    runtimeJs.slice(runtimeJs.indexOf('function statusStreamFrame('),
      runtimeJs.indexOf('function renderHomeStream(')) +
    blockAt(runtimeJs, 'async function loadHomeStatus(') +
    runtimeJs.slice(runtimeJs.search(/let\s+shotWs\s*=/),
      runtimeJs.indexOf('function setHomeSub(')) +
    blockAt(runtimeJs, 'function loadSettingsConfig(');
  const sockets = [], timers = new Map(), applied = [], elements = new Map(), errors = [];
  let timer = 0, owner = true, reachOk = 0, mount;
  class Socket {
    static OPEN = 1;
    constructor() { this.readyState = 0; this.sent = []; sockets.push(this); }
    send(value) { this.sent.push(JSON.parse(value)); }
    open() { this.readyState = 1; this.onopen(); }
    close(code = 1006) {
      if (this.readyState === 3) return;
      this.readyState = 3;
      this.onclose?.({code});
    }
  }
  const element = id => elements.get(id) ||
    elements.set(id, {value: '', checked: false}).get(id);
  const context = {
    WebSocket: Socket, location: {protocol: 'http:', host: 'device.local'},
    document: {hidden: false, addEventListener() {}}, window: {addEventListener() {}},
    setTimeout: (fn, delay) => { timers.set(++timer, {fn, delay}); return timer; },
    clearTimeout: id => timers.delete(id), requestAnimationFrame: () => 1,
    webUiPollingActive: () => owner, webUiClientId: '0123456789abcdef', webUiPowerSeconds: () => 30,
    activeView: 'settings', viewSeq: 1, setupSession: false, sessionShotLive: () => false,
    viewReady: new Promise(resolve => { mount = resolve; }), lastStatusAt: 0,
    performance: {now: () => 1234}, statusUtcAnchorSec: 0, statusUtcAnchorAt: 0,
    viewStatusHandlers: {settings: value => {
      applied.push(value);
      // The Settings form repaints through the production load guard.
      context.loadSettingsConfig(value.config);
    }},
    applyCommonStatus() {}, setMutable() {},
    noteReachOk: () => { reachOk++; }, noteReachFail() {}, invalidateHomeStream() {},
    resetWebUiInactivity() {}, deactivateWebUi: () => { owner = false; },
    hideHomeBoot() {}, clearCupWeights() {}, updateHeaderSignals() {},
    updateHomeAdminActions() {}, setHomeSub() {}, updateConfigGroups() {},
    updateScalePreferenceOptions() {}, extRate: value => value, filterResets: [],
    $: element, console: {error: e => errors.push(e)},
  };
  vm.runInNewContext(validator + source, context);
  // The layered model (D17): Home carries the identity envelope, shared
  // config, revision, scale preferences and the Micra pill state; the
  // settings frame overlays only settings-owned fields.
  const homeStatus = () => ({firmwareVersion: 'v1', bootId: 7, configMutable: true,
    snapshotStale: false, diagnosticPageVisible: true, machineType: 'paddle',
    config: {revision: 5, goalWeightG: 36, soundAlertsEnabled: true,
      timezoneId: 'UTC', scaleMacCacheMode: 'prefer'},
    quickRevert: {overrides: 0, remainingS: 0}, lastCommand: {requestId: 0, state: 'idle'},
    cycle: {active: false},
    scale: {preferredMac: 'aa:bb:cc:dd:ee:ff', preferredName: 'Bar',
      macCachePauseRemainingMs: 0, observedWeightG: 4.2, history: null},
    lineaMicra: {accountConfigured: false, powerState: 'UNKNOWN', readiness: 'unknown'}});
  const config = {dripDelayMs: 3000, postTareBaselineGraceMs: 2000,
    bullseyeRtttl: 'tune', rinseDurationMs: 5000};
  const status = () => ({buzzerSupported: true, config: {...config},
    scale: {history: []}, scaleLoss: {projectedS: 31, source: 'fixed'}});
  const flatten = (value, prefix = '', output = {}) => {
    for (const [key, item] of Object.entries(value)) {
      const name = prefix + key;
      if (item && typeof item === 'object' && !Array.isArray(item)) flatten(item, name + '.', output);
      else output[name] = item;
    }
    return output;
  };
  const snapshot = request => ({v: 1, type: 'settings', boot: 7, snapshot: true,
    request, changes: flatten(status())});
  // The reconciled gate (D17): the thin overlay passes without any
  // Home-owned field; Home's own gate still requires its envelope.
  assert(context.statusPageOk('settings', status()),
      'the settings gate requires only settings-owned fields');
  assert(!context.statusPageOk('settings', {...status(), buzzerSupported: undefined}),
      'the settings gate still fails closed without its own fields');
  // A snapshot the page cannot trust never becomes a model.
  for (const bad of [{...snapshot(1), changes: {...flatten(status()), buzzerSupported: undefined}},
      {...snapshot(1), changes: {...flatten(status()), 'scale.history': undefined}}])
    assert.throws(() => context.statusStreamFrame(null, bad),
        'an incomplete settings snapshot must fail closed');
  (async () => {
    vm.runInContext('homeFrame = {boot: 7, status: ' + JSON.stringify(homeStatus()) + '}', context);
    // Cold entry waits for markup and an open socket, subscribes exactly once,
    // and settles only on the snapshot of its own request id; the painters
    // see the composed model (overlay fields and Home fields together).
    const pending = context.startSettingsStream();
    mount();
    await Promise.resolve();
    const socket = sockets[0];
    assert.equal(socket.sent.filter(op => op.op === 'settings').length, 0,
        'the subscription waits for an open socket');
    socket.open();
    assert.deepEqual(socket.sent[1], {op: 'settings', on: true, request: 1},
        'an open socket replays the wanted settings subscription before liveness');
    socket.onmessage({data: JSON.stringify(snapshot(1))});
    assert.equal(await pending, true);
    assert.equal(applied.at(-1).config.dripDelayMs, 3000, 'the overlay field paints');
    assert.equal(applied.at(-1).config.goalWeightG, 36, 'Home field reaches the painter composed');
    assert.equal(applied.at(-1).scale.preferredMac, 'aa:bb:cc:dd:ee:ff',
        'Home scale preferences compose under the overlay history');
    assert.equal(element('goalWeightG').value, '36.0');
    assert.equal(context.formRev, 5, 'the first paint loads the form and records its revision');
    // A delta with one changed setting applies alone; the snapshot's identity
    // and every unchanged field survive it.
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 1, changes: {'config.dripDelayMs': 5000}})});
    assert.equal(applied.at(-1).config.dripDelayMs, 5000);
    assert.equal(applied.at(-1).config.revision, 5);
    // The hardware incident class (2026-10-23): an empty RTTTL string must
    // parse, merge, and reach the Settings view as "" — never a broken frame.
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 1, changes: {'config.bullseyeRtttl': ''}})});
    assert.strictEqual(applied.at(-1).config.bullseyeRtttl, '');
    assert.deepEqual(errors, []);
    // Stale request ids (a superseded subscription) never reach the model.
    let stale = applied.length;
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 99, changes: {'config.dripDelayMs': 9000}})});
    assert.equal(applied.length, stale, 'a stale request id must be discarded');
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 1, changes: {'config.rinseDurationMs': 6000}})});
    assert.equal(applied.at(-1).config.rinseDurationMs, 6000,
        'the overlay model carries the changed setting');
    assert.equal(element('rinseDurationS').value, '5',
        'without a revision move the loaded form is not reloaded');
    assert.equal(context.formRev, 5, 'an overlay-only change keeps the composed revision');
    // The revision is Home's now (D17): a Home-side bump reloads the clean
    // form on the next settings paint (the return-to-clean recovery).
    vm.runInContext('homeFrame.status.config.revision = 6; homeFrame.status.config.goalWeightG = 40', context);
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 1, changes: {'config.dripDelayMs': 4000}})});
    assert.equal(element('goalWeightG').value, '40.0');
    assert.equal(element('rinseDurationS').value, '6');
    assert.equal(context.formRev, 6, 'a clean form follows the Home-side revision');
    // The echo path (D11): a dirty form absorbs its own save's confirmation
    // (its revision bump arrives on Home) without reloading — and frames
    // never write, so no REST path exists here.
    vm.runInContext('uiState.configDirty = true; homeFrame.status.config.revision = 7', context);
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 1, changes: {'config.dripDelayMs': 7000}})});
    assert.equal(element('dripDelayS').value, '4',
        'a dirty form is never clobbered by its own echo');
    assert.equal(context.formRev, 6);
    // Returning clean applies the device state on the next paint.
    vm.runInContext('uiState.configDirty = false', context);
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 1, changes: {'config.dripDelayMs': 7000}})});
    assert.equal(element('dripDelayS').value, '7');
    assert.deepEqual(errors, []);
    // Warm entry repaints the standing model without any op.
    const ops = socket.sent.length, renders = applied.length;
    assert.equal(await context.startSettingsStream(), true);
    assert.equal(socket.sent.length, ops, 'warm entry sends no op');
    assert.equal(applied.length, renders + 1, 'warm entry repaints the cached model');
    // A corrupt frame fails closed: resync, no repaint, recovery by snapshot.
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 1, changes: {'config.__proto__.x': 1}})});
    assert.equal(socket.sent.at(-1).op, 'resync');
    socket.onmessage({data: JSON.stringify(
        {type: 'settings', request: 1, changes: {'config.dripDelayMs': 4000}})});
    assert.equal(applied.length, renders + 1, 'non-snapshot frames wait out the resync');
    socket.onmessage({data: JSON.stringify(snapshot(1))});
    assert.equal(applied.length, renders + 2, 'the resync snapshot recovers the stream');
    // D19: leaving the view unsubscribes (the bare op the router accepts),
    // the standing model is dropped, and no frame is applied while off-view —
    // not even the current request id's snapshot.
    context.stopSettingsStream();
    assert.deepEqual(socket.sent.at(-1), {op: 'settings', on: false},
        'leaving the view sends the bare unsubscribe');
    socket.onmessage({data: JSON.stringify(snapshot(1))});
    assert.equal(applied.length, renders + 2, 'an off-view frame never reaches the model');
    // Re-entry re-subscribes (a fresh thin snapshot) and settles on it.
    const reentry = context.startSettingsStream();
    await Promise.resolve();
    assert.deepEqual(socket.sent.at(-1), {op: 'settings', on: true, request: 2},
        'a re-entry subscribes again');
    socket.onmessage({data: JSON.stringify(snapshot(2))});
    assert.equal(await reentry, true);
    // The forced refresh (the stale-revision retry's path) re-subscribes
    // deterministically; socket loss settles its pending wait false, and the
    // view-wanted subscription replays on the reconnect.
    const forced = context.resyncSettingsStream();
    await Promise.resolve();
    assert.equal(socket.sent.at(-1).request, 3, 'a forced refresh re-subscribes');
    socket.close();
    assert.equal(await forced, false, 'socket loss settles the pending wait');
    const retry = [...timers.values()].find(t => t.delay >= 400 && t.delay <= 600);
    assert(retry, 'socket loss must schedule a reconnect');
    retry.fn();
    sockets[1].open();
    assert.deepEqual(sockets[1].sent[1], {op: 'settings', on: true, request: 4},
        'the reconnect replays the view-wanted subscription');
    // A superseded generation's frames are discarded: only the newest request
    // id may touch the model, and its snapshot settles the forced entry. The
    // re-entry and forced entry each paint twice (the frame's own paint plus
    // the post-homeReady repaint).
    const forcedEntry = context.startSettingsStream(true);
    await Promise.resolve();
    assert.deepEqual(sockets[1].sent.at(-1), {op: 'settings', on: true, request: 5},
        'a forced entry over a fresh socket re-subscribes');
    sockets[1].onmessage({data: JSON.stringify({...snapshot(4),
        changes: {...flatten(status()), 'config.dripDelayMs': 9999}})});
    assert.equal(applied.length, renders + 4, 'the superseded generation is discarded');
    sockets[1].onmessage({data: JSON.stringify(snapshot(5))});
    assert.equal(await forcedEntry, true);
    assert.equal(applied.at(-1).config.dripDelayMs, 3000);
    // Leaving again, then losing the socket off-view: the reconnect must not
    // re-subscribe (D19 — zero settings bytes while the view is inactive).
    context.stopSettingsStream();
    assert.deepEqual(sockets[1].sent.at(-1), {op: 'settings', on: false});
    sockets[1].close();
    // The second reconnect backs off (500 ms * 2^n with jitter).
    const idle = [...timers.values()].at(-1);
    idle.fn();
    sockets[2].open();
    assert(!sockets[2].sent.some(op => op.op === 'settings'),
        'an off-view reconnect must not re-subscribe the settings stream');
    assert.equal(applied.filter(value => value.config.dripDelayMs === 9999).length, 0,
        'a superseded frame never painted');
    assert.deepEqual(errors, []);
  })().catch(error => { console.error(error); process.exitCode = 1; });
}

// Firmware contracts: the op router's whitelist and rejection fall-through,
// the sender's bounded overlay projection, and the retired REST page's 404.
{
  const assert = require('assert').strict;
  const uiStream = fs.readFileSync(path.join(sketchDir, 'network/OpenBrewByWeightUiStream.inc'), 'utf8');
  const sender = fs.readFileSync(path.join(sketchDir, 'network/OpenBrewByWeightSettingsStream.inc'), 'utf8');
  // The settings op: History's contract in miniature. Bound sessions with the
  // owner check only, an exact field whitelist, a bounded request id, and no
  // accept outside the shape gate — a malformed shape falls through to the
  // shared owner-op rejection (close 4003, asserted in 22-diagnostic-stream).
  const start = uiStream.indexOf('strcmp(op->valuestring, "settings")');
  const branch = uiStream.slice(start, uiStream.indexOf('strcmp(op->valuestring, "bind")', start));
  const gate = branch.indexOf('if (shapeOk && numbersOk) {');
  assert(branch.includes('static const char *const kFields[] = {"op", "on", "request"};') &&
      branch.includes('uiStreamKnownFields(message, kFields, 3)') &&
      branch.includes('uiStreamIntInRange(requestId, 1, UINT32_MAX)'),
      'the settings op accepts exactly op/on/request with a bounded request id');
  assert(gate >= 0 && branch.indexOf('accepted = true') > gate &&
      !branch.includes('sendUiStreamClose'),
      'a malformed settings op must fall through to the shared 4003 rejection');
  assert(branch.includes('accepted = !requestId;') &&
      branch.includes('session->settingsOn = false;'),
      'unsubscribe carries no other field');
  assert(branch.includes('session->settingsOn = true;') &&
      branch.includes('session->settingsResync = true;') &&
      branch.includes('session->settingsRequest ='),
      'subscribe arms the snapshot and captures the request id');
  // The generic resync op arms the settings snapshot too (D9/F4).
  const resync = uiStream.slice(uiStream.indexOf('strcmp(op->valuestring, "resync")'),
    uiStream.indexOf('strcmp(op->valuestring, "diagnostic")'));
  assert(resync.includes('session->settingsResync = true;'),
      'the resync op must also force a fresh settings snapshot');
  // Dispatch: ordered after Home+card and Diagnostic, inside the budget,
  // gated on the subscription. The paused-session gate skips the whole
  // sendUiStream call — settings fingerprints are retained, not consumed —
  // and a page returning visible arms the urgent flag, so the next dispatch
  // delivers one catch-up delta (21-home-stream's gate, pinned here for the
  // settings ride).
  const chain = uiStream.slice(uiStream.indexOf('void OpenBrewByWeightNetwork::sendUiStream('),
      uiStream.indexOf('bool OpenBrewByWeightNetwork::sendShotCard('));
  assert(chain.includes('if (open && session.settingsOn)') &&
      chain.includes('within() && sendSettingsStream(session, control, network)'),
      'the settings sender rides the existing dispatch under its budget');
  assert(uiStream.includes('if (!session.paused || live || session.homeResync || session.resync)') &&
      uiStream.indexOf('session.paused') <
          uiStream.indexOf('self.sendUiStream(session, control, network, latest, eligible)') &&
      uiStream.includes('resumed = session->paused && cJSON_IsFalse(hidden);') &&
      uiStream.includes('if (resumed) self.uiStreamUrgent_.store(true, std::memory_order_release);'),
      'a hidden page holds settings frames whole; returning visible delivers one catch-up delta');
  assert(sender.includes('workBuf_->uiFingerprints[&session - uiStreams_]') &&
      sender.includes('hashes.settingsHashes'));
  // Envelope: a snapshot names v/boot/snapshot/request; an increment carries
  // only its type, request, and changes — and no frame spells snapshot:false.
  assert(sender.includes('"{\\"v\\":1,\\"type\\":\\"settings\\",\\"boot\\":%lu,'
      + '\\"snapshot\\":true,"') &&
      sender.includes('"{\\"type\\":\\"settings\\",\\"request\\":%lu,\\"changes\\":{"'),
      'the settings envelope carries its identity and request id');
  assert(!/"snapshot\\":false/.test(sender), 'no settings frame spells out snapshot:false');
  // Zero-change suppression and the deferred-send resync arm.
  assert(sender.includes('if (!delta.changed) return true;'));
  assert(sender.includes('if (deferred) {') &&
      sender.includes('session.settingsResync = true;'));
  assert(sender.includes('session.settingsBoot = control.bootId;') &&
      sender.includes('session.settingsResync = false;'),
      'boot and resync bookkeeping advances only on delivery');
  // The overlay stays inside its fingerprint table: 86 distinct fields of
  // kSettingsFields = 96 (measured 2026-10-24, D17 prune from 137).
  const settingsSlots = Number(/kSettingsFields\s*=\s*(\d+)/.exec(networkHeader)[1]);
  const paths = [...sender.matchAll(/delta\.(?:hashedField|field)\("([^"]+)"/g)].map(m => m[1]);
  const distinct = new Set(paths);
  assert(paths.length > 0 && distinct.size <= settingsSlots,
      `Settings projection ${distinct.size} distinct fields exceeds ${settingsSlots} fingerprint slots`);
  // The overlay carries only settings-owned fields (D17): the settings form,
  // bbw readback, drip chart, Bullseye, scale history, the scale-loss
  // projection, and the Micra picker/sign-in fields (34-stream-disjointness
  // enforces the Home/Settings boundary mechanically).
  for (const field of ['buzzerSupported', 'config.weightOffsetG',
    'config.postTareBaselineGraceMs', 'config.dripDelayMs', 'config.doseG',
    'config.alertOutputChannel', 'config.buzzerExtendedPulseRate',
    'config.bbwEwmaOffsetG', 'config.bbwEvidenceCount', 'config.bbwQuickStart',
    'config.bbwRecentG', 'config.bullseyeMelodyEnabled', 'config.bullseyeRtttl',
    'scale.history', 'scaleLoss.projectedS', 'scaleLoss.source',
    'lineaMicra.phase', 'lineaMicra.targetDeciC', 'lineaMicra.machines'])
    assert(distinct.has(field), 'missing settings stream field ' + field);
  // Consumer-first (D14) and layered disjointness (D17): the projection
  // carries only fields Home does not carry and the settings/home model
  // reads; unconsumed Micra telemetry rides the Diagnostic stream only and
  // must not grow back here, nor may any Home-duplicated field return.
  for (const field of ['firmwareVersion', 'bootId', 'configMutable', 'snapshotStale',
    'webUiOverrideActive', 'compatibilityMode', 'machineType', 'machineIntegration',
    'diagnosticPageVisible', 'lastCommand.requestId', 'lastCommand.state',
    'quickRevert.after', 'quickRevert.overrides', 'quickRevert.remainingS',
    'timeUtcSec', 'config.revision', 'config.timezoneId', 'config.timezoneAutomatic',
    'config.timezoneInitialized', 'config.appliedTimezoneOffsetMinutes',
    'config.goalWeightG', 'config.brewByWeight', 'config.rinseEnabled',
    'config.soundAlertsEnabled', 'config.scaleMacCacheMode',
    'config.fastExtractionGuardEnabled', 'config.fastMarginG', 'config.fastBeforeS',
    'config.slowExtractionGuardEnabled', 'config.slowMarginG', 'config.slowAfterS',
    'config.operationalWallMs', 'config.autoToManualGuardEnabled',
    'config.cupProtectionEnabled', 'config.stopIfCupRemoved', 'config.requireCupToStart',
    'config.avoidAccidentalTouchEnabled', 'config.noScaleBbwMode',
    'scale.preferredFriendlyName', 'scale.preferredMac', 'scale.preferredName',
    'scale.macCachePauseRemainingMs', 'cupSetup.observedG', 'scaleLoss.enabled',
    'lineaMicra.accountConfigured', 'lineaMicra.observeState', 'lineaMicra.powerState',
    'lineaMicra.readiness', 'lineaMicra.quality', 'lineaMicra.optimisticOn',
    'lineaMicra.optimisticOff',
    'lineaMicra.connectionType', 'lineaMicra.requestId',
    'lineaMicra.observedMode', 'lineaMicra.effectiveOn', 'lineaMicra.powerSource',
    'lineaMicra.freshnessPolicy', 'lineaMicra.sampleValid', 'lineaMicra.sampleAgeMs',
    'lineaMicra.freshnessMs', 'lineaMicra.pollIntervalMs', 'lineaMicra.temperatureState',
    'lineaMicra.temperatureError', 'lineaMicra.temperatureCommandAccepted',
    'lineaMicra.temperatureRetryable', 'lineaMicra.requestedTargetDeciC',
    'lineaMicra.appliedTargetDeciC', 'lineaMicra.temperatureHttpStatus',
    'lineaMicra.temperatureTransportStatus', 'lineaMicra.scalePaused',
    'lineaMicra.httpStatus', 'lineaMicra.transportStatus',
    'lineaMicra.accountMachineCount'])
    assert(!distinct.has(field), 'field must not ride the settings stream ' + field);
  assert(![...distinct].some(field => field === 'presets' || field.startsWith('presets.')),
      'presets ride the Home stream on every view, never the settings stream');
  // D6 steady-state cost: render caches gate the blobs and nothing self-ticks
  // (the two countdowns the overlay carried moved to Home with their fields).
  assert(sender.includes('renderedScaleHistoryHash') &&
      sender.includes('buildScaleHistoryJson(g_work->scaleHistory)') &&
      sender.includes('memcmp(&g_work->renderedBullseye'),
      'the history and RTTTL blobs sit behind render caches');
  assert(!sender.includes('g_wallClock') && !sender.includes('timeZoneOffsetAtOrUtc'),
      'the clock anchor died with timeUtcSec; no per-dispatch time work remains');
  // String values must be quoted JSON literals: an unquoted raw value renders
  // an empty melody as "config.bullseyeRtttl":, which poisons the whole frame
  // (hardware-confirmed 2026-10-23). Only the complete history array, already
  // a JSON value, may ride a raw hashedField.
  assert(sender.includes('delta.field("config.bullseyeRtttl", "\\"%s\\"", g_work->safeBullseyeRtttl)'),
      'the RTTTL must be emitted as a quoted JSON string, empty included');
  assert([...sender.matchAll(/delta\.hashedField\("([^"]+)"/g)].every(m => m[1] === 'scale.history'),
      'only the complete history array may use a raw hashedField value');
  // D15 value-space proof: every raw "%s" value must come from a producer of
  // complete JSON fragments — a drip-chart slice past a fixed literal, a
  // *Json()-named quoted-literal builder, or the machines array buffer (at
  // minimum "[]"). A raw empty-able string is the bullseye incident class.
  const rawValues = [...sender.matchAll(/delta\.field\("[^"]+", "%s",\s*([^;]+);/g)]
      .map(m => m[1].replace(/\s+/g, ' ').replace(/\)\s*$/, '').trim());
  assert(rawValues.length > 0 &&
      rawValues.every(value => value.includes('+ strlen(",') ||
          /Json\(/.test(value) || value === 'g_work->historyJson'),
      'raw %s field values must be complete-JSON producers');
  assert(!sender.includes('millis() - micra.sampleAtMs'),
      'a self-ticking sample age must never ride the settings projection (D14)');
  assert(sender.includes('std::isfinite(config.weightOffsetG) ? "%.2f" : "null"') &&
      sender.includes('std::isfinite(control.bbwEwmaOffsetG) ? "%.2f" : "null"'),
      'hand-formatted floats must degrade to null, never nan (D15)');
  // D10: values come from the control snapshot and the PSRAM cache callbacks.
  assert(sender.includes('callbacks_.copyBullseyeConfig') &&
      !/nvs|readPersisted|esp_partition|LoadPartition/.test(sender));
  // The retired REST page is a 404: the parse table knows only Admin, and the
  // handler answers unknown pages with STATUS_PAGE_UNKNOWN.
  const parse = blockAt(network, 'StatusPage parseStatusPage(');
  assert(parse.includes('/api/v1/status/admin') && !parse.includes('settings'),
      'GET /api/v1/status/settings must no longer parse to a status page');
  assert(network.includes('Unknown status page; use /api/v1/status/admin.'),
      'the retired page must answer 404 STATUS_PAGE_UNKNOWN');
}

// Client wiring: the view-scoped subscription lifecycle, the composed
// model, the Admin-only REST poll, and the write paths that stay on
// POST /api/v1/config.
{
  const assert = require('assert').strict;
  assert(codeIncludes(runtimeJs, 'sendUiOperation({ op: "settings", on: true, request: settingsRequest })'),
      'subscribe carries the request id');
  assert(codeIncludes(runtimeJs, 'sendUiOperation({ op: "settings", on: false })'),
      'leaving the view sends the bare unsubscribe the router accepts');
  assert(codeIncludes(runtimeJs, 'if (settingsStreamLive()) settingsSendSubscribe();'),
      'socket.onopen replays the subscription only while the view or a hold wants it');
  assert(codeIncludes(runtimeJs, 'if (!settingsStreamLive() || data.request !== settingsRequest) return;'),
      'frames of a superseded or ended subscription are discarded before the model');
  assert(codeIncludes(runtimeJs, 'historySynced = statsSynced = settingsSynced = false;') &&
      codeIncludes(runtimeJs, 'settingsFrame = null;'),
      'a closed socket ends the settings generation with the others');
  // D17: the composed model — the painters read {...home, ...settings} with
  // object subtrees merged, and the page waits on Home's model (homeReady).
  assert(codeIncludes(runtimeJs, 'function composeStatus(base, overlay) {') &&
      codeIncludes(runtimeJs, 'const home = await loadHomeStatus();') &&
      codeIncludes(runtimeJs, 'const status = composeStatus(homeFrame.status, settingsFrame.status);'),
      'the settings painters read the composed home+overlay model');
  assert(codeIncludes(runtimeJs, 'if (!settingsFrame || homeStale || !homeFrame) return;'),
      'a settings paint waits for the Home base layer');
  assert(codeIncludes(runtimeJs, 'typeof s.buzzerSupported === "boolean"') &&
      codeIncludes(runtimeJs, 'Array.isArray(s.scale && s.scale.history)') &&
      codeIncludes(runtimeJs, '(v !== "settings" && typeof s.configMutable !== "boolean")'),
      'the reconciled settings gate requires only settings-owned fields');
  // D19: the transient quick-write hold.
  assert(codeIncludes(runtimeJs, 'const settingsStreamLive = () => settingsStreamWanted || settingsHolds > 0;') &&
      codeIncludes(runtimeJs, 'if (!settingsHolds && !settingsStreamWanted) stopSettingsStream();') &&
      codeIncludes(runtimeJs, 'const overlay = await settingsSnapshotWait();'),
      'Home quick writes hold the stream for their snapshot, then release it');
  const settingsBlock = runtimeJs.slice(runtimeJs.indexOf('function settingsSendSubscribe('),
      runtimeJs.indexOf('function ensureBackgroundRecordStreams('));
  assert(!/\bapi\(|\bcommand\(|\bfetch\(/.test(settingsBlock),
      'settings frames only paint; writes originate from user-event handlers');
  assert(/CONFIG_REVISION_STALE"\)\s*\{\s*await R\.resyncSettingsStream\(\);/.test(viewJs.settings),
      'only the stale-revision retry pays for a fresh settings snapshot');
  assert(!viewJs.settings.includes('micraPoll'),
      'the Micra sign-in follows lineaMicra deltas; its REST poll is gone');
  assert(codeIncludes(viewJs.settings, 'loadSettingsConfig(s.config);'),
      'the Settings form repaints through the shared load guard');
  assert(codeIncludes(viewJs.settings, 's.scale && typeof s.scale.observedWeightG === "number"'),
      'the cup-minimum guidance reads Home\u2019s live platform weight (D17)');
  const polling = runtimeJs.slice(runtimeJs.indexOf('function armStatusTimer('),
      runtimeJs.indexOf('function pad2('));
  assert(polling.includes('activeView !== "admin"'),
      'the 10 s REST poll is Admin-only; Settings rides its stream');
  assert(codeIncludes(appJsSource, 'if (["home", "admin", "diagnostic"].includes(name))'),
      'Settings left the REST loadStatus entry list');
  assert(codeIncludes(appJsSource, 'name === "settings") ok = await R.startSettingsStream();'),
      'the Settings view enters through the stream');
  assert(codeIncludes(appJsSource, 'R.stopSettingsStream();'),
      'every route change stops the settings subscription');
  // No code path reads the retired Settings REST page; writes keep using
  // POST /api/v1/config with baseRevision (write-path suites cover them).
  assert(!codeIncludes(js, 'api(/api/v1/status/settings'),
      'no code path reads the retired Settings REST page');
}
