// Quick Settings write coalescing: the shared autosave engine drives the
// header panel — a trailing debounce coalesces rapid taps, a tap toggled back
// to the confirmed value never transmits, multi-field patches serialize one
// PUT per field on each other's readback (so every request carries the bumped
// baseRevision) and stay SAVE_GAP_MS apart, failures stay pending, a cycle
// drops unsent edits, and revertNow bypasses the debounce.
{
  const assert = require('assert').strict, vm = require('vm');
  assert(codeIncludes(viewJs.settings, 'autosave[sec]=R.createAutosave(sec,section)'),
      'The Settings sections must consume the shared autosave engine');
  const run = async () => {
    const GUARDS = ['slowExtractionGuardEnabled', 'fastExtractionGuardEnabled',
      'noScaleBbwMode', 'autoToManualGuardEnabled', 'avoidAccidentalTouchEnabled',
      'cupProtectionEnabled'];
    const mkButton = (dataset) => ({tagName: 'BUTTON', dataset, disabled: false,
      attributes: {}, classList: {toggle() {}},
      setAttribute(name, value) {this.attributes[name] = String(value);},
      getAttribute(name) {return this.attributes[name];}});
    const quickEls = [
      mkButton({quick: 'brewByWeight', value: 'true'}), mkButton({quick: 'brewByWeight', value: 'false'}),
      ...GUARDS.map(k => mkButton({quick: k}))];
    const revertEls = ['next_shot', '15m', '30m', '60m', 'keep'].map(
      after => mkButton({revert: after}));
    const el = (k) => quickEls.find(e => e.dataset.quick === k && e.dataset.value === undefined);
    const els = {headerSignals: {dataset: {},
        querySelectorAll: (sel) => sel === '[data-revert]' ? revertEls : quickEls},
      quickSignal: {}, guardSignal: {dataset: {}}, quickStatus: {},
      quickRevertNote: {}, quickRevertNow: {}};
    const config = {brewByWeight: true, noScaleBbwMode: 'warn_once'};
    for (const k of GUARDS) if (k !== 'noScaleBbwMode') config[k] = true;
    let status = {config, quickRevert: {after: '15m', overrides: 1}};
    // A virtual clock far from epoch zero (so the engine's first gap never
    // engages) shared by timers and Date.now; settle() drains every timer the
    // flushed pipeline re-arms.
    let now = 100000, seq = 0;
    const timers = new Map();
    const drain = async () => { for (let i = 0; i < 24; ++i) await Promise.resolve(); };
    const advance = async (ms) => {
      const end = now + ms;
      for (;;) {
        const due = [...timers.entries()].filter(([, t]) => t.at <= end).sort((a, b) => a[1].at - b[1].at)[0];
        if (!due) break;
        now = due[1].at; timers.delete(due[0]); due[1].fn(); await drain();
      }
      now = end; await drain();
    };
    const settle = async () => {
      for (let i = 0; timers.size && i < 50; ++i) {
        const next = [...timers.values()].sort((a, b) => a[1].at - b[1].at)[0];
        await advance(next.at - now);
      }
    };
    const puts = [], errors = [], failures = [];
    let readbackGate = null;
    const c = vm.createContext({$: id => els[id], document: {},
      __WEBUI_TEXT__: key => key, QUICK_GUARDS: GUARDS, uiState: {},
      controlsMutable: true, configRevision: 10, lastCommandStatus: null, homeStale: false,
      Date: {now: () => now},
      setTimeout: (fn, ms) => {const id = ++seq; timers.set(id, {fn, at: now + ms}); return id;},
      clearTimeout: (id) => timers.delete(id),
      body: JSON.stringify, message: (text) => errors.push(text),
      // The readback wait stands in for the confirming frame: it applies the
      // accepted fields to the model exactly as a Home frame would.
      awaitCommandReadback: async () => {
        if (readbackGate) await readbackGate.promise;
        const applied = {...status.config};
        for (const put of puts) for (const [k, v] of Object.entries(put))
          if (k !== 'baseRevision' && k !== 'revertNow') applied[k] = v;
        status = {...status, config: applied};
        c.updateQuickSettings(status);
        return {applied: true, polled: false};
      },
      api: async (url, options) => {
        if (failures.length) throw Object.assign(new Error('rejected'), failures.shift());
        const put = JSON.parse(options.body);
        puts.push(put);
        c.configRevision += 1;
        return {requestId: c.configRevision};
      }});
    vm.runInContext(runtimeJs.slice(runtimeJs.indexOf('const SAVE_GAP_MS'),
        runtimeJs.indexOf('function initHeaderSignals('))
        .replace(/^export /gm, ''), c);
    const tap = (k) => c.setQuickSetting(el(k));
    const commit = async () => { await advance(300); await settle(); };
    c.updateQuickSettings(status);

    // Rapid taps on one guard coalesce to one PUT carrying the final value.
    tap('slowExtractionGuardEnabled');
    tap('slowExtractionGuardEnabled');
    tap('slowExtractionGuardEnabled');
    assert.equal(puts.length, 0, 'An edit waits for its pause before saving');
    await commit();
    assert.equal(puts.length, 1);
    assert.deepEqual(puts[0], {slowExtractionGuardEnabled: false, baseRevision: 10});
    assert(!c.uiState.quickDirty);

    // A tap toggled back to the confirmed value sends nothing at all.
    tap('fastExtractionGuardEnabled');
    tap('fastExtractionGuardEnabled');
    await commit();
    assert.equal(puts.length, 1, 'A guard toggled back inside the window never transmits');

    // Two fields in one window serialize: the second PUT rides the readback's
    // bumped revision and the SAVE_GAP_MS spacing.
    tap('slowExtractionGuardEnabled');
    tap('cupProtectionEnabled');
    await commit();
    assert.equal(puts.length, 3);
    assert.deepEqual(puts[1], {slowExtractionGuardEnabled: true, baseRevision: 11});
    assert.deepEqual(puts[2], {cupProtectionEnabled: false, baseRevision: 12});

    // A failed PUT stays pending; the toggled-back retry sends nothing.
    failures.push({code: 'INVALID_FIELD'});
    tap('avoidAccidentalTouchEnabled');
    await commit();
    assert.equal(puts.length, 3);
    assert.equal(errors.at(-1), 'rejected');
    assert(c.uiState.quickDirty, 'A failed quick send stays pending');
    tap('avoidAccidentalTouchEnabled');
    await commit();
    assert.equal(puts.length, 3, 'The retried edit toggled back sends nothing');
    assert(!c.uiState.quickDirty);
    tap('avoidAccidentalTouchEnabled');
    await commit();
    assert.equal(puts.length, 4);
    assert.deepEqual(puts.at(-1), {avoidAccidentalTouchEnabled: false, baseRevision: 13});

    // A stale revision retries by itself once the moved revision arrives.
    failures.push({code: 'STALE_REVISION'});
    tap('cupProtectionEnabled');
    await commit();
    assert.equal(puts.length, 4);
    assert.equal(errors.at(-1), 'rejected');
    c.configRevision += 1; // the other writer's frame lands
    c.updateQuickSettings(status);
    await commit();
    assert.equal(puts.length, 5);
    assert.deepEqual(puts.at(-1), {cupProtectionEnabled: true, baseRevision: 15},
        'The frame-triggered retry carries the refreshed revision');

    // A cycle drops unsent edits instead of queueing them behind the lock.
    tap('slowExtractionGuardEnabled');
    c.updateQuickSettings({...status, cycle: {active: true}});
    assert(!c.uiState.quickDirty, 'A cycle start drops pending quick edits');
    assert(el('slowExtractionGuardEnabled').disabled, 'The panel locks during a cycle');
    await commit();
    assert.equal(puts.length, 5);

    // revertNow commits at once and discards every pending edit.
    c.updateQuickSettings(status);
    tap('slowExtractionGuardEnabled');
    await c.putQuickReset();
    assert.equal(puts.length, 6);
    assert.deepEqual(puts.at(-1), {revertNow: true, baseRevision: 16});
    await commit();
    assert.equal(puts.length, 6, 'The discarded edit never fires after the reset');

    // Pending edits keep the buttons enabled; only a send in flight locks them.
    let release;
    readbackGate = {promise: new Promise(resolve => {release = resolve;})};
    tap('fastExtractionGuardEnabled');
    assert(!el('fastExtractionGuardEnabled').disabled, 'A pending edit does not lock the panel');
    await advance(300);
    assert(el('fastExtractionGuardEnabled').disabled, 'A send in flight locks the panel');
    release();
    await drain();
    await settle();
    assert(!el('fastExtractionGuardEnabled').disabled);
    assert.deepEqual(puts.at(-1), {fastExtractionGuardEnabled: false, baseRevision: 17});
  };
  run().catch(error => {console.error(error); process.exitCode = 1;});
}
