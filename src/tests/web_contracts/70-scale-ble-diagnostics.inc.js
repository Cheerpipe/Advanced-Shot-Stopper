// Execute the UI formatters against historical disconnect and command records.
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
