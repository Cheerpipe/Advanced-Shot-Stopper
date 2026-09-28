// Exercise activity timing independently of automatic polls and session expiry.
{
  const assert = require('assert').strict;
  const source = runtimeJs.slice(runtimeJs.indexOf('let webUiPowerUntil='),
      runtimeJs.indexOf('const WEB_UI_INACTIVITY_MS='));
  let now = 100, owner = true;
  const doc = {hidden: false};
  const make = new Function('performance', 'document', 'getOwner',
      source.replace(/export /g, '').replace(/webUiOwner/g, 'getOwner()') +
      ';return {activity:noteWebUiPowerActivity,seconds:webUiPowerSeconds}');
  const lease = make({now: () => now}, doc, () => owner);
  lease.activity();
  assert.equal(lease.seconds(), 30);
  now += 160000;
  assert.equal(lease.seconds(), 20);
  // Polls obtain a header but never advance the human deadline.
  for (let i = 0; i < 100; ++i) assert.equal(lease.seconds(), 20);
  doc.hidden = true;
  assert.equal(lease.seconds(), 0);
  lease.activity();
  doc.hidden = false;
  now += 20000;
  assert.equal(lease.seconds(), 0);
  lease.activity();
  assert.equal(lease.seconds(), 30);
  owner = false;
  now += 180000;
  lease.activity();
  assert.equal(lease.seconds(), 0);
  assert(network.includes('"powerManagementEnabled"'));
  const handler = network.slice(network.indexOf('const bool diagnosticPagePatch'),
      network.indexOf('const bool diagnosticPagePatch') + 200);
  assert(handler.includes('powerManagementEnabled'));
  assert(network.includes('diagnosticPagePatch && !self.requireAdminUnlock(request)'));
  assert(network.includes('notePowerWebActivity(millis()'));
  assert(runtimeJs.includes("options.headers['X-WebUI-Activity']"));
}

// Real config waiter: APPLY_CONFIG remains APPLIED after asynchronous saving.
{
  const assert = require('assert').strict;
  const admin = viewJs.admin;
  const source = admin.slice(admin.indexOf('async function waitSaved('),
      admin.indexOf('function controlChanged('));
  const run = async () => {
    let now = 0, snapshots = [];
    const waiter = new Function('R', 'wait', 'Date', source + ';return waitSaved')({
      api: async () => {
        assert(snapshots.length, 'unexpected extra poll after durable config');
        return snapshots.shift();
      }
    }, async () => { now += 150; }, {now: () => now});
    for (const key of ['powerManagementEnabled', 'showDiagnosticPage']) {
      for (const value of [true, false]) {
        const snapshot = (state, revision, pending, failed = false, actual = value) => ({
          lastCommand: {requestId: 7, state},
          config: {revision, persistPending: pending, persistFailed: failed, [key]: actual}
        });
        snapshots = [snapshot('QUEUED', 12, false, true), snapshot('APPLIED', 12, false, true),
          snapshot('APPLIED', 13, true), snapshot('APPLIED', 13, false, false, !value),
          snapshot('APPLIED', 14, false)]; // Coalesced later revision is durable too.
        await waiter(7, key, {baseRevision: 12, [key]: value});
        assert.equal(snapshots.length, 0);
        snapshots = [snapshot('APPLIED', 1, false)];
        await waiter(7, key, {baseRevision: 0xffffffff, [key]: value});
        snapshots = [snapshot('APPLIED', 13, true, true)];
        await assert.rejects(waiter(7, key, {baseRevision: 12, [key]: value}), /Could not save/);
        for (const state of ['FAILED', 'CANCELED']) {
          snapshots = [snapshot(state, 12, false)];
          await assert.rejects(waiter(7, key, {baseRevision: 12, [key]: value}), /Could not save/);
        }
        snapshots = Array.from({length: 54}, () => snapshot('APPLIED', 13, true));
        await assert.rejects(waiter(7, key, {baseRevision: 12, [key]: value}), /Timeout saving/);
      }
    }
  };
  run().catch(error => { console.error(error); process.exitCode = 1; });
}

// Shared durable config save: revisioned request, durable acknowledgement;
// failures reject so section Save keeps its dirty draft for retry or revert.
{
  const assert = require('assert').strict;
  const admin = viewJs.admin;
  const source = admin.slice(admin.indexOf('async function saveDurableConfigKey('),
      admin.indexOf('async function saveBleSettings('));
  const run = async () => {
    const el = {checked: true};
    let reject = false, posted, waited = 0;
    const handler = new Function('R', 'waitSaved', '$', source + ';return saveDurableConfigKey')(
        {
          withCommandGate: fn => fn(),
          withBaseRev: fields => ({...fields, baseRevision: 12}),
          body: JSON.stringify,
          api: async (_, options) => {
            posted = JSON.parse(options.body);
            if (reject) throw new Error('Revision conflict');
            return {requestId: 5};
          }, message: () => {}
        },
        async (id, key, config) => {
          assert.equal(id, 5);
          assert.equal(key, 'powerManagementEnabled');
          assert.deepEqual(config, posted);
          ++waited;
        }, () => el);
    await handler('powerManagementEnabled');
    assert.deepEqual(posted, {powerManagementEnabled: true, baseRevision: 12});
    assert.equal(waited, 1);
    assert.equal(el.checked, true);
    reject = true;
    await assert.rejects(handler('powerManagementEnabled'), /Revision conflict/);
  };
  run().catch(error => { console.error(error); process.exitCode = 1; });
}
