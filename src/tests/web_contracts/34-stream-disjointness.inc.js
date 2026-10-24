// D18 (2026-10-24): layered disjoint stream projections. Each field path is
// extracted from its sender body (the `delta.field`/`delta.hashedField`
// literals; the Diagnostic sender also uses a `telemetry` alias) and no path
// may appear in two projections. The pre-existing Diagnostic overlap is a
// dated allowance list: this slice's owner decision covers the Settings
// projection only, and Diagnostic's own cleanup is a separate, explicit
// owner decision (do not fix Diagnostic here).
{
  const assert = require('assert').strict;
  const homeStreamSource = fs.readFileSync(
      path.join(sketchDir, 'network/OpenBrewByWeightHomeStream.inc'), 'utf8');
  const settingsSender = fs.readFileSync(
      path.join(sketchDir, 'network/OpenBrewByWeightSettingsStream.inc'), 'utf8');
  // sendHomeStream and sendDiagnosticStream share one include; slice each
  // sender body at its function boundaries so the extractions never mix.
  const between = (source, from, to) => {
    const start = source.indexOf(from);
    assert(start >= 0, 'missing sender boundary ' + from);
    const end = source.indexOf(to, start);
    assert(end > start, 'missing sender boundary ' + to);
    return source.slice(start, end);
  };
  const pathsOf = body => {
    const paths = [...body.matchAll(
        /\b(?:delta|telemetry)\.(?:hashedField|field)\(\s*"([^"]+)"/g)].map(m => m[1]);
    assert(paths.length > 0, 'sender extraction found no field paths');
    return new Set(paths);
  };
  const home = pathsOf(between(homeStreamSource,
      'bool OpenBrewByWeightNetwork::sendHomeStream(',
      'bool OpenBrewByWeightNetwork::sendUiStreamFrame('));
  const diagnostic = pathsOf(between(homeStreamSource,
      'bool OpenBrewByWeightNetwork::sendDiagnosticStream(',
      'bool OpenBrewByWeightNetwork::sendLogStream('));
  const settings = pathsOf(settingsSender);
  // The Settings overlay is Home-disjoint outright (D17's contract): the
  // painters compose {...home, ...settings}, so any shared path would be a
  // REST-era duplication defect, never an allowance.
  const settingsHomeOverlap = [...settings].filter(path => home.has(path));
  assert.deepEqual(settingsHomeOverlap, [],
      'Settings projection must not duplicate Home stream fields');
  // Pre-existing Diagnostic overlap, verified and enumerated 2026-10-24
  // (allowance dated the same day; Diagnostic is untouched by D17 and its
  // cleanup is the owner's separate decision).
  const diagnosticHomeAllowance = [
    'adminUnlocked', 'backflush.ready', 'backflush.stopReason',
    'backflush.unresolved', 'backflush.waiting', 'bootId',
    'config.appliedTimezoneOffsetMinutes', 'config.timezoneId',
    'cupPresence.present', 'cupPresence.state', 'cupPresence.weightG',
    'cupPresence.weightValid', 'development', 'firmwareVersion',
    'lastCommand.requestId', 'lastCommand.state',
    'lineaMicra.accountConfigured', 'lineaMicra.boiler.coffee.readyAtUtcSec',
    'lineaMicra.boiler.coffee.state', 'lineaMicra.boiler.coffee.targetDeciC',
    'lineaMicra.boiler.steam.readyAtUtcSec', 'lineaMicra.boiler.steam.state',
    'lineaMicra.connectionType', 'lineaMicra.observeState',
    'lineaMicra.powerState', 'lineaMicra.quality', 'lineaMicra.readiness',
    'lineaMicra.websocket.cleaning', 'lineaMicra.websocket.cleaningAvailable',
    'lineaMicra.websocket.cleaningLabel', 'machineRunning', 'machineState',
    'physicalActivatorOn', 'safety.state', 'scale.available',
    'scale.observedWeightG', 'scale.streamState', 'scale.timerMs', 'state',
  ];
  // The Settings-side residue of that overlap: the Settings page's Micra
  // sign-in flow and scale-loss projection have no other source (Home
  // carries none of these paths), so they stay until the owner's separate
  // Diagnostic decision resolves them.
  const diagnosticSettingsAllowance = [
    'lineaMicra.email', 'lineaMicra.phase', 'lineaMicra.selectedName',
    'lineaMicra.selectedSerial', 'scaleLoss.projectedS',
  ];
  const overlapOf = (a, b) => [...a].filter(path => b.has(path)).sort();
  const diagHome = overlapOf(diagnostic, home);
  const diagSettings = overlapOf(diagnostic, settings);
  assert.deepEqual(diagHome, diagnosticHomeAllowance.slice().sort(),
      'the Diagnostic/Home allowance list must match the real overlap exactly');
  assert.deepEqual(diagSettings, diagnosticSettingsAllowance.slice().sort(),
      'the Diagnostic/Settings allowance list must match the real overlap exactly');
  // The scale-loss projection keeps its live inputs on the overlay (owner
  // decision recorded 2026-10-24): Home carries no scale-loss path, so
  // removing it here would break the painted projection.
  assert(!home.has('scaleLoss.projectedS') && settings.has('scaleLoss.projectedS'),
      'scaleLoss.projectedS stays on the Settings overlay until the owner moves it');
}
