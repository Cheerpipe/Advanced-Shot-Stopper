// The Diagnostic page subscribes to a live section projection on the owned
// socket only while the view is shown; every navigation unsubscribes.
{
  const assert = require('assert').strict, vm = require('vm');
  const source = runtimeJs.slice(runtimeJs.indexOf('let shotWs='),
    runtimeJs.indexOf('function formatExtractionGuard('));
  const sockets = [], timers = new Map(); let timer = 0, owner = true;
  const applied = [], frames = [];
  class Socket {
    static OPEN = 1;
    constructor(url) { this.url = url; this.readyState = 1; this.sent = []; sockets.push(this); }
    send(value) { this.sent.push(JSON.parse(value)); }
    close() { this.readyState = 3; this.onclose?.({}); }
  }
  const listeners = {};
  const context = {
    WebSocket: Socket, location: {protocol: 'http:', host: 'device.local'},
    document: {hidden: false, addEventListener() {}}, window: {addEventListener() {}},
    setTimeout: (fn, delay) => { timers.set(++timer, {fn, delay}); return timer; },
    clearTimeout: id => timers.delete(id), requestAnimationFrame: fn => { fn(); return 1; },
    webUiPollingActive: () => owner, webUiClientId: '0123456789abcdef', webUiPowerSeconds: () => 30,
    activeView: 'diagnostic', viewSeq: 1, viewReady: Promise.resolve(), lastStatusAt: 0,
    performance: {now: () => 1234}, invalidateHomeStream() {}, noteReachFail() {},
    applyDiagnosticLive: status => applied.push(status),
  };
  vm.runInNewContext(source, context);
  const changes = {
    'state': 'READY', 'machineState': 'CONFIRMED_OFF', 'relayClosed': false,
    'controlSource': 'none', 'physicalActivatorOn': false, 'reedOn': false,
    'backflush.stopReason': 'none',
    'cupPresence.state': 'ABSENT', 'cupPresence.present': false,
    'cupPresence.weightG': null, 'cupPresence.weightValid': false,
    'safety.state': 'READY', 'safety.taskWatchdogReady': true,
    'scale.available': true, 'scale.streamState': 'FRESH',
    'scale.controlState': 'ACTIVE', 'scale.rssi': -52,
    'scale.weightUpdateIntervalMs': 100, 'scale.timerMs': 65430,
    'scale.maxPacketGapMs': 240, 'scale.lastDisconnect.summary': '',
  };
  const snapshot = {v: 1, type: 'diagnostic', boot: 7, snapshot: true, changes: {...changes}};
  const snapshotFrame = context.diagStreamFrame(null, snapshot);
  assert.equal(snapshotFrame.status.scale.timerMs, 65430);
  const patch = context.diagStreamFrame(snapshotFrame,
      {v: 1, type: 'diagnostic', boot: 7, snapshot: false,
       changes: {'scale.timerMs': 65900, 'relayClosed': true}});
  assert.equal(patch.status.relayClosed, true, 'deltas patch the live cache');
  assert.equal(patch.status.scale.rssi, -52, 'unchanged fields survive a patch');
  for (const bad of [{...patch, boot: 8},
      {...patch, changes: {'scale.timerMs': Infinity}},
      {...patch, changes: {'scale.constructor.prototype': {poisoned: true}}},
      {...patch, snapshot: true, changes: {'scale.available': true}}]) {
    assert.throws(() => context.diagStreamFrame(patch, bad),
        'invalid diagnostic frames must fail closed');
  }

  context.startUiStream();
  assert.equal(sockets.length, 1);
  sockets[0].onopen();
  assert.equal(sockets[0].sent[0].op, 'bind');
  assert(!sockets[0].sent.some(m => m.op === 'diagnostic'),
      'the diagnostic subscription starts detached');
  context.startDiagnosticStream();
  assert.deepEqual(sockets[0].sent.at(-1), {op: 'diagnostic', on: true});
  sockets[0].onmessage({data: JSON.stringify(snapshot)});
  assert.equal(applied.at(-1).scale.timerMs, 65430, 'live frames render the three sections');
  vm.runInContext("activeView='settings'", context);
  const paints = applied.length;
  sockets[0].onmessage({data: JSON.stringify({
    v: 1, type: 'diagnostic', boot: 7, snapshot: false, changes: {'scale.timerMs': 66100}})});
  assert.equal(applied.length, paints,
      'frames on another page must not render diagnostics');
  vm.runInContext("activeView='diagnostic'", context);
  context.paintDiagnosticStream();
  assert.equal(applied.length, paints + 1);
  assert.equal(applied.at(-1).scale.timerMs, 66100,
      'returning to the page renders the updated cache');
  sockets[0].onmessage({data: JSON.stringify(
      {v: 1, type: 'diagnostic', boot: 7, snapshot: false, changes: {'__proto__.x': 1}})});
  assert.equal(sockets[0].sent.at(-1).op, 'resync', 'poisoned frames request a resync');
  context.stopDiagnosticStream();
  assert.deepEqual(sockets[0].sent.at(-1), {op: 'diagnostic', on: false});
  sockets[0].close();
  const retry = [...timers.values()].find(t => t.delay >= 400 && t.delay <= 600);
  assert(retry, 'socket loss must schedule a reconnect');
  retry.fn();
  sockets[1].onopen();
  assert(!sockets[1].sent.some(m => m.op === 'diagnostic'),
      'a closed subscription must not resubscribe on reconnect');
  context.startDiagnosticStream();
  assert.deepEqual(sockets[1].sent.at(-1), {op: 'diagnostic', on: true});
  sockets[1].close();
  const again = [...timers.values()].find(t => t.delay >= 700 && t.delay <= 1300);
  again.fn();
  sockets[2].onopen();
  assert.deepEqual(sockets[2].sent.find(m => m.op === 'diagnostic'), {op: 'diagnostic', on: true},
      'an open subscription resubscribes after reconnect');
}

