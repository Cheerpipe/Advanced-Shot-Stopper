// This standalone chunk follows the complete power contracts, not a split expression.
// Run real timezone UI functions with controlled async responses and clocks.
{
  const assert = require('assert').strict, vm = require('vm');
  const slice = (a, b) => rawRuntimeJs.slice(rawRuntimeJs.indexOf(a), rawRuntimeJs.indexOf(b));
  const source = slice('async function populateTimezoneOptions(', 'function renderShotSpark(') +
      slice('function loadAdminConfig(', 'function updateHomeAdminActions(') +
      slice('async function saveDateTimeConfig(', 'function commitRenamePreset(');
  const flush = async () => { for (let i = 0; i < 12; ++i) await Promise.resolve(); };
  const deferred = () => { let resolve; const promise = new Promise(r => { resolve = r; }); return {promise, resolve}; };
  function harness() {
    const select = {value: '', isConnected: true, options: [{}],
      replaceChildren(f) { this.options = f.children; this.value = ''; }};
    const els = {timezoneId: select, timezonePreview: {}, timezoneSaveState: {},
      ntpServerPreset: {value: 'pool'}, ntpServerCustom: {value: ''}};
    const calls = [], state = {zone: 'America/Santiago', commands: 0};
    const c = vm.createContext({$: id => els[id], Date, performance: {now: () => 1000},
      document: {hidden: false, createDocumentFragment: () => ({children: [], appendChild(o) { this.children.push(o); }})},
      Option: function(label, value) { this.value = value; },
      Intl: {DateTimeFormat: () => ({resolvedOptions: () => ({timeZone: state.zone})})},
      __WEBUI_TEXT__: key => key, formatWallTimeLocal: String, formatTzLabel: String,
      formatCommandError: (_, error) => error.message, body: JSON.stringify,
      setSaveDirty() {}, message() {}, clearFieldErrors() {}, showFieldError() {},
      validateDateTimeClient: () => null, withBaseRev: p => p,
      dateTimePayload: () => ({timezoneId: select.value, ntpServerPreset: 'pool', ntpServerCustom: ''}),
      markDateTimeDirty() { c.dateTimeDirty = true; }, refreshStatus: async () => {},
      dateTimeDirty: false, controlsMutable: true, diagnosticUnlocked: true, activeView: 'admin',
      api: async (url, options) => {
        if (url.endsWith('/zones')) return ['Etc/UTC', 'America/Santiago', 'Asia/Tokyo'];
        if (!c.diagnosticUnlocked) throw new Error('Admin locked');
        const {timezoneId} = JSON.parse(options.body); calls.push(timezoneId);
        return {timezoneId, clockAvailable: true, clockSource: 'device', utcSec: 1800000000,
          localSec: 1800000000, offsetMinutes: 0, nextTransitionUtcSec: 0};
      },
      command: async () => { ++state.commands; }
    });
    vm.runInContext(source, c);
    return {c, select, els, calls, state};
  }
  const cases = [
    ['manual redetection reads the current browser zone', async () => {
      const h = harness(); await h.c.populateTimezoneOptions();
      h.state.zone = 'Asia/Tokyo'; await h.c.detectTimezone();
      assert.equal(h.select.value, 'Asia/Tokyo');
    }],
    ['status changes refresh the selected-zone preview', async () => {
      const h = harness(); await h.c.populateTimezoneOptions(); await flush();
      h.c.loadAdminConfig({timezoneId: 'Asia/Tokyo'}); await flush();
      assert.equal(h.select.value, 'Asia/Tokyo');
      assert.equal(h.calls.at(-1), 'Asia/Tokyo');
    }],
    ['late previews cannot replace the newest selection', async () => {
      const h = harness(); await h.c.populateTimezoneOptions(); await flush();
      const old = deferred(), api = h.c.api;
      h.c.api = async (url, options) => {
        const result = await api(url, options);
        if (result.timezoneId === 'America/Santiago') await old.promise;
        return result;
      };
      h.select.value = 'America/Santiago'; const first = h.c.refreshTimezonePreview();
      h.select.value = 'Asia/Tokyo'; await h.c.refreshTimezonePreview();
      old.resolve(); await first;
      assert.equal(vm.runInContext('timezonePreviewAnchor.timezoneId', h.c), 'Asia/Tokyo');
    }],
    ['preview recovers after Admin unlock without a zone edit', async () => {
      const h = harness(); h.c.diagnosticUnlocked = false;
      await h.c.populateTimezoneOptions(); await flush();
      h.c.diagnosticUnlocked = true; h.c.loadAdminConfig({timezoneId: 'America/Santiago'});
      await flush(); assert.equal(h.calls.at(-1), 'America/Santiago');
    }],
    ['initial detection serializes catalog waiters', async () => {
      const h = harness(), gate = deferred(), api = h.c.api;
      h.c.api = async (url, options) => { if (url.endsWith('/zones')) await gate.promise; return api(url, options); };
      const a = h.c.maybeInitializeTimezone({timezoneId: ''});
      const b = h.c.maybeInitializeTimezone({timezoneId: ''});
      gate.resolve(); await Promise.all([a, b]); assert.equal(h.state.commands, 1);
    }],
    ['a lost initial save can be retried in the same browser', async () => {
      const h = harness(); await h.c.maybeInitializeTimezone({timezoneId: ''});
      // The device rebooted before the flash write and again reports no zone.
      await h.c.maybeInitializeTimezone({timezoneId: ''});
      assert.equal(h.state.commands, 2);
    }],
    ['saving a prior draft retains edits made while it was pending', async () => {
      const h = harness(), gate = deferred(); h.c.command = () => gate.promise;
      h.select.value = 'America/Santiago'; h.c.dateTimeDirty = true;
      const save = h.c.saveDateTimeConfig();
      h.select.value = 'Asia/Tokyo'; h.c.timeZoneSelectionChanged();
      gate.resolve(); await save; assert.equal(h.c.dateTimeDirty, true);
    }],
    ['a command rejection cannot be acknowledged by an unrelated revision', async () => {
      const code = slice('async function command(', 'async function setBleScanIntensity(');
      let polls = 0;
      const c = vm.createContext({configRevision: 10, lastCommandStatus: null,
        withCommandGate: fn => fn(), api: async () => ({requestId: 7}), body: JSON.stringify,
        clearFieldErrors() {}, noteReachOk() {}, message() {}, commandBusy: false,
        __WEBUI_TEXT__: key => key, setTimeout: fn => { fn(); },
        refreshStatus: async () => {
          ++polls; c.configRevision = 11;
          c.lastCommandStatus = polls === 1 ? {requestId: 6, state: 'APPLIED'} : {requestId: 7, state: 'FAILED'};
        }});
      vm.runInContext(code, c);
      await assert.rejects(c.command('/api/v1/config', {timezoneId: 'Asia/Tokyo'}, true, '', ''));
      assert.equal(polls, 2);
    }],
    ['command success waits for its own applied result', async () => {
      const code = slice('async function command(', 'async function setBleScanIntensity(');
      let polls = 0;
      const c = vm.createContext({configRevision: 10, lastCommandStatus: null,
        withCommandGate: fn => fn(), api: async () => ({requestId: 7}), body: JSON.stringify,
        clearFieldErrors() {}, noteReachOk() {}, message() {}, commandBusy: false,
        __WEBUI_TEXT__: key => key, setTimeout: fn => { fn(); },
        refreshStatus: async () => {
          ++polls; c.configRevision = 11;
          c.lastCommandStatus = {requestId: 7, state: polls === 1 ? 'QUEUED' : 'APPLIED'};
        }});
      vm.runInContext(code, c);
      assert.equal(await c.command('/api/v1/config', {timezoneId: 'Asia/Tokyo'}, true, '', ''), true);
      assert.equal(polls, 2);
    }]
  ];
  (async () => {
    let failures = 0;
    for (const [name, run] of cases) {
      try { await run(); } catch (error) { ++failures; console.error(name, error); }
    }
    if (failures) throw new Error(`${failures} timezone regressions failed`);
  })().catch(error => { console.error(error); process.exitCode = 1; });
}
