'use strict';
// Synthetic states; production rendering above never contacts a device.
const $ = id => document.getElementById(id);
const R = {formatWallTime: sec => new Date(sec * 1000).toISOString().slice(0, 19)};
let controlsMutable = true, micraDirty = false;
document.documentElement.classList.add('lineaMicraIntegration');
$('homeBoot').remove();
document.querySelectorAll('.view').forEach(view => { view.hidden = !['settings', 'diagnostic'].includes(view.dataset.view); });
const settings = $('lineaMicraSettings');
$('view-settings').replaceChildren(settings);
settings.open = true;
const states = $('dMicraMode').closest('fieldset'), cloud = $('micraCloudDiagnostics'), heap = $('hHeapLargest').closest('fieldset');
states.querySelectorAll('.metric:not(.micraOnly)').forEach(row => row.remove());
heap.querySelectorAll('.metric').forEach(row => { if (!row.contains($('hHeapLargest'))) row.remove(); });
$('view-diagnostic').replaceChildren(states, cloud, heap);
const controls = document.createElement('p');
controls.textContent = 'Micra preview · synthetic data · no device connection ';
const sample = document.createElement('select');
['streaming', 'paused', 'unknown', 'api', 'disabled', 'disconnected'].forEach(value => sample.add(new Option(value, value)));
controls.append(sample);
$('app').prepend(controls);
function renderSample() {
  const choice = sample.value, api = choice === 'api', disconnected = choice === 'disconnected';
  const lm = {accountConfigured: !disconnected, email: 'barista@example.test', selectedName: 'Kitchen Micra', selectedSerial: 'SYNTHETIC',
    connectionType: api ? 'api' : 'websocket', observeState: choice !== 'disabled', applyTemperature: true,
    phase: 'idle', error: 'none', staConnected: true, targetValid: true, targetDeciC: 935,
    websocket: {state: choice === 'paused' ? 'paused' : 'streaming', reason: choice === 'paused' ? 'scale_acquisition_and_ble_communication_quiet' : 'none',
      nowMs: 50000, messageAtMs: 48000, powerAtMs: 45000, pongAtMs: 49000,
      rxBytes: 123456, txBytes: 6789, rxBytesPerSecond: 240, rxBytesPerMinute: 12000,
      txBytesPerSecond: 0, txBytesPerMinute: 650, messages: 42, reconnects: 2, errors: 1,
      cleaning: choice === 'unknown' ? 'unknown' : 'waiting_for_paddle', cleaningLabel: '<b>Future cleaning status</b>', cleaningAvailable: !disconnected,
      cleaningAtMs: 47000, retainedBytes: 23900, connectFreeDelta: -15000, connectLargestDelta: -14000, stopFreeDelta: 15000, stopLargestDelta: 14000,
      stoppedLatencyMs: 25, maxStoppedLatencyMs: 80}};
  micraDirty = false;
  applyLineaMicraStatus({lineaMicra: lm});
  renderMicraCloudDiagnostic(lm);
  $('dMicraPowerValue').textContent = 'on'; $('dMicraMode').textContent = 'brewing_mode';
  $('dMicraQuality').textContent = choice === 'paused' ? 'stale · retained' : 'current';
  $('hHeapLargest').textContent = 'Internal 150000 B · PSRAM 6000000 B';
}
sample.onchange = renderSample;
$('lineaMicraConnectionType').onchange = markLineaMicraDirty;
for (const button of settings.querySelectorAll('button')) button.onclick = () => false;
renderSample();
