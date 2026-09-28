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
         adminUi.includes("R.command('/api/v1/network',payload,undefined,undefined,undefined,'saveNetworkButton').then(ok=>{if(!ok)return") &&
         adminUi.includes("R.command('/api/v1/device/password',{newPassword:") &&
         css.includes('.btnGlyph:disabled{opacity:.4;cursor:not-allowed}'),
  'Save actions must enable on edits, disable only after success, and look disabled');

  assert(runtimeJs.includes('command(path,value={},soft,okMsg,failMsg,busyId)') &&
         runtimeJs.includes('busyId?$(busyId):null') &&
         !runtimeJs.includes('function busyBtn') &&
         runtimeJs.includes("classList.remove('busy')") &&
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
    assert(new RegExp(`command\\('${endpoint}[^;]*'${id}'`).test(source),
        `saving via ${endpoint} must mark ${id} busy`);
  assert(adminUi.includes("$('saveWebhookButton').classList.add('busy')") &&
         adminUi.includes("finally{$('saveWebhookButton').classList.remove('busy')}"),
    'Webhook saving must keep its dedicated busy feedback');
}
