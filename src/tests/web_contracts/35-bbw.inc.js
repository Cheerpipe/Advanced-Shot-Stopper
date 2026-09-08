{
  const assert = require('assert');
  const vm = require('vm');
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
    'bbwAlphaStatus', 'bbwAlgorithmHelp', 'resetCalibrationButton',
    'resetEwmaButton', 'weightOffsetBaselineG', 'goalWeightG']) element(id);
  const learning = element('learning', ['bbwLearning']);
  learning.querySelectorAll = () => [elements.get('weightOffsetBaselineG'),
    elements.get('resetCalibrationButton'), elements.get('resetEwmaButton')];
  const ewma = element('ewma', ['bbwEwma']);
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
  const makePayload = new Function('$', 'number', 'sToMs', 'presetState', 'bbwFormPresetId',
    payload + ';return brewPayload();');
  const fields = makePayload(id => elements.get(id) || {checked: true, value: 'auto'},
    () => 36, () => 30000, {activeId: 1}, 2);
  assert.equal(fields.id, 2, 'A selection change must not retarget the rendered recipe');
  assert(!('bbwAlgorithm' in fields));
  assert(!('weightOffsetBaselineG' in fields));
  assert(!('bbwAlpha' in fields));
}

(async () => {
  const assert = require('assert');
  const vm = require('vm');
  let blob;
  const records = Array.from({length: 120}, (_, i) => ({id: i + 1, bootId: 1,
    goalG: 36, actualG: 36.2, offsetG: i ? 1.5 : 0, durationS: 30,
    bbwAlgorithm: i % 2 ? 'legacy' : 'linear_ewma', bbwAlgorithmVersion: 1,
    bbwAlpha: i % 2 ? 1 : .3, bbwLearningApplied: i ? true : null, presetId: i ? 255 : 0}));
  const context = vm.createContext({
    api: async url => {assert.equal(url, '0/120/date/desc'); return {shots: records};},
    shotsUrl: (...args) => args.join('/'), SHOTS_EXPORT_LIMIT: 120,
    formatShotTimeCsv: () => '', shotDisplayActualG: weight => weight,
    shotDisplayFlowGS: () => 1.2, Blob,
    URL: {createObjectURL: value => {blob = value; return 'blob:test';}, revokeObjectURL() {}},
    document: {createElement: () => ({click() {}})},
    message: message => {throw new Error(message);}, formatCommandError: (_, e) => e.message
  });
  vm.runInContext(runtimeJs.split('\n').find(line => line.startsWith('async function exportShotsCsv(')), context);
  await vm.runInContext('exportShotsCsv()', context);
  const lines = (await blob.text()).split('\n').map(line => line.split(','));
  assert.equal(lines.length, 121);
  assert.equal(lines[0][11], 'offset_g');
  assert.equal(lines[1][11], '0');
  assert.deepEqual(lines[0].slice(-5), ['bbw_algorithm', 'bbw_algorithm_version', 'bbw_alpha', 'bbw_learning_applied', 'preset_id']);
  assert.deepEqual(lines[1].slice(-5), ['linear_ewma', '1', '0.30', '', '']);
  assert.deepEqual(lines[2].slice(-5), ['legacy', '1', '1.00', '1', '255']);
})().catch(error => {console.error(error); process.exitCode = 1;});
