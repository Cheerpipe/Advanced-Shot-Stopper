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
    svCut('const sub = (t, vals)', 'function validateDateTimeClient') +
    // The live-warning twin sits before the rule table in the source; the
    // harness compiles it in so the executable cases below run the exact
    // production filter, not a copy.
    svCut('function brewWarningList(', 'function clearBrewWarnings(');
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
      svValidatorSrc +
      ';return {validateBrewClient, brewWarningList, rules: BREW_CROSS_RULES};')(
      (id) => fields[id],
      {documentElement: {classList: {contains: () => false}}},
      (section) => (section === 'brew' ? Object.values(fields) : []),
      baseline);
    return {errors: api.validateBrewClient(), warnings: api.brewWarningList(),
            rules: api.rules};
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

  // An emptied participant reports only its own required-value error: the
  // cross rules must not compare against Number("") === 0.
  const svEmpty = svValidate((f) => {
    f.goalWeightG.value = '';
  });
  svAssert.equal(svEmpty.errors.length, 1, JSON.stringify(svEmpty.errors));
  svAssert.equal(svEmpty.errors[0].msg, 'Target is required (10–200 g).');

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
  svAssert.ok(codeIncludes(runtimeJs, 'const errs = validateBrewClient(); if (errs.length) {') &&
      codeIncludes(runtimeJs, 'clearBrewWarnings(); showFieldErrors(errs); return false; }'),
    'saveBrewPreset must render the full violation list, replacing the yellow preview');
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
        attrs: {},
        setAttribute(k, v) { this.attrs[k] = v; },
        getAttribute() { return null; },
        removeAttribute() {},
      };
      const ui = new Function('$', 'document', 'message', 'clearFieldErrors',
        'activeView', svUiSrc + ';return {showFieldError, showFieldErrors};')(
        () => field, {createElement: () => ({className: '', textContent: ''})},
        (text, kind) => state.banner.push([text, kind]), () => {}, view);
      ui.showFieldErrors([
        {id: 'goalWeightG', msg: 'one.'},
        {id: 'minRecoveryWeightG', msg: 'two.'},
      ]);
      return {state, details, field};
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
    svAssert.equal(svIn.field.attrs['aria-invalid'], 'true',
      'Red errors must mark their input for assistive tech');
    svAssert.ok(/^fieldErr\d/.test(svIn.field.attrs['aria-describedby']),
      'Red errors must point aria-describedby at their message');
  }

  // Live warnings (yellow preview of the save rejection): the executable
  // cases prove the warning set is exactly the validator's output minus the
  // empty-field "is required" results, resolves whole-set, and respects
  // guard gating; the engine harness proves signature-skip rendering and the
  // conflict chip.
  {
    // (w1) Editing only Target into conflict shows the same single verdict
    // the save would produce, and the empty-field result is suppressed.
    const w1 = svValidate((f) => {
      f.goalWeightG.value = '10';
    });
    svAssert.equal(w1.warnings.length, 1, JSON.stringify(w1.warnings));
    svAssert.equal(w1.warnings[0].msg,
      'Target 10 g must be greater than 34.0 g (Min recovery, Slow extraction guard).');
    const w1Empty = svValidate((f) => {
      f.goalWeightG.value = '';
    });
    svAssert.equal(w1Empty.errors.length, 1);
    svAssert.deepEqual(w1Empty.warnings, [],
      'The empty-field "is required" result must not warn live');

    // (w2) Resolving the other side clears the whole set without saving.
    const w2 = svValidate((f) => {
      f.goalWeightG.value = '33';
      f.minRecoveryWeightG.value = '32';
    });
    svAssert.deepEqual(w2.warnings, [], JSON.stringify(w2.warnings));

    // (w3) A disabled guard keeps producing no warnings at all.
    const w3 = svValidate((f) => {
      f.fastExtractionGuardEnabled.checked = false;
      f.maxRecoveryWeightG.value = '5';
    });
    svAssert.deepEqual(w3.warnings, []);

    // (w4)/(w5) Engine harness: count DOM writes across two unchanged runs
    // (signature skip) and check the chip text/count and hidden-at-zero.
    const strings = JSON.parse(
      fs.readFileSync(path.join(sketchDir, 'web/locales/en.json'), 'utf8')).strings;
    const engineSrc = runtimeJs.slice(runtimeJs.indexOf('let brewWarnSig'),
      runtimeJs.indexOf('function revertBrewPreset()'));
    const mkField = (id) => {
      const el = {id, attrs: {},
        classList: {add: () => {}, remove: () => {}},
        setAttribute(k, v) { el.attrs[k] = v; },
        getAttribute(k) { return k in el.attrs ? el.attrs[k] : null; },
        removeAttribute(k) { delete el.attrs[k]; },
        closest: () => ({appendChild: () => harness.counts.appended++,
                         querySelectorAll: () => []})};
      return el;
    };
    const harness = {
      counts: {created: 0, appended: 0, chipTexts: []},
      chip: {textContent: '', hidden: true,
        classList: {add: () => {}, remove: () => {},
          toggle: (_c, on) => { harness.chip.hidden = on; }}},
      fields: {
        goalWeightG: null, minRecoveryWeightG: null, brewConflictChip: null,
      },
    };
    harness.fields.goalWeightG = mkField('goalWeightG');
    harness.fields.minRecoveryWeightG = mkField('minRecoveryWeightG');
    harness.fields.brewConflictChip = harness.chip;
    const script = [
      [{id: 'goalWeightG', msg: 'one.'}],
      [{id: 'goalWeightG', msg: 'one.'}],
      // Same field twice: the validator does emit two results for one input
      // (self-range + cross rule), and both smalls must stay aria-linked.
      [{id: 'goalWeightG', msg: 'one.'}, {id: 'goalWeightG', msg: 'two.'}],
      [],
    ];
    let call = 0;
    const engine = new Function('$', 'document', 'validateBrewClient',
      '__WEBUI_TEXT__', 'settingsSectionEls',
      engineSrc + ';return {refreshBrewWarnings, clearBrewWarnings};')(
      (id) => harness.fields[id],
      {querySelectorAll: () => [],
       createElement: () => { harness.counts.created++; return {}; }},
      () => script[Math.min(call, script.length - 1)],
      (k) => strings[k],
      () => []);
    engine.refreshBrewWarnings();
    const createdAfterFirst = harness.counts.created;
    call = 1;
    engine.refreshBrewWarnings();
    svAssert.equal(harness.counts.created, createdAfterFirst,
      'An unchanged warning signature must not write the DOM');
    call = 2;
    // The production clear (clearBrewWarningsDom) resets the pairing via
    // refreshFieldAria before re-rendering; the flat DOM stub cannot model
    // class queries, so reset the recorded attrs the same way here.
    harness.fields.goalWeightG.attrs = {};
    engine.refreshBrewWarnings();
    svAssert.equal(harness.counts.created, createdAfterFirst + 2);
    svAssert.equal(harness.chip.hidden, false);
    svAssert.equal(harness.chip.textContent, '2 conflicts will block saving.');
    svAssert.match(harness.fields.goalWeightG.attrs['aria-describedby'],
      /^brewWarn\d+ brewWarn\d+$/,
      'Two warnings on one field must both stay aria-linked');
    call = 3;
    engine.refreshBrewWarnings();
    svAssert.equal(harness.chip.hidden, true, 'The chip must hide at zero conflicts');
    svAssert.ok(!engineSrc.match(/\bmessage\(/),
      'The live-warning path must stay inline-only (no banner)');
    svAssert.ok(strings['settings.conflict_will_block_saving'] &&
                strings['settings.conflicts_will_block_saving'],
      'Chip locale keys must exist');
  }

  // (w6) The aria rebuild branch: a red small that survives the brew-scoped
  // clear keeps its id in the rebuilt pairing (the flat w4 stub cannot
  // exercise refreshFieldAria's query, so drive it through the real
  // clearBrewFieldErrors with a host that reports one surviving red small).
  {
    const engineSrc = runtimeJs.slice(runtimeJs.indexOf('let brewWarnSig'),
      runtimeJs.indexOf('function revertBrewPreset()'));
    const survivor = {id: 'fieldErr7', remove: () => {}};
    const el = {id: 'goalWeightG', attrs: {},
      classList: {add: () => {}, remove: () => {}},
      setAttribute(k, v) { el.attrs[k] = v; },
      getAttribute(k) { return k in el.attrs ? el.attrs[k] : null; },
      removeAttribute(k) { delete el.attrs[k]; },
      closest: () => ({appendChild: () => {}, querySelectorAll: () => [survivor]})};
    el.attrs['aria-invalid'] = 'true';
    el.attrs['aria-describedby'] = 'fieldErr7 brewWarn9';
    const engine = new Function('$', 'document', 'validateBrewClient',
      '__WEBUI_TEXT__', 'settingsSectionEls',
      engineSrc + ';return {clearBrewFieldErrors};')(
      () => null, {querySelectorAll: () => [], createElement: () => ({})},
      () => [], (k) => strings[k], () => [el]);
    engine.clearBrewFieldErrors();
    svAssert.equal(el.attrs['aria-describedby'], 'fieldErr7',
      'The rebuild must re-point at the surviving red small');
    svAssert.equal(el.attrs['aria-invalid'], 'true');
  }

  // Structural pins: wiring, styles, chip, hooks, and symmetric aria.
  {
    const settingsJsSrc = viewJs.settings || '';
    const engineSrc = runtimeJs.slice(runtimeJs.indexOf('let brewWarnSig'),
      runtimeJs.indexOf('function revertBrewPreset()'));
    svAssert.ok(codeIncludes(settingsJsSrc,
      'R.clearBrewFieldErrors();R.refreshBrewWarnings();'),
      'The shared settings listener must clear stale red marks and refresh warnings');
    svAssert.ok(codeIncludes(blockAt(runtimeJs, 'function applySettingsStatus(s) {'),
      'refreshBrewWarnings();'),
      'applySettingsStatus must refresh warnings after re-capturing baselines');
    svAssert.ok(codeIncludes(blockAt(runtimeJs, 'function revertBrewPreset() {'),
      'refreshBrewWarnings();'));
    svAssert.ok(codeIncludes(blockAt(runtimeJs, 'function revertMachineConfig() {'),
      'refreshBrewWarnings();'));
    svAssert.ok(codeIncludes(blockAt(runtimeJs, 'function saveBrewPreset('),
      'clearBrewWarnings();'),
      'A successful brew save must clear the warnings immediately');
    svAssert.ok(engineSrc.includes('aria-invalid') &&
                engineSrc.includes('aria-describedby'),
      'Yellow warnings must mark their input for assistive tech');
    svAssert.ok(blockAt(runtimeJs, 'function clearBrewFieldErrors(')
        .includes('small.fieldError'),
      'The scoped red clear must remove the brew smalls, not just the class');
    svAssert.ok(css.includes('.fieldWarning{') && css.includes('.fieldWarn{') &&
                rawCss.includes('.fieldWarning:before{content:__WEBUI_CSS_TEXT__("css.symbol")'),
      'Warning twins must exist with the shared alert glyph');
    svAssert.ok(settingsHtml.includes('id="brewConflictChip"'),
      'The brew save bar must carry the conflict chip');
  }
}
