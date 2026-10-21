// Stats consistency model: thresholds as a share of each preset's average, units
// per data (seconds for one preset, percent for several), and a vertical scale
// with a floor, an outlier cap and tidy label steps.
{
  const assert = require('assert');
  const model = new Function(`${blockAt(viewJs.stats, 'const CONSISTENT = ')};return consistencyModel;`)();
  const near = (a, b, what) => assert.ok(Math.abs(a - b) < 1e-9, `${what}: ${a} != ${b}`);
  // Table order is newest first; ids grow with every logged shot.
  const shots = (times, preset = () => 1) =>
    times.map((arrivalS, i) => ({id: 100 + i, presetId: preset(i), arrivalS})).reverse();
  const alternate = (lo, hi, n = 10) => Array.from({length: n}, (_, i) => (i % 2 ? hi : lo));

  // Identical shots: very consistent, scale at its floor of twice the 5 % band.
  const flat = model(shots(Array(10).fill(30)));
  assert.equal(flat.level, 'good');
  assert.equal(flat.sec, 30);
  near(flat.typical, 0, 'flat typical');
  near(flat.M * flat.sec, 3, 'flat scale in seconds');
  near(flat.spread, 0, 'flat spread');

  // Neighbour changes of 4 %, 8 % and 13.3 % of a 30 s average.
  assert.equal(model(shots(alternate(29.4, 30.6))).level, 'good');
  const fair = model(shots(alternate(28.8, 31.2)));
  assert.equal(fair.level, 'fair');
  near(fair.typical * fair.sec, 2.4, 'fair typical in seconds');
  assert.equal(model(shots(alternate(28, 32))).level, 'poor');

  // Fewer than five shots never get a verdict.
  const few = model(shots([30, 31, 29]));
  assert.equal(few.level, 'few');
  assert.equal(few.typical, null);
  assert.equal(model(shots([30])).level, 'few');
  assert.equal(model([]).rows.length, 0);

  // Only the newest ten count, oldest first, whatever the table order.
  const twelve = model(shots(Array.from({length: 12}, (_, i) => 30 + (i % 3))));
  assert.deepEqual(twelve.rows.map((r) => r.id), [102, 103, 104, 105, 106, 107, 108, 109, 110, 111]);

  // Several presets: each shot against its own average, in percent, with 5 % label steps.
  const mixed = model(shots([20, 45, 20.4, 45.9, 19.6, 44.1, 20, 45], (i) => (i % 2 ? 2 : 1)));
  assert.equal(mixed.sec, 0);
  assert.equal(mixed.level, 'good');
  near(mixed.M, 0.1, 'mixed floor');
  near(mixed.rel[1], 0, 'a lungo at its own average');

  // One gushing shot cannot flatten the other nine: the scale stops at 30 %,
  // rounded up to whole seconds, and the shot is drawn past it as clipped.
  const outlier = model(shots([...Array(9).fill(30), 45]));
  near(outlier.sec, 31.5, 'outlier average');
  near(outlier.M * outlier.sec, 10, 'outlier scale in seconds');
  assert.ok(outlier.rel[9] > outlier.M);

  // Short shots round the floor to half seconds instead of whole ones.
  const short = model(shots(Array(10).fill(18)));
  near(short.M * short.sec, 2, 'short shot scale in seconds');
}
// Preset names are user text: the consistency pill escapes them before the
// pill writes its label as markup.
{
  const assert = require('assert');
  const escHtml = new Function(`${blockAt(runtimeJs, 'function escHtml(')};return escHtml;`)();
  assert.equal(escHtml('<b>"A&B"'), '&#60;b&#62;&#34;A&#38;B&#34;');
  assert.ok(codeIncludes(viewJs.stats, 'R.placePill(pill, R.escHtml(text),'),
      'The consistency pill must escape the preset name');
}
// The Stats averages resolve their element ids from fixed names, never from
// translated text, so every locale fills all five values.
{
  const assert = require('assert');
  const filled = [];
  new Function('$', 'R', '__WEBUI_TEXT__', 'renderStatsDurChart',
      `${blockAt(rawViewJs.stats, 'function renderShotStats(')};renderShotStats();`)(
      (id) => (filled.push(id), {classList: {toggle() {}}}),
      {uiState: {shotStats: {avgDurationS: 1, avgYieldG: 2, shotsPerDay: 3, avgErrorPct: 4,
        avgFlowGps: 5}}}, (key) => 'translated ' + key, () => {});
  assert.deepEqual(filled, ['statsAvgDur', 'statsAvgWeight', 'statsAvgDaily', 'statsAvgErr',
    'statsAvgFlow']);
}
