// Execute the UI formatters against historical disconnect and command records.
{
  const assert = require('assert').strict;
  const rows = {values: [], replaceChildren() {this.values = [];}, insertRow() {
    const cells = []; this.values.push(cells);
    return {insertCell: () => {const cell = {}; cells.push(cell); return cell;}};
  }};
  const elements = {scaleCommandRows: rows, scaleCommandTable: {}, scaleCommandHint: {}};
  const first = viewJs.diagnostic.indexOf('function applyScaleCommands(');
  const last = viewJs.diagnostic.indexOf('\n', first);
  const apply = new Function('$', viewJs.diagnostic.slice(first, last) +
    ';return applyScaleCommands;')(id => elements[id]);
  const commands = ['Tare', 'Volume', 'Start timer', 'Stop timer', 'Reset timer',
    'Tare and start timer', 'Power off'].map((name, i) => ({name,
      code: ['0x01', '0x02', '0x04', '0x05', '0x06', '0x07', '0x15'][i]}));
  apply({model: 'bookoo_ultra', supportedCommandsKnown: true, supportedCommands: commands});
  assert.equal(rows.values.length, 7); assert(!elements.scaleCommandTable.hidden);
  assert.equal(rows.values[6][1].textContent, '0x15');
  assert(elements.scaleCommandHint.textContent.includes('charging'));
  apply({model: 'bookoo_mini', supportedCommandsKnown: true, supportedCommands: commands.slice(0, 6)});
  assert.equal(rows.values.length, 6); assert.equal(elements.scaleCommandHint.textContent, '');
  apply({model: 'unknown', supportedCommandsKnown: false, supportedCommands: []});
  assert(elements.scaleCommandTable.hidden); assert.equal(rows.values.length, 0);
  assert(elements.scaleCommandHint.textContent.includes('unknown'));
}
{
  const source = runtimeJs;
  const first = source.indexOf('function formatScaleStatus(');
  const last = source.indexOf('function formatScaleTimer(', first);
  const elements = {cupWeight: {textContent: 'old'}, dCupWeight: {textContent: 'old'}, idleTareStatus: {textContent: 'old'}};
  const helpers = new Function('$', source.slice(first, last) +
    ';return {formatCupWeight,clearCupWeights,formatIdleTare};')(id => elements[id]);
  const good = {scale: {available: true, streamState: 'FRESH'},
    cupPresence: {present: true, weightValid: true, weightG: 80, placementId: 1}};
  if (helpers.formatCupWeight(good) !== '≈ 80.0 g') throw new Error('Cup weight format');
  for (const weight of [null, undefined, NaN, Infinity, '80']) {
    if (helpers.formatCupWeight({...good, cupPresence: {...good.cupPresence, weightG: weight}}) !== '—')
      throw new Error('Invalid cup weight must be unavailable');
  }
  for (const status of [{}, {...good, cupPresence: {}},
    {...good, cupPresence: {...good.cupPresence, present: false}},
    {...good, cupPresence: {...good.cupPresence, weightValid: false}},
    {...good, scale: {available: false, streamState: 'FRESH'}},
    {...good, scale: {available: true, streamState: 'STALE'}}]) {
    if (helpers.formatCupWeight(status) !== '—') throw new Error('Unavailable cup provenance');
  }
  helpers.clearCupWeights();
  if (Object.values(elements).some(el => el.textContent !== '—')) throw new Error('Stale cup UI');
  for (const [state, expected] of [
    ['empty', 'Empty scale must settle'],
    ['ready', 'Ready for a cup'],
    ['pending', 'Taring — waiting for zero'],
    ['machine_not_off', 'Waiting for machine off'],
    ['disabled', 'Off'],
    ['uncertain', 'Tare the empty scale, then reconnect it'],
  ]) {
    const s = {...good, cupPresence: {present: false, idleTare: state}};
    if (helpers.formatIdleTare(s) !== expected) throw new Error('Idle tare readiness: ' + expected);
  }
  for (const reason of ['remove', 'retry']) {
    const s = {...good, cupPresence: {present: true, idleTare: reason}};
    const text = helpers.formatIdleTare(s);
    if (!text.toLowerCase().includes('remove the cup')) throw new Error('Idle tare recovery');
  }
  if (helpers.formatIdleTare(good) !== '—') throw new Error('Older firmware idle tare fallback');
  for (const [available, streamState, expected] of [[false, 'FRESH', 'Disconnected'],
    [true, 'STALE', 'Stale'], [true, 'NO_SAMPLE', 'No sample']]) {
    const s = {scale: {available, streamState}, cupPresence: {idleTare: 'ready'}};
    if (helpers.formatIdleTare(s) !== expected) throw new Error('Idle tare must show stream health: ' + expected);
  }
  const home = partialHtml.home;
  const diagnostic = partialHtml.diagnostic;
  const cup = home.match(/<details id="cupRow" class="lampRow">([\s\S]*?)<\/details>/);
  if (!cup || !cup[1].includes('id="cupState"') || !cup[1].includes('id="cupWeight"') || !cup[1].includes('id="idleTareStatus"') ||
      !/<details id="scaleRow" class="lampRow">[\s\S]*?<\/details><details id="cupRow" class="lampRow">/.test(home) ||
      !/<legend>Scale<\/legend>[\s\S]*?id="dCupWeight"/.test(diagnostic) ||
      !diagnostic.includes('id="dCup"') || diagnostic.includes('id="scaleTareButton"') ||
      !source.includes("$('cupWeight').textContent=formatCupWeight(s)") ||
      !source.includes("t('dCupWeight',formatCupWeight(s))") ||
      !source.includes('function noteReachFail(err,force){clearCupWeights();') ||
      !source.includes('function stopViewPolls(){clearCupWeights();')) {
    throw new Error('Home and Diagnostic cup panel contract');
  }
  // Parse the actual adjacent C++ format literals for both status paths.
  const status = fs.readFileSync(path.join(sketchDir, 'network/ShotStopperStatus.inc'), 'utf8');
  // The shared C++ projection is exercised natively by scale_profiler_host_test.
  // Keep the Web-specific legacy labels and shared projection binding explicit.
  if (!status.includes('idleTarePresentationCode(tare, control.cupPresent)') ||
      !status.includes('idleCode == IDLE_WAITING_FOR_SETTLE ? "empty"') ||
      !status.includes('idleCode == IDLE_READY_FOR_CUP ? "ready"') ||
      !status.includes('idleTarePresentationName(idleCode)')) {
    throw new Error('Idle tare Web projection must preserve its public labels');
  }
  const blocks = [...status.matchAll(/"\\"cupPresence[^\n]*\n\s*("(?:\\.|[^"\\])*")/g)];
  if (blocks.length !== 1 || !network.includes('delta.field("cupPresence.weightG"'))
    throw new Error('Diagnostic cup JSON and Home cup deltas required');
  for (const block of blocks) {
    const format = block[0].match(/"(?:\\.|[^"\\])*"/g).map(s => JSON.parse(s)).join('').replace(/,$/, '');
    for (const valid of [false, true]) {
      const args = ['PRESENT', 'true', valid ? '80.0' : 'null', String(valid), '7', 'tared'];
      const json = JSON.parse('{' + format.replace(/%s|%lu/g, () => args.shift()) + '}');
      if (json.cupPresence.weightValid !== valid || json.cupPresence.placementId !== 7 ||
          json.cupPresence.weightG !== (valid ? 80 : null) ||
          json.cupPresence.idleTare !== 'tared') throw new Error('Cup JSON contract');
    }
  }
}

