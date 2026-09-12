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
  const context = vm.createContext({$: id => elements.get(id), controlsMutable: true});
  const helper = runtimeJs.split('\n').find(line => line.startsWith('function setSaveDirty('));
  assert(helper, 'Missing shared dirty-save helper');
  vm.runInContext(helper, context);
  vm.runInContext("setSaveDirty('save','hint',true)", context);
  assert(!button.disabled && button.dataset.dirty == 1 &&
         !hint.classList.contains('hidden'));
  vm.runInContext("setSaveDirty('save','hint',false)", context);
  assert(button.disabled && hint.classList.contains('hidden'));
  context.controlsMutable = false;
  vm.runInContext("setSaveDirty('save','hint',true)", context);
  assert(button.disabled, 'Locked configuration must keep dirty save buttons disabled');

  const adminUi = viewJs.admin, normalizedUi = ui.replace(/\\"/g, '"');
  for (const id of ['saveConfigButton', 'saveBrewPresetButton', 'saveWebhookButton',
    'saveNetworkButton', 'saveDateTimeButton', 'changeDevicePasswordButton'])
    assert(new RegExp(`id="${id}"[^>]*data-dirty="0"[^>]*disabled`).test(normalizedUi),
        `${id} must start visibly disabled`);
  assert(runtimeJs.includes('e.dataset.dirty!=null') &&
         runtimeJs.includes("setSaveDirty('saveConfigButton','configDirtyHint',false)") &&
         runtimeJs.includes("setSaveDirty('saveDateTimeButton','dateTimeDirtyHint',false)") &&
         runtimeJs.includes('await refreshStatus();return false') &&
         adminUi.includes("const networkChanged=()=>R.setSaveDirty('saveNetworkButton','',true)") &&
         adminUi.includes("R.setSaveDirty('saveWebhookButton','webhookDirtyHint',true)") &&
         adminUi.includes("R.setSaveDirty('changeDevicePasswordButton','',false)") &&
         adminUi.includes("R.command('/api/v1/network',payload).then(ok=>{if(!ok)return") &&
         adminUi.includes("R.command('/api/v1/device/password',{newPassword:") &&
         css.includes('.btnGlyph:disabled{opacity:.4;cursor:not-allowed}'),
  'Save actions must enable on edits, disable only after success, and look disabled');
}