{
  // The blink regression: value validity is number-vs-null in both
  // transports; the formatters must not require socket-only fields such as
  // scale.available (absent from the REST diagnostic payload). runtimeJs is
  // already localized, so expectations use the baked-in English literals.
  const assert = require('assert').strict;
  const first = runtimeJs.indexOf('function formatScaleWeight(');
  const last = runtimeJs.indexOf('const RR=', first);
  const formatters = new Function('pad2',
    runtimeJs.slice(first, last) + ';return {formatScaleWeight,formatScaleTimer};')(
    n => String(n).padStart(2, '0'));
  assert.equal(formatters.formatScaleTimer({scale: {timerMs: 65430}}), '1:05.4');
  assert.equal(formatters.formatScaleTimer({scale: {timerMs: null}}), '—');
  assert.equal(formatters.formatScaleTimer({scale: {}}), '—',
      'no REST available field may blank the timer');
  assert.equal(formatters.formatScaleWeight({scale: {observedWeightG: 12.34}}), '12.3 g');
  assert.equal(formatters.formatScaleWeight({scale: {currentWeightG: -0.5}}), '-0.5 g');
  assert.equal(formatters.formatScaleWeight({scale: {observedWeightG: null}}), '—');
  // One writer at a time: REST skips the live sections while the socket
  // cache is live, and losing the socket hands the sections back to REST.
  assert(runtimeJs.includes('if(!diagFrame)applyDiagnosticLive(s)'),
      'REST must not repaint live sections over the socket cache');
  assert(runtimeJs.includes('shotWs=null;diagFrame=null;'),
      'socket loss must drop the cache so REST resumes painting');
}

