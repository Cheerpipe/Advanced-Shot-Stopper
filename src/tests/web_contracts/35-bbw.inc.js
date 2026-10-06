{
  const assert = require('assert');
  const vm = require('vm');
  assert(html.includes('<option value="legacy">Linear regression + offset correction</option>'));
  assert(html.includes('<strong>Linear regression + offset correction:</strong>'));
  assert(html.includes('applies the full final-weight error from the latest successful shot'));
  assert(html.includes('<strong>Linear prediction + adaptive EWMA:</strong>'));
  assert(html.includes('it never changes to the other algorithm automatically'));
  assert(!html.includes('<div class="row"><label>Target (g)'));
  for (const id of ['resetCalibrationButton', 'resetEwmaButton'])
    assert(html.includes('id="' + id + '" class="btnGlyph mutable"'));
  assert(html.includes('id="bbwAlphaBaseline" type="number" min="0.01" max="1" step="0.01" required'));
  const elements = new Map();
  const element = (id, classes = []) => {
    const names = new Set(classes);
    const node = {value: '', checked: false, disabled: false, textContent: '',
      classList: {contains: name => names.has(name),
        toggle: (name, on) => on ? names.add(name) : names.delete(name)},
      querySelectorAll: () => []};
    elements.set(id, node);
    return node;
  };
  for (const id of ['bbwAlgorithm', 'brewByWeight', 'learnedOffsetG', 'bbwAlpha',
    'bbwAlphaStatus', 'resetCalibrationButton',
    'resetEwmaButton', 'weightOffsetBaselineG', 'bbwAlphaBaseline', 'goalWeightG']) element(id);
  const learning = element('learning', ['bbwLearning']);
  learning.querySelectorAll = () => [elements.get('weightOffsetBaselineG'),
    elements.get('resetCalibrationButton'), elements.get('resetEwmaButton'), elements.get('bbwAlphaBaseline')];
  const ewma = element('ewma', ['bbwEwma']);
  ewma.querySelectorAll = () => [elements.get('bbwAlphaBaseline'), elements.get('resetEwmaButton')];
  const select = elements.get('bbwAlgorithm');
  const context = vm.createContext({$: id => elements.get(id), controlsMutable: true,
    brewDirty: false, configDirty: false, configLoaded: true, formRev: 1,
    document: {querySelectorAll: () => [learning, ewma]}});
  const start = runtimeJs.indexOf('let bbwReadback=');
  const end = runtimeJs.indexOf('function soundAlertsAreOn', start);
  vm.runInContext(runtimeJs.slice(start, end), context);
  vm.runInContext('bbwFormPresetId=2;bbwReadback={bbwPresetId:2,bbwAlgorithm:"linear_ewma",bbwLegacyOffsetG:0,bbwEwmaOffsetG:1.56,bbwAlpha:0.3,bbwAlphaSource:"initial",bbwEvidenceCount:0}', context);
  elements.get('brewByWeight').checked = true;
  select.value = 'linear_ewma';
  const refresh = () => vm.runInContext('updateBbwControls()', context);
  refresh();
  assert.equal(elements.get('bbwAlpha').textContent, '0.30');
  assert.equal(elements.get('learnedOffsetG').textContent, '1.56 g');
  assert(!ewma.classList.contains('hidden'));
  select.value = 'legacy';
  context.brewDirty = true;
  refresh();
  assert(ewma.classList.contains('hidden'));
  assert(elements.get('bbwAlphaBaseline').disabled);
  assert.equal(elements.get('learnedOffsetG').textContent, '0.00 g');
  assert(elements.get('resetCalibrationButton').disabled);
  vm.runInContext('bbwReadback.bbwAlpha=.5;bbwReadback.bbwAlphaSource="learned";bbwReadback.bbwEvidenceCount=20', context);
  refresh();
  assert.equal(select.value, 'legacy');
  assert.equal(elements.get('bbwAlpha').textContent, '0.50');
  select.value = 'linear_ewma';
  context.brewDirty = false;
  refresh();
  assert.equal(elements.get('bbwAlphaStatus').textContent, 'Learned · Evaluating');
  elements.get('brewByWeight').checked = false;
  refresh();
  assert(select.disabled && learning.classList.contains('hidden'));
  assert(elements.get('weightOffsetBaselineG').disabled);
  elements.get('brewByWeight').checked = true;
  context.controlsMutable = false;
  refresh();
  assert(select.disabled && elements.get('resetEwmaButton').disabled);
  vm.runInContext('bbwReadback.bbwPresetId=1', context);
  refresh();
  assert.equal(elements.get('bbwAlpha').textContent, '—');
  assert.equal(elements.get('learnedOffsetG').textContent, '—');
  const load = runtimeJs.split('\n').find(line => line.startsWith('function loadSettingsConfig('));
  vm.runInContext(load, context);
  context.brewDirty = true;
  select.value = 'legacy';
  vm.runInContext('loadSettingsConfig({goalWeightG:36,revision:5,bbwAlgorithm:"linear_ewma"})', context);
  assert.equal(select.value, 'legacy');

  const payload = runtimeJs.split('\n').find(line => line.startsWith('function brewPayload('));
  const makePayload = new Function('$', 'number', 'sToMs', 'presetState', 'bbwFormPresetId', 'document',
    payload + ';return brewPayload();');
  const nonMicraDocument = {documentElement: {classList: {contains: () => false}}};
  const fields = makePayload(id => elements.get(id) || {checked: true, value: 'auto'},
    () => 36, () => 30000, {activeId: 1}, 2, nonMicraDocument);
  assert.equal(fields.id, 2, 'A selection change must not retarget the rendered recipe');
  assert(!('bbwAlgorithm' in fields));
  assert(!('weightOffsetBaselineG' in fields));
  assert(!('bbwAlpha' in fields));
  assert(!('bbwAlphaBaseline' in fields));
  context.controlsMutable = true;
  elements.get('brewByWeight').checked = true;
  select.value = 'linear_ewma';
  refresh();
  const withBase = makePayload(id => elements.get(id) || {checked: true, value: 'auto'},
    id => id === 'bbwAlphaBaseline' ? .37 : 36, () => 30000,
    {activeId: 1}, 2, nonMicraDocument);
  assert.equal(withBase.bbwAlphaBaseline, .37);
}

