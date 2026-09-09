// Exercise the real UI handler and persistence waiter with transport/DOM doubles.
{
  const assert = require('assert').strict;
  const vm = require('vm');
  const admin = fs.readFileSync(path.join(sketchDir, 'web/js/admin.js'), 'utf8');
  const handler = admin.slice(admin.indexOf("$('saveDeviceNameButton').onclick="),
      admin.indexOf(";$('saveNetworkButton').onclick="));
  const waiter = admin.slice(admin.indexOf('async function waitSaved('),
      admin.indexOf('function saveToggle('));
  assert(handler && waiter);
  assert(html.includes('id="deviceName"') && html.includes('maxlength="63"'));
  const route = network.slice(network.indexOf('strcmp(action, "name")'),
      network.indexOf('strcmp(action, "forget")'));
  assert(route.includes('webUiConfigurationAllowed(request, status)'));
  assert(route.includes('jsonHasOnlyUniqueFields(root, nameFields, 2)'));
  assert(route.includes('validDeviceName(command.network.deviceName)'));
  const run = async () => {
    const elements = {deviceName: {value: ''}, saveDeviceNameButton: {}};
    let allowed = true, posts = [], states = [], messages = [], errors = 0;
    const context = {
      $: id => elements[id], wait: async () => {},
      R: {
        body: JSON.stringify,
        showFieldError: () => ++errors, clearFieldErrors: () => {},
        withCommandGate: fn => allowed ? fn() : undefined,
        refreshStatus: async () => {}, formatCommandError: text => text,
        message: (text, kind) => messages.push({text, kind}),
        api: async (url, options) => {
          if (options) {
            assert.equal(url, '/api/v1/network');
            assert.equal(options.method, 'POST');
            posts.push(JSON.parse(options.body));
            return {requestId: 7};
          }
          assert.equal(url, '/api/v1/status/admin');
          assert(states.length, 'waiter must stop at a terminal result');
          return {lastCommand: {requestId: 7, state: states.shift()}};
        },
      },
    };
    vm.runInNewContext(waiter + '\n' + handler, context);
    const save = () => elements.saveDeviceNameButton.onclick();
    for (const name of ['', '-bad', 'bad-', 'a.local', 'a b', 'é', 'a'.repeat(64)]) {
      elements.deviceName.value = name;
      await save();
    }
    assert.equal(errors, 7);
    assert.equal(posts.length, 0);
    elements.deviceName.value = ' Coffee-Bar ';
    allowed = false;
    await save();
    assert.equal(posts.length, 0);
    allowed = true;
    states = ['QUEUED', 'APPLIED', 'PERSISTED'];
    await save();
    assert.deepEqual(posts[0], {action: 'name', deviceName: 'coffee-bar'});
    assert.equal(elements.deviceName.value, 'coffee-bar');
    assert.equal(messages.at(-1).kind, 'ok');
    for (const state of ['FAILED', 'CANCELED']) {
      states = [state];
      messages = [];
      await save();
      assert.equal(messages.length, 1);
      assert.equal(messages[0].kind, 'error');
    }
  };
  run().catch(error => { console.error(error); process.exitCode = 1; });
}