// Firmware projection: bounded field budget, view-scoped dispatch, and wiring.
{
  const assert = require('assert').strict;
  const stream = fs.readFileSync(path.join(sketchDir, 'network/ShotStopperUiStream.inc'), 'utf8');
  const homeStream = fs.readFileSync(path.join(sketchDir, 'network/ShotStopperHomeStream.inc'), 'utf8');
  assert(stream.includes('strcmp(op->valuestring, "diagnostic")'));
  assert(stream.includes('session->diagnostic = cJSON_IsTrue(on)'));
  assert(stream.includes('session.diagnostic && !sendDiagnosticStream(session, control)'),
      'the diagnostic delta rides the existing dispatch under the status workspace');
  // Record-stream no-change bookkeeping: the suppression path must adopt a
  // moved epoch, and every standing-window send (snapshots included) must
  // store epoch+fingerprint — otherwise idle ticks rebuild pages forever.
  assert.equal((homeStream.match(/session\.historyEpoch = epoch;/g) || []).length, 2,
      'history suppression and standing sends must both store the epoch');
  assert.equal((homeStream.match(/session\.statsEpoch = epoch;/g) || []).length, 2,
      'stats suppression and standing sends must both store the epoch');
  assert.equal((homeStream.match(/if \(!fetch\) \{/g) || []).length, 2,
      'snapshot sends must store the standing fingerprint');
  // Backpressure triage: a failed frame send must not tear down the owned
  // socket when nothing of the frame reached the wire. Only a partial
  // payload, a dead peer, or lost ownership may close the session.
  assert(stream.includes('uiStreamSendCleanAbort'),
      'the send override must classify clean header aborts');
  assert(homeStream.includes('session.homeResync = true;') &&
         homeStream.includes('session.diagResync = true;'),
      'deferred Home/diagnostic sends must re-arm their snapshot');
  assert(homeStream.includes('return uiStreamSendCleanAbort;'),
      'deferred record pages keep their retry state instead of closing');
  assert(homeStream.includes('session.statsSent = sentBase;'),
      'a deferred stats frame must roll its buffered rows back');
  // Dispatch order: the client's setup window waits for Home and the shot
  // card, so record pages — which may pace across dispatch ticks — go last.
  const homeCall = stream.indexOf('if (!sendHomeStream(session, control,');
  const cardCall = stream.indexOf('if (!sendShotCard(session, control,');
  const diagCall = stream.indexOf('session.diagnostic && !sendDiagnosticStream(session, control)');
  const historyCall = stream.indexOf('session.historyOn && !sendHistoryStream(session, control)');
  const statsCall = stream.indexOf('session.statsOn && !sendStatsStream(session, control)');
  assert(homeCall >= 0 && cardCall > homeCall && diagCall > cardCall &&
         historyCall > diagCall && statsCall > historyCall,
      'dispatch must send home, card, diagnostic, history, stats in order');
  // Pacing: one budgeted stats frame per dispatch resumes the in-flight page
  // and flags urgency so a healthy client streams without cadence gaps.
  assert(homeStream.includes('session.statsPageEpoch != epoch') &&
         homeStream.includes('session.statsSent = 0;'),
      'stats pages must capture identity at page start and resume by epoch');
  assert(homeStream.includes('uiStreamUrgent_.store(true, std::memory_order_release);\n    return true;'),
      'a continued page must flag urgency for the next dispatch');
  const diagRegion = homeStream.slice(homeStream.indexOf('sendDiagnosticStream'));
  const diagSlots = Number(/kDiagFields\s*=\s*(\d+)/.exec(networkHeader)[1]);
  const diagCalls = (diagRegion.match(/\bdelta\.field\(/g) || []).length;
  assert(diagCalls > 0 && diagCalls <= diagSlots,
      `Diagnostic projection ${diagCalls} fields exceeds ${diagSlots} fingerprint slots`);
  for (const field of ['"state"', '"machineState"', '"backflush.remainingMs"',
      '"cupPresence.weightG"', '"physicalActivatorOn"', '"relayClosed"', '"controlSource"',
      '"safety.taskWatchdogReady"', '"scale.streamState"', '"scale.timerMs"',
      '"scale.observedWeightG"', '"scale.maxPacketGapMs"',
      '"scale.lastDisconnect.summary"',
      '"scale.lastCommandFailure.summary"', '"scale.connectedMac"',
      '"lineaMicra.powerState"']) {
    assert(diagRegion.includes('delta.field(' + field), 'missing diagnostic field ' + field);
  }
  assert(diagRegion.includes('control.scaleAvailable && control.currentTimerValid'));
  assert(diagRegion.includes('control.currentTimerMs / 100 * 100'),
      'the timer keeps the Home projection 0.1 s quantization');
  assert(diagRegion.includes('static_cast<uint32_t>(millis() - micra.sampleAtMs) / 1000 * 1000'),
      'the Micra sample age must quantize to whole seconds so an idle stream'
      + ' does not emit one frame per dispatch tick');
  assert(diagRegion.includes('session.diagBoot = control.bootId'));
  assert(runtimeJs.includes("t('dScaleTimer',formatScaleTimer(s))") &&
         runtimeJs.includes("t('dScaleWeight',formatScaleWeight(s))"),
      'the shared live renderer paints the Scale timer and weight');
  assert(appJsSource.includes('R.stopDiagnosticStream()'),
      'leaving the view must unsubscribe');
  assert(appJsSource.indexOf('R.startDiagnosticStream()') <
         appJsSource.indexOf("await R.loadLog()"),
      'entering the view subscribes before the first REST poll');
  assert(partialHtml.diagnostic.includes('id="dScaleTimer"'));
  assert(partialHtml.diagnostic.includes('id="dScaleWeight"'));
  assert(JSON.parse(fs.readFileSync(
      path.join(sketchDir, 'web', 'locales', 'en.json'), 'utf8'))
      .strings['diagnostic.weight'] === 'Weight');
}
