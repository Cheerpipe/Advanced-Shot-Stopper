// Quick target-weight sheet: the hero pencil is the only invocation point and
// closing the sheet commits through the proven settings-hydrate → preset-save
// pipeline with the firmware's whole-gram 10–200 g preset goal contract.
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
  for (const id of ['qwGrab', 'qwDone', 'qwHead', 'qwPreset', 'qwNum', 'qwDef', 'qwReset',
    'qwZone', 'qwScale'])
    qwAssert.ok(qwHomeHtml.includes('id="' + id + '"'), 'Sheet element missing: ' + id);
  qwAssert.ok(/id="qwPreset".*<\/p><p class="qwLabel"/.test(qwHomeHtml) &&
      qwHomeHtml.indexOf('id="qwPreset"') < qwHomeHtml.indexOf('class="qwLabel"'),
    'The preset name must stack above the Target weight label');
  qwAssert.ok(/id="qwZone"[^>]*aria-valuemin="10"[^>]*aria-valuemax="200"/.test(qwHomeHtml),
    'The scale zone must declare the firmware goal bounds');

  // The slider bounds must match the settings field the commit writes and the
  // constants the drag clamps to.
  const qwSettingsGoal = settingsHtml.match(
      /id="goalWeightG"[^>]*\bmin="(\d+)"[^>]*\bmax="(\d+)"/);
  qwAssert.ok(qwSettingsGoal, 'Settings goalWeightG input must declare min/max');
  qwAssert.ok(codeIncludes(qwHomeJs, 'const MIN = 10, MAX = 200') &&
      qwSettingsGoal[1] === '10' && qwSettingsGoal[2] === '200',
    'Sheet bounds must equal the firmware preset goal bounds');

  // Commit order: hydrate the settings form, write the goal, save the preset.
  const qwHydrate = qwFirst('R.ensureSettingsHydrated()'),
    qwWrite = qwFirst('$("goalWeightG").value = String(grams)'),
    qwSave = qwFirst('R.saveBrewPreset()');
  qwAssert.ok(0 <= qwHydrate && qwHydrate < qwWrite && qwWrite < qwSave,
    'Closing must hydrate, then write the goal field, then save the preset');
  qwAssert.ok((runtimeJs.match(/export\s*\{[\s\S]*?\n\};/) || [''])[0]
      .replace(/\s/g, '').includes('ensureSettingsHydrated'),
    'Runtime must export ensureSettingsHydrated for the commit path');

  qwAssert.ok(codeIncludes(qwHomeJs, 'value = clamp(value + dx * BASE_G_PER_PX * speedMul(vel.v))') &&
      codeIncludes(qwHomeJs, 'BASE_G_PER_PX = 0.008, SPEED_CAP = 10'),
    'The drag must keep the accepted sensitivity and ×10 acceleration cap');
  qwAssert.ok(codeIncludes(qwHomeJs, 'if (v % 5 === 0) tick.className = "big"'),
    'The tick strip keeps 1 g ticks with 5 g majors');
  qwAssert.ok(codeIncludes(qwHomeJs, 'resetBtn.classList.toggle("show", shown !== openValue)'),
    'Reset must appear only while the value differs from the open value');
  qwAssert.ok(codeIncludes(qwHomeJs,
      '["BREW", "RINSE", "MANUAL_NO_SCALE"].includes(s.state)'),
    'A machine shot must close the sheet');

  qwAssert.ok(/position:fixed[^}]*bottom:0/.test(qwCssRule('qwSheet')) &&
      /backdrop-filter:blur\(18px\)/.test(css),
    'The sheet anchors to the viewport bottom with a frosted surface');
  qwAssert.ok(/touch-action:none/.test(qwCssRule('qwZone')),
    'The tick zone must own touch gestures');
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
        qwElements.set(id, {hidden: false, textContent: '', style: {setProperty() {}},
          classList: {toggle() {}, contains() {return false;}}, setAttribute() {},
          replaceChildren() {}});
      return qwElements.get(id);
    };
    const qwPaint = new Function('$', 'buildShotSparkModel', 'formatShotEnded', 'ms',
      runtimeJs.slice(runtimeJs.indexOf('function renderShotHero('),
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

  // Execute the real sheet controller against a stub DOM: open, pointer drag
  // with the accepted acceleration, clamping, Reset, commit-on-close, and the
  // drag-to-dismiss grab row.
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
    const qwIds = ['qwSheet', 'qwBackdrop', 'qwZone', 'qwScale', 'qwNum', 'qwDef',
      'qwReset', 'qwPreset', 'qwDone', 'qwGrab', 'qwHead', 'shotGoalEdit', 'goalWeightG'];
    const qwEls = Object.fromEntries(qwIds.map((id) => {
      const node = qwNode();
      node.classList = qwClassSet(node);
      return [id, node];
    }));
    let qwNow = 0;
    const qwRuntime = {
      presetState: {activeId: 2, items: [{id: 2, name: 'Espresso', goalWeightG: 36}]},
      ensureSettingsHydrated: async () => { qwRuntime.hydrated++; },
      saveBrewPreset: async () => { qwRuntime.saved++; },
      message: () => {},
      formatCommandError: (m) => m,
      hydrated: 0,
      saved: 0,
    };
    const qwInit = new Function('R', '$', 'document', 'addEventListener', 'performance',
      viewJs.home.slice(viewJs.home.indexOf('function initQuickWeight(')) +
      ';return initQuickWeight;')(
      qwRuntime, (id) => qwEls[id],
      {body: (() => { const body = qwNode(); body.classList = qwClassSet(body); return body; })(),
        createElement: () => {
          const node = qwNode();
          node.classList = qwClassSet(node);
          return node;
        }}, () => {}, {now: () => qwNow});
    const qwFire = (node, type, event) =>
      (node.listeners[type] || []).forEach((fn) => fn(Object.assign(
        {pointerType: 'mouse', button: 0, pointerId: 1, preventDefault() {}}, event)));
    const qwDragZone = (moves) => {
      qwFire(qwEls.qwZone, 'pointerdown', {clientX: 100});
      for (const [dx, dt] of moves) {
        qwNow += dt;
        qwFire(qwEls.qwZone, 'pointermove', {clientX: (qwDragZone.x = (qwDragZone.x || 100) + dx)});
      }
      qwFire(qwEls.qwZone, 'pointerup', {});
      qwDragZone.x = 0;
    };

    qwInit();
    qwEls.shotGoalEdit.onclick();
    qwAssert.ok(qwEls.qwSheet.classes.has('open'), 'The pencil must open the sheet');
    qwAssert.equal(qwEls.qwNum.textContent, '36', 'The sheet opens at the active preset goal');
    qwAssert.equal(qwEls.qwPreset.textContent, 'Espresso', 'The header shows the preset name');
    qwAssert.ok(!qwEls.qwReset.classes.has('show'), 'Reset stays hidden at the preset goal');
    qwAssert.ok(qwEls.qwDone.focused, 'Opening focuses Done');

    // Slow drags stay surgical; fast drags gear up. Replicate the accepted
    // velocity model to pin the exact deltas.
    qwDragZone([[10, 100], [10, 100], [10, 100], [10, 100], [10, 100], [10, 100]]);
    const qwExpectedSlow = (() => {
      let value = 36, v = 0, x = 100, t = 0;
      for (let i = 0; i < 6; i++) {
        v = 0.75 * v + 0.25 * (10 / 100);
        value += 10 * 0.008 * (1 + Math.min(9, Math.abs(v) * 14));
        x += 10; t += 100;
      }
      return Math.round(value);
    })();
    qwAssert.equal(qwEls.qwNum.textContent, String(qwExpectedSlow),
      'A slow drag must follow the accepted fine sensitivity');
    qwAssert.ok(qwEls.qwReset.classes.has('show'), 'Editing away from the goal reveals Reset');
    qwAssert.equal(qwEls.qwDef.textContent, '36', 'Reset offers the open value');
    qwEls.qwReset.onclick();
    qwAssert.equal(qwEls.qwNum.textContent, '36', 'Reset restores the open value');
    qwAssert.ok(!qwEls.qwReset.classes.has('show'), 'Reset hides again at the open value');

    qwDragZone([[50, 10], [50, 10], [50, 10], [50, 10], [50, 10]]);
    const qwFast = Number(qwEls.qwNum.textContent);
    qwAssert.ok(qwFast - 36 >= 15 && qwFast - 36 <= 21,
      'A fast drag must engage the ×10 gear (' + (qwFast - 36) + ' g)');

    // Bounds: the firmware goal range clamps however far the drag runs.
    qwEls.qwReset.onclick();
    qwDragZone([[600, 5], [600, 5], [600, 5], [600, 5]]);
    qwAssert.equal(qwEls.qwNum.textContent, '200', 'The drag clamps at the 200 g ceiling');
    qwEls.qwReset.onclick();
    qwDragZone([[-600, 5], [-600, 5], [-600, 5], [-600, 5]]);
    qwAssert.equal(qwEls.qwNum.textContent, '10', 'The drag clamps at the 10 g floor');

    // Keyboard stepping works without pointers.
    qwEls.qwReset.onclick();
    qwFire(qwEls.qwZone, 'keydown', {key: 'ArrowRight', preventDefault() {}});
    qwAssert.equal(qwEls.qwNum.textContent, '37', 'Arrow keys step by 1 g');
    qwFire(qwEls.qwZone, 'keydown', {key: 'ArrowLeft', preventDefault() {}});

    // Closing without changes must not hit the save pipeline.
    qwFire(qwEls.qwSheet, 'keydown', {key: 'Escape'});
    qwAssert.ok(!qwEls.qwSheet.classes.has('open'), 'Escape closes the sheet');
    await new Promise((r) => setTimeout(r, 0));
    qwAssert.equal(qwRuntime.hydrated, 0, 'An unchanged close must not hydrate or save');

    // Done commits the changed value through hydrate → goal field → save.
    qwEls.shotGoalEdit.onclick();
    qwDragZone([[50, 10], [50, 10], [50, 10], [50, 10], [50, 10]]);
    const qwEdited = qwEls.qwNum.textContent;
    qwEls.qwDone.onclick();
    qwAssert.ok(!qwEls.qwSheet.classes.has('open'), 'Done closes the sheet');
    await new Promise((r) => setTimeout(r, 0));
    qwAssert.equal(qwRuntime.hydrated, 1, 'Done must hydrate the settings form');
    qwAssert.equal(qwEls.goalWeightG.value, qwEdited,
      'Done must write the rounded goal into goalWeightG');
    qwAssert.equal(qwRuntime.saved, 1, 'Done must save the brew preset');

    // A long grab-row drag dismisses and still commits an edited value.
    qwEls.shotGoalEdit.onclick();
    qwDragZone([[50, 10], [50, 10], [50, 10], [50, 10], [50, 10]]);
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
  })().catch((error) => { console.error(error); process.exitCode = 1; });
}
