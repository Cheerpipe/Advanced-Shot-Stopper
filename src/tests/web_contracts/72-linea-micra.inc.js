const micraWeb = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperLineaMicraWeb.inc'), 'utf8');
const micraStatus = fs.readFileSync(
    path.join(sketchDir, 'network/ShotStopperStatus.inc'), 'utf8');
const micraSettingsHtml = rawPartialHtml.settings;
const micraDiagnosticHtml = rawPartialHtml.diagnostic;

if (!network.includes('/api/v1/machine/linea-micra') ||
    !micraWeb.includes('UNSUPPORTED_MACHINE') ||
    !micraWeb.includes('jsonHasOnlyUniqueFields') ||
    !micraWeb.includes('MICRA_STA_REQUIRED') ||
    !micraWeb.includes('queueMachineIntegrationConnect') ||
    !micraWeb.includes('selectMachineIntegrationDevice') ||
    !micraWeb.includes('disconnectLineaMicra(candidate)') ||
    !micraWeb.includes('stagedLineaMicra_')) {
  throw new Error('Linea Micra API must be profile-gated, strict, STA-only, staged, and asynchronous');
}
const micraStatusFailures = [
  micraStatus.includes('\"password\"') && 'password exposed',
  micraStatus.includes('\"username\"') && 'username exposed',
  !micraStatus.includes('\\\"accountConfigured\\\"') && 'account status missing',
  !micraStatus.includes('\\\"machines\\\"') && 'machine list missing',
  !micraStatus.includes('SHOT_STOPPER_MACHINE_INTEGRATION_LINEA_MICRA_CLOUD') && 'compile gate missing',
  !micraStatus.includes('\\\"machineIntegration\\\"') && 'integration capability missing',
].filter(Boolean);
if (micraStatusFailures.length) {
  throw new Error('Linea Micra status contract: ' + micraStatusFailures.join(', '));
}
for (const id of ['lineaMicraUsername', 'lineaMicraPassword',
  'lineaMicraConnectButton', 'lineaMicraMachine', 'lineaMicraSelectButton',
  'lineaMicraApplyTemperature', 'lineaMicraObserveState',
  'lineaMicraSaveButton', 'lineaMicraDisconnectButton',
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
    !rawRuntimeJs.includes("s.machineIntegration==='linea_micra_cloud'") ||
    !rawRuntimeJs.includes("{action:'connect',username,password}") ||
    !rawRuntimeJs.includes("{action:'select',serial") ||
    !rawRuntimeJs.includes("lineaMicraAction('disconnect')") ||
    !rawRuntimeJs.includes("['queued','authenticating','listing','running','backoff'].includes(m.phase)") ||
    !rawRuntimeJs.includes("expired?'UNKNOWN':lm.powerState") ||
    !rawRuntimeJs.includes('age>=lm.freshnessMs')) {
  throw new Error('Linea Micra UI must implement account connection, selection, and freshness expiry');
}
for (const file of ['ShotStopperMachinePaddleControl.h',
  'ShotStopperMachinePaddleInput.h', 'ShotStopperMachinePaddlePolicy.h',
  'ShotStopperMachinePaddleState.h']) {
  if (fs.readFileSync(path.join(sketchDir, file), 'utf8').includes('LineaMicra')) {
    throw new Error(`Paddle behavior must remain independent of Linea Micra: ${file}`);
  }
}
