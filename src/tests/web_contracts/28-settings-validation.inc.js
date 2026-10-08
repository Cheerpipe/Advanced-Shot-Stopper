// Settings brew validation: every cross-field rule reports on the field the
// user edited, phrased from that side with the attempted value, the blocking
// limit, and the source field plus its settings section; multiple edited
// fields in conflict yield one error each. The battery executes the real
// validator (rangeCheck, number helpers, and the rule table) against a stub
// form with a controlled brew baseline.
{
  const svAssert = require('assert').strict;
  const svCut = (start, end) => runtimeJs.slice(runtimeJs.indexOf(start),
    runtimeJs.indexOf(end));
  const svValidatorSrc =
    svCut('function rangeCheck(', 'function formNumber(') +
    svCut('function number(', 'function sToMs(') +
    svCut('function formNumber(', 'function validNtpHostnameClient(') +
    svCut('const sub = (t, vals)', 'function validateDateTimeClient');
  // Field defaults mirror the Settings form: steps come from the shipped
  // inputs (asserted below) and values form one consistent stored state.
  const svFieldSpec = {
    goalWeightG: ['number', '1', '36'],
    operationalWallS: ['number', '1', '45'],
    bbwProtectionS: ['number', '0.1', '12'],
    weightOffsetBaselineG: ['number', '0.01', '1.5'],
    bbwAlphaBaseline: ['number', '0.01', '0.3'],
    maxRecoveryWeightG: ['number', '0.1', '80'],
    minBbwBrewTimeS: ['number', '0.5', '15'],
    minRecoveryWeightG: ['number', '0.1', '34'],
    maxBbwBrewTimeS: ['number', '0.5', '40'],
    autoToManualGuardManualLimitS: ['number', '1', '30'],
    autoToManualGuardBaselineS: ['number', '1', '25'],
    retareWindowS: ['number', '0.1', '5'],
    fastExtractionGuardEnabled: ['checkbox', '', true],
    slowExtractionGuardEnabled: ['checkbox', '', true],
    autoRetare: ['checkbox', '', false],
  };
  const svValidate = (edit, setup) => {
    const fields = {};
    for (const [id, [type, step, init]] of Object.entries(svFieldSpec))
      fields[id] = {id, type, step, value: type === 'checkbox' ? '' : init,
        checked: type === 'checkbox' ? init : false, disabled: false,
        validity: {valid: true}};
    if (setup) setup(fields);
    const baseline = {};
    for (const el of Object.values(fields))
      baseline[el.id] = el.type === 'checkbox' ? el.checked : el.value;
    if (edit) edit(fields);
    const api = new Function('$', 'document', 'settingsSectionEls', 'brewBaseline',
      svValidatorSrc + ';return {validateBrewClient, rules: BREW_CROSS_RULES};')(
      (id) => fields[id],
      {documentElement: {classList: {contains: () => false}}},
      (section) => (section === 'brew' ? Object.values(fields) : []),
      baseline);
    return {errors: api.validateBrewClient(), rules: api.rules};
  };

  // (a) Editing only Target into conflict attributes the error to Target with
  // the exact self-contained phrasing: attempted value, limit with the
  // 0.1-step precision, source field, and section.
  const svA = svValidate((f) => {
    f.goalWeightG.value = '10';
  });
  svAssert.equal(svA.errors.length, 1, JSON.stringify(svA.errors));
  svAssert.equal(svA.errors[0].id, 'goalWeightG');
  svAssert.equal(svA.errors[0].msg,
    'Target 10 g must be greater than 34.0 g (Min recovery, Slow extraction guard).');

  // (b) A self-range violation keeps its own field and gains the attempted
  // value prefix; the cross-field consequence of the same edit follows.
  const svB = svValidate((f) => {
    f.goalWeightG.value = '250';
  });
  svAssert.equal(svB.errors[0].id, 'goalWeightG');
  svAssert.equal(svB.errors[0].msg, 'Target 250 g must be from 10 to 200 g.');
  svAssert.equal(svB.errors[1].msg,
    'Target 250 g must be less than 80.0 g (Max recovery, Fast extraction guard).');
  svAssert.ok(svB.errors.every((e) => e.id === 'goalWeightG'));

  // (c) Two edited fields in conflict produce two errors, one per side, each
  // embedding the other field's value.
  const svC = svValidate((f) => {
    f.goalWeightG.value = '10';
    f.minRecoveryWeightG.value = '40';
  });
  svAssert.equal(svC.errors.length, 2, JSON.stringify(svC.errors));
  svAssert.deepEqual(svC.errors.map((e) => e.id),
    ['minRecoveryWeightG', 'goalWeightG']);
  svAssert.equal(svC.errors[0].msg,
    'Min recovery 40.0 g must be less than 10 g (Target, Brew by Weight).');
  svAssert.equal(svC.errors[1].msg,
    'Target 10 g must be greater than 40.0 g (Min recovery, Slow extraction guard).');

  // (d) A disabled guard skips both its self-range checks and its rules.
  const svD = svValidate((f) => {
    f.fastExtractionGuardEnabled.checked = false;
    f.maxRecoveryWeightG.value = '5';
  });
  svAssert.deepEqual(svD.errors, [], JSON.stringify(svD.errors));

  // (e) Editing Max BBW time below a guard's minimum time blames Max BBW time
  // and embeds the blocking minimum with its 0.5-step precision.
  const svE = svValidate((f) => {
    f.slowExtractionGuardEnabled.checked = false;
    f.operationalWallS.value = '25';
  }, (f) => {
    f.minBbwBrewTimeS.value = '28';
    f.autoToManualGuardManualLimitS.value = '20';
    f.autoToManualGuardBaselineS.value = '15';
  });
  svAssert.equal(svE.errors.length, 1, JSON.stringify(svE.errors));
  svAssert.equal(svE.errors[0].id, 'operationalWallS');
  svAssert.equal(svE.errors[0].msg,
    'Max BBW time 25 s must be greater than 28.0 s (Min BBW brew time, Fast extraction guard).');

  // (f) A stored conflict with nothing edited falls back to the rule's owning
  // field, phrased from that side.
  const svF = svValidate(null, (f) => {
    f.goalWeightG.value = '10';
  });
  svAssert.equal(svF.errors.length, 1, JSON.stringify(svF.errors));
  svAssert.equal(svF.errors[0].id, 'minRecoveryWeightG');
  svAssert.equal(svF.errors[0].msg,
    'Min recovery 34.0 g must be less than 10 g (Target, Brew by Weight).');

  // (h) The retare floor rule shows its computation when auto-retare is on and
  // states the bare bound when it is off.
  const svH1 = svValidate(null, (f) => {
    f.bbwProtectionS.value = '5';
    f.autoRetare.checked = true;
  });
  svAssert.equal(svH1.errors.length, 1, JSON.stringify(svH1.errors));
  svAssert.equal(svH1.errors[0].msg,
    'BBW protection 5.0 s must be at least 8.0 s (Retare window 5.0 s + 3 s, Tare).');
  const svH2 = svValidate(null, (f) => {
    f.bbwProtectionS.value = '2';
  });
  svAssert.equal(svH2.errors.length, 1, JSON.stringify(svH2.errors));
  svAssert.equal(svH2.errors[0].msg, 'BBW protection 2.0 s must be at least 3.0 s.');

  // (i)/(j) The A→M ceiling conflict blames the edited side: the wall side
  // gets "at least", the limit side gets the capped range wording.
  const svI = svValidate((f) => {
    f.operationalWallS.value = '40';
  }, (f) => {
    f.autoToManualGuardManualLimitS.value = '45';
    f.maxBbwBrewTimeS.value = '20';
  });
  svAssert.equal(svI.errors.length, 1, JSON.stringify(svI.errors));
  svAssert.equal(svI.errors[0].msg,
    'Max BBW time 40 s must be at least 45 s (Manual limit, A→M time guard).');
  const svJ = svValidate((f) => {
    f.autoToManualGuardManualLimitS.value = '70';
  });
  svAssert.deepEqual(svJ.errors.map((e) => e.id),
    ['autoToManualGuardManualLimitS', 'autoToManualGuardManualLimitS']);
  svAssert.equal(svJ.errors[1].msg,
    'Manual limit 70 s must be from 10 s to 45 s (up to Max BBW time, Brew by Weight).');

  // (g) Rule-table completeness against the relation inventory: eleven rules
  // (ten two-sided plus the single-sided retare floor), every referenced
  // field a real Settings input with the steps the phrasing depends on, and
  // every section reference a real section summary.
  svAssert.equal(svA.rules.length, 11);
  svAssert.ok(svA.rules.some((r) => r[1] === 'retareFloor'));
  for (const [id, step] of [['goalWeightG', '1'], ['operationalWallS', '1'],
    ['bbwProtectionS', '0.1'], ['maxRecoveryWeightG', '0.1'],
    ['minBbwBrewTimeS', '0.5'], ['minRecoveryWeightG', '0.1'],
    ['maxBbwBrewTimeS', '0.5'], ['autoToManualGuardManualLimitS', '1'],
    ['autoToManualGuardBaselineS', '1']]) {
    const input = settingsHtml.match(new RegExp('id="' + id + '"[^>]*'));
    svAssert.ok(input, 'Settings input missing: ' + id);
    svAssert.ok(input[0].includes('step="' + step + '"'),
      id + ' must keep step ' + step + ' for message precision');
  }
  for (const section of ['Brew by Weight', 'Fast extraction guard',
    'Slow extraction guard', 'A→M time guard', 'Tare'])
    svAssert.ok(settingsHtml.includes('>' + section + '</summary>'),
      'Settings section summary missing: ' + section);
  svAssert.ok(codeIncludes(runtimeJs,
      'const errs = validateBrewClient(); if (errs.length) { showFieldErrors(errs); return false; }'),
    'saveBrewPreset must render the full violation list');
  svAssert.ok(codeIncludes(runtimeJs,
      'brewBaseline[el.id] !== (el.type === "checkbox" ? el.checked : el.value)'),
    'Attribution must diff the live form against the clean brew baseline');
  svAssert.equal(
    (runtimeJs.match(/brewBaseline = snapshotControls\(/g) || []).length, 2,
    'The brew baseline must be captured on hydration as well as on clean polls');

  // Multi-error UI: inline marking per field with focus on the first offender
  // inside Settings, and the joined banner fallback when Settings is not the
  // active view (the Home quick-weight sheet path).
  {
    const svUiSrc = runtimeJs.slice(runtimeJs.indexOf('function markFieldError('),
      runtimeJs.indexOf('function clearMessage('));
    const svUiRun = (view) => {
      const state = {classes: [], focused: 0, opened: [], banner: []};
      const details = {open: false, parentElement: {closest: () => null}};
      const field = {
        classList: {add: (c) => state.classes.push(c)},
        closest: (sel) => (sel === 'details' ? details : null),
        parentElement: {appendChild: () => {}},
        focus() { state.focused++; },
        scrollIntoView() {},
      };
      const ui = new Function('$', 'document', 'message', 'clearFieldErrors',
        'activeView', svUiSrc + ';return {showFieldError, showFieldErrors};')(
        () => field, {createElement: () => ({className: '', textContent: ''})},
        (text, kind) => state.banner.push([text, kind]), () => {}, view);
      ui.showFieldErrors([
        {id: 'goalWeightG', msg: 'one.'},
        {id: 'minRecoveryWeightG', msg: 'two.'},
      ]);
      return {state, details};
    };
    const svAway = svUiRun('home');
    svAssert.deepEqual(svAway.state.banner, [['one. two.', 'error']]);
    svAssert.equal(svAway.state.classes.length, 0,
      'Away from Settings the fields must not be marked inline');
    const svIn = svUiRun('settings');
    svAssert.deepEqual(svIn.state.classes, ['invalid', 'invalid']);
    svAssert.equal(svIn.details.open, true, 'The section must unfold to reveal the fields');
    svAssert.equal(svIn.state.focused, 1, 'Only the first offender takes focus');
    svAssert.deepEqual(svIn.state.banner, [['one. two.', 'error']]);
  }
}
