{
  const assert = require('assert');
  const vm = require('vm');
  const elements = new Map();
  const element = (id, initial = []) => {
    const classes = new Set(initial);
    const node = {disabled: false, dataset: {}, classList: {
      add: name => classes.add(name),
      contains: name => classes.has(name),
      toggle: (name, on) => on ? classes.add(name) : classes.delete(name)}};
    elements.set(id, node);
    return node;
  };
  const button = element('save'), hint = element('hint');
  button.dataset.dirty = '0';
  const revert = element('revertConfigButton');
  revert.dataset.dirty = '0';
  const fwRevert = element('revertFirmwareModeButton');
  fwRevert.dataset.dirty = '0';
  const context = vm.createContext({$: id => elements.get(id), controlsMutable: true});
  const helper = [blockAt(runtimeJs, 'function setSaveDirty('),
    blockAt(runtimeJs, 'const REVERT_BUTTONS')];
  assert(helper.every(Boolean), 'Missing shared dirty-save helper and revert pairing map');
  vm.runInContext(helper.join('\n'), context);
  vm.runInContext("setSaveDirty('save','hint',true)", context);
  assert(!button.disabled && button.dataset.dirty == 1 &&
         !hint.classList.contains('hidden'));
  vm.runInContext("setSaveDirty('save','hint',false)", context);
  assert(button.disabled && hint.classList.contains('hidden'));
  vm.runInContext("setSaveDirty('saveConfigButton','hint',true)", context);
  assert(!revert.disabled && revert.dataset.dirty == 1,
      'The paired revert button must mirror its save button dirty state');
  vm.runInContext("setSaveDirty('saveConfigButton','hint',false)", context);
  assert(revert.disabled && revert.dataset.dirty == 0);
  vm.runInContext("setSaveDirty('saveFirmwareModeButton','hint',true)", context);
  assert(!fwRevert.disabled && fwRevert.dataset.dirty == 1,
      'Dirtying the firmware-mode toggle must enable its paired revert button');
  vm.runInContext("setSaveDirty('saveFirmwareModeButton','hint',false)", context);
  assert(fwRevert.disabled && fwRevert.dataset.dirty == 0);
  context.controlsMutable = false;
  vm.runInContext("setSaveDirty('save','hint',true)", context);
  assert(button.disabled, 'Locked configuration must keep dirty save buttons disabled');
  vm.runInContext("setSaveDirty('saveConfigButton','hint',true)", context);
  assert(revert.disabled, 'Locked configuration must keep revert buttons disabled');

  const adminUi = viewJs.admin, normalizedUi = ui.replace(/\\"/g, '"');
  for (const id of ['saveConfigButton', 'saveBrewPresetButton', 'saveWebhookButton',
    'saveNetworkButton', 'saveDateTimeButton', 'changeDevicePasswordButton',
    'saveFirmwareModeButton', 'saveBleButton', 'savePowerButton', 'saveFrontendButton'])
    assert(new RegExp(`id="${id}"[^>]*data-dirty="0"[^>]*disabled`).test(normalizedUi),
        `${id} must start visibly disabled`);
  for (const id of ['revertConfigButton', 'revertBrewPresetButton', 'revertLineaMicraButton',
    'revertWebhookButton', 'revertNetworkButton', 'revertDateTimeButton', 'revertDevicePasswordButton',
    'revertFirmwareModeButton', 'revertBleButton', 'revertPowerButton', 'revertFrontendButton'])
    assert(new RegExp(`id="${id}"[^>]*data-dirty="0"[^>]*disabled`).test(normalizedUi),
        `${id} must start visibly disabled next to its save button`);
  assert(codeIncludes(viewJs.settings, 
      "if(el.id==='presetRenameInput')return"),
      'The preset rename dialog input must not dirty the machine-config section');
  assert(codeIncludes(runtimeJs, 'e.dataset.dirty!=null') &&
         codeIncludes(runtimeJs, "setSaveDirty('saveConfigButton','configDirtyHint',false)") &&
         codeIncludes(runtimeJs, "setSaveDirty('saveDateTimeButton','dateTimeDirtyHint',false)") &&
         codeIncludes(runtimeJs, 'await refreshStatus();return false') &&
         codeIncludes(adminUi, "const networkChanged=()=>R.setSaveDirty('saveNetworkButton','',true)") &&
         codeIncludes(adminUi, "R.setSaveDirty('saveWebhookButton','webhookDirtyHint',true)") &&
         codeIncludes(adminUi, "R.setSaveDirty('changeDevicePasswordButton','',false)") &&
         codeIncludes(adminUi, "R.command('/api/v1/network',payload,undefined,undefined,undefined,'saveNetworkButton',).then((ok)=>{if(!ok)return") &&
         codeIncludes(adminUi, "R.command('/api/v1/device/password',{newPassword:") &&
         codeIncludes(adminUi, "sectionChanged('#blePanel',R.markBleDirty)") &&
         codeIncludes(adminUi, "sectionChanged('#powerPanel',R.markPowerDirty)") &&
         codeIncludes(adminUi, "sectionChanged('#frontendPanel',R.markFrontendDirty)") &&
         codeIncludes(adminUi, "const b=$('savePowerButton');b.classList.add('busy')") &&
         css.includes('.btnGlyph:disabled{opacity:.4;cursor:not-allowed}'),
  'Save actions must enable on edits, disable only after success, and look disabled');
  assert(codeIncludes(runtimeJs, 'function snapshotControls(') &&
         codeIncludes(runtimeJs, 'function restoreSnapshot(') &&
         codeIncludes(runtimeJs, "if(!configDirty)configBaseline=snapshotControls(settingsSectionEls('config'))") &&
         codeIncludes(runtimeJs, 'if(!dateTimeDirty)dateTimeBaseline=snapshotControls(') &&
         codeIncludes(runtimeJs, 'if(!bleDirty)bleBaseline=snapshotControls(') &&
         codeIncludes(runtimeJs, 'if(!powerDirty)powerBaseline=snapshotControls(') &&
         codeIncludes(runtimeJs, 'if(!frontendDirty)frontendBaseline=snapshotControls(') &&
         codeIncludes(runtimeJs, 'networkBaseline=snapshotControls(networkControls())') &&
         codeIncludes(adminUi, 'webhookBaseline=R.snapshotControls(WEBHOOK_IDS.map((id)=>$(id)))') &&
         codeIncludes(runtimeJs, 'confirm("Discard unsaved changes?")') &&
         codeIncludes(adminUi, 'confirm("Discard unsaved changes?")') &&
         codeIncludes(runtimeJs, 'revertLineaMicra') &&
         css.includes('.btnGlyph.btnRevert{'),
    'Revert buttons must confirm, then restore the snapshot taken when the section was last hydrated clean');

  assert(codeIncludes(runtimeJs, 'command(path,value={},soft,okMsg,failMsg,busyId)') &&
         codeIncludes(runtimeJs, 'busyId?$(busyId):null') &&
         !codeIncludes(runtimeJs, 'function busyBtn') &&
         codeIncludes(runtimeJs, "classList.remove('busy')") &&
         css.includes('.btnGlyph.busy{pointer-events:none}') &&
         css.includes('.btnGlyph.busy .g:after') &&
         css.includes('@keyframes busySpin') &&
         css.includes('.btnGlyph.busy .g:after{animation:none}'),
    'Busy save buttons must spin in the glyph slot, block repeat clicks, and respect reduced motion');
  const busyCalls = [
    [runtimeJs, '/api/v1/config', 'saveConfigButton'],
    [runtimeJs, '/api/v1/config', 'saveDateTimeButton'],
    [runtimeJs, '/api/v1/presets', 'saveBrewPresetButton'],
    [runtimeJs, '/api/v1/machine/linea-micra', 'lineaMicraSaveButton'],
    [adminUi, '/api/v1/network', 'saveNetworkButton'],
    [adminUi, '/api/v1/device/password', 'changeDevicePasswordButton']];
  for (const [source, endpoint, id] of busyCalls)
    assert(new RegExp(`command\\(\\s*['"]${endpoint}[\\s\\S]*?['"]${id}['"]`).test(source),
        `saving via ${endpoint} must mark ${id} busy`);
  assert(codeIncludes(adminUi, "$('saveWebhookButton').classList.add('busy')") &&
         codeIncludes(adminUi, "finally{$('saveWebhookButton').classList.remove('busy')}"),
    'Webhook saving must keep its dedicated busy feedback');
}

// Execute the preferred-scale draft, polling, Save, and Revert lifecycle.
{
  const assert = require('assert').strict, vm = require('vm');
  const run = async () => {
    const a = 'AA:BB:CC:DD:EE:01', b = 'AA:BB:CC:DD:EE:02';
    const sel = {id: 'preferredScaleSelect', dataset: {}, options: [], selected: '',
      appendChild(o) {this.options.push(o);},
      set innerHTML(_) {this.options = []; this.selected = '';},
      set value(v) {this.selected = this.options.some(o => o.value === v) ? v : '';},
      get value() {return this.selected;},
      get selectedOptions() {return this.options.filter(o => o.value === this.value);}};
    const errors = [], requests = [];
    let applied = a, fail = false, editDuringSave = false;
    const context = vm.createContext({$: id => id === sel.id ? sel : null,
      document: {activeElement: null, createElement: () => ({dataset: {}}),
        querySelectorAll: () => [sel]},
      preferredScaleSelectSyncing: false, controlsMutable: true,
      configDirty: false, configBaseline: null,
      settingsSectionOf: () => 'config', updateScalePreferenceOptions() {},
      updateScaleRenameUi: (_, mac) => {context.renameMac = mac;},
      setSaveDirty() {}, confirm: () => true, clearFieldErrors() {},
      refreshBrewWarnings() {},
      updateConfigGroups() {}, syncHomeGuardSwitchesFromSettings() {},
      ensureSettingsHydrated: async () => {}, validateMachineClient: () => null,
      validateBullseyeClient: () => null, machinePayload: () => ({scaleMacCacheMode: 'only'}),
      addBullseyePayload: p => p, withBaseRev: p => ({...p, baseRevision: 7}),
      formatCommandError: (_, e) => e.message, message: e => errors.push(e),
      refreshStatus: async () => context.updatePreferredScaleSelect(status()),
      command: async (path, payload) => {
        requests.push({path, payload});
        if (fail) throw new Error('rejected');
        if ('preferredScaleMac' in payload) applied = payload.preferredScaleMac;
        if (editDuringSave) {sel.value = a; context.selectPreferredScale();}
        await context.refreshStatus();
        return true;
      }});
    const status = (history = [a, b]) => ({config: {scaleMacCacheMode: 'only'},
      scale: {preferredMac: applied, history: history.map(mac => ({mac, name: mac}))}});
    const take = (start, end) => runtimeJs.slice(runtimeJs.indexOf(start), runtimeJs.indexOf(end));
    vm.runInContext([
      take('function scaleHistoryLabel(', 'function formatScaleWeight('),
      take('function settingsSectionEls(', 'function setSaveDirty('),
      take('function markConfigDirty(', 'function markDateTimeDirty('),
      blockAt(runtimeJs, 'function revertMachineConfig('),
      take('function selectPreferredScale(', 'async function command('),
      take('async function saveMachineConfig(', 'async function saveDateTimeConfig(')
    ].join('\n'), context);
    context.updatePreferredScaleSelect(status());
    context.configBaseline = context.snapshotControls(context.settingsSectionEls('config'));
    sel.value = b; context.selectPreferredScale();
    assert.equal(requests.length, 0, 'Editing sends no mutation request');
    assert(context.configDirty); assert.equal(context.renameMac, b);
    context.updatePreferredScaleSelect(status([a]));
    assert.equal(sel.value, b, 'Blur and disappearing history preserve the draft');
    context.revertMachineConfig();
    assert.equal(sel.value, a); assert(!context.configDirty);
    sel.value = b; context.selectPreferredScale();
    fail = true; await context.saveMachineConfig();
    assert.equal(sel.value, b); assert(context.configDirty);
    assert.equal(errors.at(-1), 'rejected');
    fail = false; await context.saveMachineConfig();
    assert.equal(requests.at(-1).path, '/api/v1/config');
    assert.equal(requests.at(-1).payload.preferredScaleMac, b);
    assert.equal(requests.at(-1).payload.scaleMacCacheMode, 'only');
    assert.equal(requests.at(-1).payload.baseRevision, 7);
    assert(!context.configDirty); assert.equal(sel.dataset.pending, '0');
    context.markConfigDirty(); await context.saveMachineConfig();
    assert(!('preferredScaleMac' in requests.at(-1).payload), 'Unedited preference is omitted');
    sel.value = a; context.selectPreferredScale();
    applied = b; context.updatePreferredScaleSelect(status());
    assert.equal(sel.value, a, 'Polling cannot replace a pending draft');
    await context.saveMachineConfig();
    sel.value = b; context.selectPreferredScale();
    editDuringSave = true; await context.saveMachineConfig();
    assert.equal(sel.value, a); assert(context.configDirty, 'New edits survive Save acknowledgement');
    editDuringSave = false;
    context.revertMachineConfig(); applied = ''; context.updatePreferredScaleSelect(status());
    context.configBaseline = context.snapshotControls(context.settingsSectionEls('config'));
    context.markConfigDirty(); applied = a; context.updatePreferredScaleSelect(status());
    await context.saveMachineConfig();
    assert(!('preferredScaleMac' in requests.at(-1).payload), 'Unrelated edits preserve bootstrap adoption');
    applied = ''; context.updatePreferredScaleSelect(status()); sel.value = '';
    context.selectPreferredScale(); applied = a; context.updatePreferredScaleSelect(status());
    sel.value = ''; context.selectPreferredScale(); await context.saveMachineConfig();
    assert.equal(requests.at(-1).payload.preferredScaleMac, '', 'Explicit empty selection clears preference');
  };
  run().catch(error => {console.error(error); process.exitCode = 1;});
}
