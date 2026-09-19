const micraWeb = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperLineaMicraWeb.inc'), 'utf8');
const micraStatus = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperStatus.inc'), 'utf8');
const micraSettingsHtml = rawPartialHtml.settings;
const micraDiagnosticHtml = rawPartialHtml.diagnostic;

if (!network.includes('/api/v1/machine/linea-micra') ||
    !micraWeb.includes('UNSUPPORTED_MACHINE') ||
    !micraWeb.includes('jsonHasOnlyUniqueFields') ||
    !micraWeb.includes('token must contain exactly 64 printable characters') ||
    !micraWeb.includes('address must be empty or a BLE address') ||
    !micraWeb.includes('stagedLineaMicra_') ||
    !micraWeb.includes('queueMachineIntegrationRequest')) {
  throw new Error('Linea Micra API must be profile-gated, strict, staged, and asynchronous');
}
if (micraStatus.includes('\\"token\\":\\"') ||
    !micraStatus.includes('\\"tokenConfigured\\"') ||
    !micraStatus.includes('SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_BLE') ||
    !micraStatus.includes('\\"machineIntegration\\"')) {
  throw new Error('Linea Micra status must be compile-gated and redact the BLE token');
}
for (const id of ['lineaMicraToken', 'lineaMicraAddress',
  'lineaMicraApplyTemperature',
  'lineaMicraObserveState', 'lineaMicraSaveButton', 'lineaMicraTestButton',
  'lineaMicraForgetButton', 'lineaMicraRemoveTokenButton',
  'lineaMicraBrewTargetC']) {
  if (!micraSettingsHtml.includes(`id="${id}"`)) {
    throw new Error(`Linea Micra settings control is missing: ${id}`);
  }
}
for (const id of ['dMicraPower', 'dMicraMode', 'dMicraQuality', 'dMicraAge',
  'lineaMicraRefreshButton']) {
  if (!micraDiagnosticHtml.includes(`id="${id}"`)) {
    throw new Error(`Linea Micra diagnostic control is missing: ${id}`);
  }
}
if (!rawCss.includes('html:not(.lineaMicraIntegration) .micraOnly') ||
    !rawRuntimeJs.includes("s.machineIntegration==='linea_micra_ble'") ||
    !rawRuntimeJs.includes("payload={action:'save',address") ||
    !rawRuntimeJs.includes("['queued','running','backoff'].includes(m.phase)") ||
    !rawRuntimeJs.includes("['queued','running','backoff'].includes(lm.phase)") ||
    !rawRuntimeJs.includes("expired?'UNKNOWN':lm.powerState") ||
    !rawRuntimeJs.includes('age>=lm.freshnessMs')) {
  throw new Error('Linea Micra UI must use compiled capability gating and local freshness expiry');
}
for (const file of ['ShotStopperMachinePaddleControl.h',
  'ShotStopperMachinePaddleInput.h', 'ShotStopperMachinePaddlePolicy.h',
  'ShotStopperMachinePaddleState.h']) {
  if (fs.readFileSync(path.join(sketchDir, file), 'utf8').includes('LineaMicra')) {
    throw new Error(`Paddle behavior must remain independent of Linea Micra: ${file}`);
  }
}