(async () => {
  const assert = require('assert');
  const vm = require('vm');
  let blob;
  const records = Array.from({length: 100}, (_, i) => ({id: i + 1, bootId: 1,
    goalG: 36, actualG: 36.2, offsetG: i ? 1.5 : 0, durationS: 30,
    bbwAlgorithm: i % 2 ? 'legacy' : 'linear_ewma', bbwAlgorithmVersion: i % 2 ? 1 : 2,
    bbwAlpha: i % 2 ? 1 : .37, bbwLearningApplied: i ? true : null, presetId: i ? 255 : 0}));
  records[1].wCg = [0, 1520, 3105, 3620]; records[1].wAtMs = [0, 450, 900, 1600];
  records[2].wCg = [0, 800]; records[2].wAtMs = [0, 500];
  records[3].wAtMs = [0,137,1001,1138];
  records[3].wCg = records[3].wAtMs.slice();
  const context = vm.createContext({
    statsFrameWindow: async (offset, limit, sort, dir) => {
      assert.equal(`${offset}/${limit}/${sort}/${dir}`, '0/100/date/desc');
      return records;
    },
    SHOTS_EXPORT_LIMIT: 100,
    formatShotTimeCsv: () => '', shotDisplayActualG: weight => weight,
    Blob,
    URL: {createObjectURL: value => {blob = value; return 'blob:test';}, revokeObjectURL() {}},
    document: {createElement: () => ({click() {}})},
    message: message => {throw new Error(message);}, formatCommandError: (_, e) => e.message
  });
  vm.runInContext(runtimeJs.slice(runtimeJs.indexOf('function lastCurveWeightG('),
    runtimeJs.indexOf('async function populateTimezoneOptions(')), context);
  vm.runInContext(runtimeJs.split('\n').find(line => line.startsWith('async function exportShotsCsv(')), context);
  await vm.runInContext('exportShotsCsv()', context);
  const lines = (await blob.text()).split('\n').map(line => line.split(','));
  assert.equal(lines.length, 101);
  assert.equal(lines[0][11], 'offset_g');
  assert.equal(lines[1][11], '0');
  assert.deepEqual(lines[0].slice(-14), ['curve_truncated', 'curve_break_before', ...Array.from({length:4}, (_,i)=>['sample_'+(i+1)+'_time_s','sample_'+(i+1)+'_weight_g','sample_'+(i+1)+'_flow_g_s']).flat()]);
  assert.deepEqual(lines[1].slice(-14), ['0', '', ...Array(12).fill('')]);
  assert.deepEqual(lines[2].slice(-14), ['0', '', '0', '0', '', '0.45', '15.2', '', '0.9', '31.05', '', '1.6', '36.2', '15.72']);
  assert.equal(+lines[2][34], 15.716666666666665);
  assert.deepEqual(lines[3].slice(-14), ['0', '', '0', '0', '', '0.5', '8', '', ...Array(6).fill('')]);
  assert.deepEqual(lines[4].slice(-12), ['0','0','','0.137','1.37','','1.001','10.01','10.00','1.138','11.38','10.00']);
  records[1].wAtMs=Array.from({length:1201},(_,i)=>i*50);
  records[1].wCg=records[1].wAtMs.map(t=>t/5);
  records[1].durationS=60;
  await vm.runInContext('exportShotsCsv()',context);
  const complete=(await blob.text()).split('\n')[2].split(',');
  assert.equal(complete[35],'0');
  assert.equal(complete[34],'2');
  assert.equal(complete.at(-3),'60');
  assert.equal(complete.at(-1),'2.00');
  assert.equal(complete.slice(37).filter((v,i)=>i%3===2&&v==='2.00').length,1181);
  for (const record of records) {
    record.wCg = Array(1201).fill(1234);
    record.wAtMs = Array.from({length:1201}, (_,i)=>i*50);
    record.wBreakBefore = Array.from({length:1200}, (_,i)=>i+1);
    record.wTruncated = true;
  }
  await vm.runInContext('exportShotsCsv()', context);
  const maximum = (await blob.text()).split('\n').map(line=>line.split(','));
  assert.equal(maximum.length, 101);
  assert.equal(maximum[0].length, 3640);
  assert.equal(maximum[0].at(-3), 'sample_1201_time_s');
  for (const row of maximum.slice(1)) {
    assert.equal(row.length, maximum[0].length);
    assert.equal(row[35], '1');
    assert.equal(row[36].split(';').length, 1200);
    assert.equal(row.at(-3), '60');
    assert.equal(row.at(-2), '12.34');
  }
})().catch(error => {console.error(error); process.exitCode = 1;});
