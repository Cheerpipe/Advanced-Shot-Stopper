// Execute the UI formatters against historical disconnect and command records.
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
    ['uncertain', 'Tare empty scale in Diagnostic'],
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
  const cup = home.match(/<fieldset id="cupPanel">([\s\S]*?)<\/fieldset>/);
  if (!cup || !cup[1].includes('id="cupState"') || !cup[1].includes('id="cupWeight"') || !cup[1].includes('id="idleTareStatus"') ||
      !/<fieldset id="scalePanel">[\s\S]*?<\/fieldset><fieldset id="cupPanel">/.test(home) ||
      !/<legend>Scale<\/legend>[\s\S]*?id="dCupWeight"/.test(diagnostic) ||
      !diagnostic.includes('id="dCup"') ||
      !source.includes("$('cupWeight').textContent=formatCupWeight(s)") ||
      !source.includes("t('dCupWeight',formatCupWeight(s))") ||
      !source.includes('function noteReachFail(err,force){clearCupWeights();') ||
      !source.includes('function stopViewPolls(){clearCupWeights();')) {
    throw new Error('Home and Diagnostic cup panel contract');
  }
  // Parse the actual adjacent C++ format literals for both status paths.
  const status = fs.readFileSync(path.join(sketchDir, 'network/ShotStopperStatus.inc'), 'utf8');
  const blocks = [...status.matchAll(/"\\"cupPresence[^\n]*\n\s*("(?:\\.|[^"\\])*")/g)];
  if (blocks.length !== 2) throw new Error('Both cup JSON projections required');
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
  const runtimeSource = viewJs.diagnostic;
  const first = runtimeSource.indexOf('function formatScaleDisconnect(');
  const last = runtimeSource.indexOf("'use strict'", first);
  if (first < 0 || last < first) throw new Error('Missing scale diagnostic formatters');
  const formatters = new Function(runtimeSource.slice(first, last) +
    ';return {formatScaleDisconnect,formatScaleCommandFailure};')();
  const historical = formatters.formatScaleDisconnect({lastDisconnect: {
    summary: 'SUPERVISION_TIMEOUT · gap · hci 520 · 5s ago',
    sequence: 1, reason: 'SUPERVISION_TIMEOUT', origin: 'gap', domain: 'hci',
    status: 520, ageMs: 5500, command: 'tare', commandElapsedMs: 14, teardownStatus: 7
  }});
  for (const expected of ['SUPERVISION_TIMEOUT', 'gap', 'hci 520', '5s ago']) {
    if (!historical.includes(expected)) throw new Error('Missing disconnect detail: ' + expected);
  }
  if (formatters.formatScaleDisconnect({lastDisconnectReasonName:'NONE'}) !== 'NONE') {
    throw new Error('Legacy scale status compatibility failed');
  }
  if (formatters.formatScaleCommandFailure({sequence:1, summary:'volume · status 15 · 0ms'}) !==
      'volume · status 15 · 0ms') throw new Error('Command failure must be separate from disconnect');
  if (formatters.formatScaleCommandFailure({sequence:0}) !== 'none') throw new Error('Missing-event rendering');
}
