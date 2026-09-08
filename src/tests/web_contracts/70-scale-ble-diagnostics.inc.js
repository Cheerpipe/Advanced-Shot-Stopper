// Execute the UI formatters against historical disconnect and command records.
{
  const source = fs.readFileSync(path.join(sketchDir, 'web/js/runtime.js'), 'utf8');
  const first = source.indexOf('function clearCupWeights(');
  const last = source.indexOf('function formatScaleTimer(', first);
  const elements = {cupWeight: {textContent: 'old'}, dCupWeight: {textContent: 'old'}};
  const helpers = new Function('$', source.slice(first, last) +
    ';return {formatCupWeight,clearCupWeights};')(id => elements[id]);
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
  const home = fs.readFileSync(path.join(sketchDir, 'web/html/home.html'), 'utf8');
  const diagnostic = fs.readFileSync(path.join(sketchDir, 'web/html/diagnostic.html'), 'utf8');
  const cup = home.match(/<fieldset id="cupPanel">([\s\S]*?)<\/fieldset>/);
  if (!cup || !cup[1].includes('id="cupState"') || !cup[1].includes('id="cupWeight"') ||
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
      const args = ['PRESENT', 'true', valid ? '80.0' : 'null', String(valid), '7'];
      const json = JSON.parse('{' + format.replace(/%s|%lu/g, () => args.shift()) + '}');
      if (json.cupPresence.weightValid !== valid || json.cupPresence.placementId !== 7 ||
          json.cupPresence.weightG !== (valid ? 80 : null)) throw new Error('Cup JSON contract');
    }
  }
}

{
  const runtimeSource = fs.readFileSync(path.join(sketchDir, 'web/js/diagnostic.js'), 'utf8');
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