{
  // Historical disconnect/command summaries render through the shared live
  // renderer (runtime.js applyDiagnosticLive), shared with the socket stream.
  const runtimeSource = runtimeJs;
  const first = runtimeSource.indexOf('function applyDiagnosticLive(');
  const last = runtimeSource.indexOf('function applyDiagnosticStatus(', first);
  if (first < 0 || last < first) throw new Error('Missing scale diagnostic formatters');
  const elements = {};
  const live = new Function('$', '__WEBUI_TEXT__', 'formatBackflushState',
    'formatCupWeight', 'formatScaleWeight', 'formatScaleTimer',
    'updateScaleRenameUi', 'renderLineaMicraDiagnostic',
    runtimeSource.slice(first, last) + ';return applyDiagnosticLive;')(
    id => elements[id] || (elements[id] = {textContent: ''}),
    key => ({'diagnostic.none_2': 'NONE', 'diagnostic.none_3': 'none'}[key] || key),
    () => '', () => '', () => '', () => '', () => {}, () => {});
  const record = lastDisconnect => live({lineaMicra: null, safety: {}, scale: {
    lastDisconnect, lastDisconnectReasonName: lastDisconnect ? undefined : 'NONE',
    lastCommandFailure: {sequence: 1, summary: 'volume · status 15 · 0ms'},
    cupPresence: {}, backflush: {}}, cupPresence: {}});
  record({summary: 'SUPERVISION_TIMEOUT · gap · hci 520 · 5s ago'});
  const historical = elements.hLastDisconnect.textContent;
  for (const expected of ['SUPERVISION_TIMEOUT', 'gap', 'hci 520', '5s ago']) {
    if (!historical.includes(expected)) throw new Error('Missing disconnect detail: ' + expected);
  }
  record(null);
  if (elements.hLastDisconnect.textContent !== 'NONE') {
    throw new Error('Legacy scale status compatibility failed');
  }
  if (elements.hScaleCommandFailure.textContent !== 'volume · status 15 · 0ms')
    throw new Error('Command failure must be separate from disconnect');
  live({lineaMicra: null, safety: {}, scale: {lastCommandFailure: {sequence: 0}, cupPresence: {}}, cupPresence: {}});
  if (elements.hScaleCommandFailure.textContent !== 'none') throw new Error('Missing-event rendering');
}
