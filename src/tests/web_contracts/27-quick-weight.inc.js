// Quick target-weight sheet: the hero pencil, the one-shot ?edit_weight URL,
// and the active preset row's Target text are the invocation points, and
// closing the sheet commits through the proven settings-hydrate → preset-save
// pipeline with the firmware's decigram 10.0–200.0 g preset goal contract.
{
  const qwAssert = require('assert').strict;
  const qwHomeHtml = partialHtml.home,
    qwHomeJs = viewJs.home;
  const qwCssRule = (name) => css.match(new RegExp('\\.' + name + '\\{[^}]*\\}'))?.[0] || '';
  const qwFirst = (needle) => qwHomeJs.indexOf(needle);

  qwAssert.ok(codeIncludes(qwHomeHtml,
      '<span id="shotHeroGoal"></span><button id="shotGoalEdit" class="qeditPen"'),
    'The pencil must sit directly after the hero goal text');
  qwAssert.ok(/<div id="qwBackdrop" class="qwBackdrop" inert>/.test(qwHomeHtml) &&
      /<section id="qwSheet" class="qwSheet" role="dialog" aria-modal="true"/.test(qwHomeHtml),
    'Sheet and backdrop must ship inert until opened');
  for (const id of ['qwGrab', 'qwDone', 'qwHead', 'qwPreset', 'qwNum', 'qwRatio', 'qwDef',
    'qwReset', 'qwMinus', 'qwRange', 'qwPlus'])
    qwAssert.ok(qwHomeHtml.includes('id="' + id + '"'), 'Sheet element missing: ' + id);
  qwAssert.ok(/id="qwPreset".*<\/p><p class="qwLabel"/.test(qwHomeHtml) &&
      qwHomeHtml.indexOf('id="qwPreset"') < qwHomeHtml.indexOf('class="qwLabel"'),
    'The preset name must stack above the Target weight label');
  qwAssert.ok(/<input id="qwRange" type="range" min="10" max="200" step="0\.1"/.test(qwHomeHtml) &&
      qwHomeHtml.indexOf('id="qwMinus"') < qwHomeHtml.indexOf('id="qwRange"') &&
      qwHomeHtml.indexOf('id="qwRange"') < qwHomeHtml.indexOf('id="qwPlus"'),
    'The slider sits between −/+ and declares the firmware goal bounds in 0.1 g steps');
  qwAssert.ok(/<p class="qwLimit" id="qwLimitMin" hidden>/.test(qwHomeHtml) &&
      /<p class="qwLimit" id="qwLimitMax" hidden>/.test(qwHomeHtml),
    'Both range-limit pills must ship hidden next to the value');

  // Limit pills reuse the firmware guard colors: Fast amber, Slow blue.
  qwAssert.ok(/--gFast:#d97706;--gSlow:#6492d7/.test(css) &&
      css.includes('.qwLimit.toneSlow{border-color:var(--gSlow)') &&
      css.includes('.qwLimit.toneFast{border-color:var(--gFast)'),
    'Limit pills must reuse the firmware Fast/Slow colors');
  qwAssert.ok(/border-radius:2rem/.test(qwCssRule('qwLimit')) &&
      qwCssRule('guardFast i').includes('var(--gFast)') &&
      qwCssRule('guardSlow i').includes('var(--gSlow)'),
    'Limit pills use the chip format; guard dots share the zone color source');

  // The slider bounds must match the settings field the commit writes and the
  // constants the drag clamps to.
  const qwSettingsGoal = settingsHtml.match(
      /id="goalWeightG"[^>]*\bmin="(\d+)"[^>]*\bmax="(\d+)"/);
  qwAssert.ok(qwSettingsGoal, 'Settings goalWeightG input must declare min/max');
  qwAssert.ok(codeIncludes(qwHomeJs, 'const MIN = 10, MAX = 200') &&
      qwSettingsGoal[1] === '10' && qwSettingsGoal[2] === '200',
    'Sheet bounds must equal the firmware preset goal bounds');

  // Commit order: clean (discard-or-cancel) and hydrate the settings form,
  // write the goal, save the preset, so unsaved Settings edits never ride along.
  // A rejected save must put the previous value back so no phantom edit
  // lingers in the form (the live warnings and the brew baseline would
  // otherwise describe a value the device never accepted).
  const qwHydrate = qwFirst('if (!(await R.cleanBrewForm())) return;'),
    qwWrite = qwFirst('field.value = (dg / 10).toFixed(1)'),
    qwSave = qwFirst('R.saveBrewPreset(undefined, undefined, true)'),
    qwRestore = qwFirst('field.value = rejected;');
  qwAssert.ok(0 <= qwHydrate && qwHydrate < qwWrite && qwWrite < qwSave,
    'Closing must hydrate, then write the goal field, then save the preset');
  qwAssert.ok(qwRestore > qwSave,
    'A rejected quick-weight save must restore the previous goal value');
  qwAssert.ok((runtimeJs.match(/export\s*\{[\s\S]*?\n\};/) || [''])[0]
      .replace(/\s/g, '').includes('cleanBrewForm'),
    'Runtime must export cleanBrewForm for the commit path');

  qwAssert.ok(codeIncludes(qwHomeJs, 'value = clamp(value + dir * (n > 20 ? 10 : 1));') &&
      codeIncludes(qwHomeJs, 'hold = setTimeout(step, n++ ? 70 : 400);'),
    'Holding −/+ must repeat a tenth at a time, then speed up to whole grams');
  qwAssert.ok(codeIncludes(qwHomeJs, 'resetBtn.classList.toggle("show", value !== openValue)'),
    'Reset must appear only while the value differs from the open value');
  qwAssert.ok(codeIncludes(qwHomeJs,
      '["BREW", "RINSE", "MANUAL_NO_SCALE"].includes(s.state)'),
    'A machine shot must close the sheet');

  // URL launcher: without shots the hero card is absent, so ?edit_weight=1 is
  // the entry point. It must wait for the active preset and the admin lock,
  // open once, and strip the one-shot parameter from the address bar.
  qwAssert.ok(codeIncludes(qwHomeJs,
      'new URLSearchParams(location.search).has("edit_weight")'),
    'The edit_weight URL parameter must arm the quick-weight launcher');
  qwAssert.ok(codeIncludes(qwHomeJs, 'quickWeight.open()') &&
      codeIncludes(qwHomeJs, 'preset.goalWeightG && pen && !pen.disabled'),
    'The armed launcher must open only once the preset is ready and unlocked');
  qwAssert.ok(codeIncludes(qwHomeJs,
      'history.replaceState(null, "", location.pathname)'),
    'Opening from the URL must strip the one-shot parameter');
  qwAssert.ok(codeIncludes(qwHomeJs, 'return { open, close };'),
    'The controller must expose open for the URL launcher');
  qwAssert.ok(codeIncludes(runtimeJs,
      'if (p.id === presetState.activeId && m.mode === "active" && svg) {') &&
      codeIncludes(qwHomeJs, 'e.target.closest(".presetAccEdit") && open()'),
    'Only the already-active preset row may offer the quick-weight pencil');

  qwAssert.ok(/position:fixed[^}]*bottom:0/.test(qwCssRule('qwSheet')) &&
      /backdrop-filter:blur\(18px\)/.test(css),
    'The sheet anchors to the viewport bottom with a frosted surface');
  qwAssert.ok(/\.qwStep button\{[^}]*touch-action:manipulation/.test(css),
    'The −/+ buttons must not trigger double-tap zoom while held');
  qwAssert.ok(/body\.qwOpen\{overflow:hidden\}/.test(css),
    'Page scroll must lock while the sheet is open');
  qwAssert.ok(/\.qeditPen:disabled\{display:none\}/.test(css),
    'A locked UI must hide the pencil');

  // Pencil visibility follows the live hero: only an idle, scaled shot with a
  // real goal offers quick editing, never full screen.
  {
    const qwHeroClasses = new Set();
    const qwRoot = {
      hidden: false,
      classList: {
        toggle(cls, on) { on ? qwHeroClasses.add(cls) : qwHeroClasses.delete(cls); },
        contains: (cls) => qwHeroClasses.has(cls),
      },
    };
    const qwElements = new Map();
    const qwLookup = (id) => {
      if (id === 'shotHero') return qwRoot;
      if (!qwElements.has(id))
        qwElements.set(id, {hidden: false, textContent: '', dataset: {}, style: {setProperty() {}},
          classList: {toggle() {}, contains() {return false;}}, setAttribute() {},
          replaceChildren() {}});
      return qwElements.get(id);
    };
    const qwPaint = new Function('$', 'buildShotSparkModel', 'formatShotEnded', 'ms',
      blockAt(runtimeJs, 'function verdictLabel(') + runtimeJs.slice(runtimeJs.indexOf('function renderShotHero('),
          runtimeJs.indexOf('function shotPresetName(')) + ';return renderShotHero;')(
        qwLookup, () => null, () => '', (v, n) => (v / 1000).toFixed(n));
    const qwCard = {live: false, weight: 36.2, goal: 36, elapsedMs: 36800,
      firstDropMs: null, averageFlowGps: null, presetName: 'Espresso'};
    qwPaint(qwCard);
    qwAssert.equal(qwLookup('shotGoalEdit').hidden, false,
      'An idle hero with a goal must offer the pencil');
    qwPaint({...qwCard, live: true});
    qwAssert.equal(qwLookup('shotGoalEdit').hidden, true, 'A live shot must hide the pencil');
    qwPaint({...qwCard, goal: 0});
    qwAssert.equal(qwLookup('shotGoalEdit').hidden, true, 'No goal must hide the pencil');
    qwPaint({...qwCard, scaleAvailable: false, weight: null, elapsedMs: 27400});
    qwAssert.equal(qwLookup('shotGoalEdit').hidden, true,
      'No-scale timer mode must hide the pencil');
    qwHeroClasses.add('fs');
    qwPaint(qwCard);
    qwAssert.equal(qwLookup('shotGoalEdit').hidden, true,
      'The full-screen hero must not host the pencil');
  }

  // Execute the real sheet controller against a stub DOM: open, slider and
  // −/+ steps, clamping, Reset, commit-on-close, and the drag-to-dismiss
  // grab row.
  (async () => {
    const qwNode = (extra) => {
      const node = Object.assign({
        className: '',
        innerHTML: '',
        hidden: false,
        inert: false,
        value: '',
        children: [],
        style: {},
        classes: new Set(),
        listeners: {},
        appendChild(child) { this.children.push(child); },
        setAttribute() {},
        addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); },
        removeEventListener(type, fn) {
          const at = (this.listeners[type] || []).indexOf(fn);
          if (at >= 0) this.listeners[type].splice(at, 1);
        },
        setPointerCapture() {},
        focus() { this.focused = true; },
        getBoundingClientRect() { return {width: 320}; },
        classList: null,
        _text: '',
      }, extra);
      Object.defineProperty(node, 'textContent', {
        get() { return node._text; },
        set(v) { node._text = String(v); },
      });
      return node;
    };
    const qwClassSet = (node) => ({
      add: (...c) => c.forEach((x) => node.classes.add(x)),
      remove: (...c) => c.forEach((x) => node.classes.delete(x)),
      contains: (c) => node.classes.has(c),
      toggle: (c, on) => { on === undefined
        ? node.classes.has(c) ? node.classes.delete(c) : node.classes.add(c)
        : on ? node.classes.add(c) : node.classes.delete(c); },
    });
    const qwIds = ['qwSheet', 'qwBackdrop', 'qwMinus', 'qwRange', 'qwPlus', 'qwNum', 'qwRatio', 'qwDef',
      'qwReset', 'qwPreset', 'qwDone', 'qwGrab', 'qwHead', 'shotGoalEdit',
      'goalWeightG', 'qwLimitMin', 'qwLimitMax'];
    const qwEls = Object.fromEntries(qwIds.map((id) => {
      const node = qwNode();
      node.classList = qwClassSet(node);
      return [id, node];
    }));
    let qwNow = 0;
    const qwRuntime = {
      presetState: {activeId: 2, items: [{id: 2, name: 'Espresso', goalWeightG: 36, doseG: 18}]},
      cleanBrewForm: async () => { qwRuntime.hydrated++; return true; },
      saveBrewPreset: async () => { qwRuntime.saved++; return true; },
      message: () => {},
      formatCommandError: (m) => m,
      hydrated: 0,
      saved: 0,
    };
    const qwAccListeners = {};
    const qwInit = new Function('R', '$', 'document', 'addEventListener', 'performance',
      viewJs.home.slice(viewJs.home.indexOf('function initQuickWeight(')) +
      ';return initQuickWeight;')(
        qwRuntime, (id) => qwEls[id],
        {body: (() => { const body = qwNode(); body.classList = qwClassSet(body); return body; })(),
        createElement: () => {
          const node = qwNode();
          node.classList = qwClassSet(node);
          return node;
        }},
        (type, fn) => { (qwAccListeners[type] ||= []).push(fn); },
        {now: () => qwNow});
    const qwFire = (node, type, event) =>
      (node.listeners[type] || []).forEach((fn) => fn(Object.assign(
        {pointerType: 'mouse', button: 0, pointerId: 1, preventDefault() {}}, event)));
    const qwSlide = (g) => {
      qwEls.qwRange.value = String(g);
      qwEls.qwRange.oninput();
    };
    const qwTap = (id) => {
      qwEls[id].onpointerdown({preventDefault() {}});
      qwEls[id].onpointerup();
    };

    const qwCtl = qwInit();
    qwEls.shotGoalEdit.onclick();
    qwAssert.ok(qwEls.qwSheet.classes.has('open'), 'The pencil must open the sheet');
    qwAssert.equal(qwEls.qwNum.textContent, '36.0', 'The sheet opens at the active preset goal');
    qwAssert.equal(qwEls.qwPreset.textContent, 'Espresso', 'The header shows the preset name');
    qwAssert.equal(qwEls.qwRatio.textContent, '1:2.0 · 18.0 g in', 'The sheet shows ratio and dose');
    qwAssert.ok(!qwEls.qwReset.classes.has('show'), 'Reset stays hidden at the preset goal');
    qwAssert.ok(qwEls.qwDone.focused, 'Opening focuses Done');

    // The slider moves the value; −/+ taps step a tenth.
    qwSlide(38.4);
    qwAssert.equal(qwEls.qwNum.textContent, '38.4', 'The slider sets the target');
    qwAssert.equal(qwEls.qwRatio.textContent, '1:2.1 · 18.0 g in', 'The ratio follows the target');
    qwAssert.ok(qwEls.qwReset.classes.has('show'), 'Editing away from the goal reveals Reset');
    qwAssert.equal(qwEls.qwDef.textContent, '36.0', 'Reset offers the open value');
    qwEls.qwReset.onclick();
    qwAssert.equal(qwEls.qwNum.textContent, '36.0', 'Reset restores the open value');
    qwAssert.ok(!qwEls.qwReset.classes.has('show'), 'Reset hides again at the open value');
    qwTap('qwPlus');
    qwAssert.equal(qwEls.qwNum.textContent, '36.1', 'A + tap steps up 0.1 g');
    qwTap('qwMinus');
    qwTap('qwMinus');
    qwAssert.equal(qwEls.qwNum.textContent, '35.9', 'A − tap steps down 0.1 g');
    qwAssert.equal(qwEls.qwRange.value, '35.9', 'The slider follows the buttons');

    // Bounds: the firmware goal range clamps however far the slider runs.
    qwSlide(250);
    qwAssert.equal(qwEls.qwNum.textContent, '200.0', 'The slider clamps at the 200 g ceiling');
    qwSlide(1);
    qwAssert.equal(qwEls.qwNum.textContent, '10.0', 'The slider clamps at the 10 g floor');

    // Keyboard on the buttons steps without pointers.
    qwEls.qwReset.onclick();
    qwEls.qwPlus.onkeydown({key: 'Enter', preventDefault() {}});
    qwAssert.equal(qwEls.qwNum.textContent, '36.1', 'Enter on + steps by 0.1 g');
    qwEls.qwReset.onclick();

    // Closing without changes must not hit the save pipeline.
    qwFire(qwEls.qwSheet, 'keydown', {key: 'Escape'});
    qwAssert.ok(!qwEls.qwSheet.classes.has('open'), 'Escape closes the sheet');
    await new Promise((r) => setTimeout(r, 0));
    qwAssert.equal(qwRuntime.hydrated, 0, 'An unchanged close must not hydrate or save');

    // The URL launcher path: open() reopens at the preset goal without a hero.
    qwCtl.open();
    qwAssert.ok(qwEls.qwSheet.classes.has('open'),
      'The exposed open must launch the sheet without the hero pencil');
    qwAssert.equal(qwEls.qwNum.textContent, '36.0', 'A programmatic open starts at the preset goal');
    qwFire(qwEls.qwSheet, 'keydown', {key: 'Escape'});
    await new Promise((r) => setTimeout(r, 0));

    // The pencil on the active row's target is the third invocation point;
    // only the active row renders it, and taps elsewhere never open the sheet.
    const qwAccClick = (target) =>
      (qwAccListeners.click || []).forEach((fn) => fn({target}));
    qwAccClick({closest: () => null});
    qwAssert.ok(!qwEls.qwSheet.classes.has('open'),
      'Taps off the pencil must not open the sheet');
    qwAccClick({closest: (sel) => (sel === '.presetAccEdit' ? {} : null)});
    qwAssert.ok(qwEls.qwSheet.classes.has('open'),
      'The active row pencil must open the sheet');
    qwAssert.equal(qwEls.qwNum.textContent, '36.0',
      'The sheet still opens at the active preset goal');
    qwFire(qwEls.qwSheet, 'keydown', {key: 'Escape'});
    await new Promise((r) => setTimeout(r, 0));

    // Done commits the changed value through hydrate → goal field → save.
    qwEls.shotGoalEdit.onclick();
    qwSlide(40);
    const qwEdited = qwEls.qwNum.textContent;
    qwEls.qwDone.onclick();
    qwAssert.ok(!qwEls.qwSheet.classes.has('open'), 'Done closes the sheet');
    await new Promise((r) => setTimeout(r, 0));
    qwAssert.equal(qwRuntime.hydrated, 1, 'Done must hydrate the settings form');
    qwAssert.equal(qwEls.goalWeightG.value, qwEdited,
      'Done must write the one-decimal goal into goalWeightG');
    qwAssert.equal(qwRuntime.saved, 1, 'Done must save the brew preset');

    // A long grab-row drag dismisses and still commits an edited value.
    qwEls.shotGoalEdit.onclick();
    qwSlide(41.5);
    const qwDismissed = qwEls.qwNum.textContent;
    qwFire(qwEls.qwGrab, 'pointerdown', {clientY: 500});
    qwFire(qwEls.qwGrab, 'pointermove', {clientY: 660});
    qwFire(qwEls.qwGrab, 'pointerup', {});
    await new Promise((r) => setTimeout(r, 320));
    qwAssert.ok(!qwEls.qwSheet.classes.has('open'),
      'A long grab drag must dismiss the sheet');
    qwAssert.equal(qwRuntime.saved, 2, 'Dismiss must commit the pending edit');
    qwAssert.equal(qwEls.goalWeightG.value, qwDismissed,
      'Dismiss must write the edited goal before saving');

    // A short grab wiggle never dismisses.
    qwEls.shotGoalEdit.onclick();
    qwFire(qwEls.qwGrab, 'pointerdown', {clientY: 500});
    qwFire(qwEls.qwGrab, 'pointermove', {clientY: 540});
    qwFire(qwEls.qwGrab, 'pointerup', {});
    await new Promise((r) => setTimeout(r, 0));
    qwAssert.ok(qwEls.qwSheet.classes.has('open'),
      'A short grab drag must keep the sheet open');
    qwAssert.equal(qwRuntime.saved, 2, 'An unchanged sheet must not save on wiggle');

    // Margin-bounded range: with both sides on the dial offers only targets
    // the firmware accepts for these margins (target ≥ 4 × extra yield and
    // ≥ less yield + 5 g; target + extra yield ≤ 200 g), the limit pills mark
    // the two ends, and the ruler tints reference zones outside the range.
    qwRuntime.presetState.items = [{
      id: 2, name: 'Espresso', goalWeightG: 36,
      slowExtractionGuardEnabled: true, slowMarginG: 3,
      fastExtractionGuardEnabled: true, fastMarginG: 8,
    }];
    qwEls.shotGoalEdit.onclick();
    qwAssert.equal(qwEls.qwNum.textContent, '36.0', 'The sheet opens at the in-range goal');
    qwAssert.equal(qwEls.qwRange.min, '32.0',
      'The slider floor keeps the extra yield within a quarter of the target');
    qwAssert.equal(qwEls.qwRange.max, '192.0',
      'The slider ceiling keeps target plus extra yield within 200 g');
    qwAssert.equal(qwEls.qwLimitMin.hidden, true, 'The floor pill starts hidden');
    qwAssert.equal(qwEls.qwLimitMax.hidden, true, 'The ceiling pill starts hidden');
    qwSlide(5);
    qwAssert.equal(qwEls.qwNum.textContent, '32.0',
      'The slider clamps at the lowest target the margins allow');
    qwAssert.equal(qwEls.qwLimitMin.hidden, false,
      'The floor pill appears at the bottom of the target range');
    qwAssert.ok(qwEls.qwLimitMin.classes.has('toneSlow') &&
        qwEls.qwLimitMax.classes.has('toneFast'),
      'Limit pills adopt the tint of the side that bounds them');
    qwEls.qwReset.onclick();
    qwFire(qwEls.qwSheet, 'keydown', {key: 'Escape'});
    await new Promise((r) => setTimeout(r, 0));
    qwAssert.equal(qwRuntime.saved, 2,
      'A guard-bounded session closed without edits must not save');
  })().catch((error) => { console.error(error); process.exitCode = 1; });
}
