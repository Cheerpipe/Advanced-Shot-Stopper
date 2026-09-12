{
  const integrationApi = fs.readFileSync(
    path.join(sketchDir, 'network', 'ShotStopperIntegrationApi.inc'), 'utf8');
  for (const route of [
    '/api/v1/integration', '/api/v1/integration/request',
    '/api/v1/integration/webhook', '/api/v1/integration/webhook/test',
    '/api/v1/integration/presets', '/api/v1/integration/presets/active',
    '/api/v1/integration/quick-settings', '/api/v1/integration/restart',
  ]) {
    if (!integrationApi.includes(route))
      throw new Error(`Missing Home Assistant integration route: ${route}`);
  }
  if (/Authorization|Bearer|pairing|management.token|requireIntegrationAuth/i.test(integrationApi) ||
      viewJs.admin.includes('/api/v1/integration/pairing/open')) {
    throw new Error('The integration API must share the open local-LAN posture of the Web UI');
  }
  if (!webhookHeader.includes('bool presetChanges = false') ||
      !webhookHeader.includes('PRESETS_CHANGED') ||
      !webhookHeader.includes('QUICK_SETTINGS_CHANGED') ||
      !webhookHeader.includes('CONTROLLER_STARTED') ||
      !webhookSource.includes('"presets_changed"') ||
      !webhookSource.includes('escapeJsonString') ||
      !firmware.includes('activePresetName') ||
      !firmware.includes('pendingPresetPersistence')) {
    throw new Error('Webhook v1 must preserve preset provenance and emit persisted preset snapshots');
  }
  if (integrationApi.includes('/start') || integrationApi.includes('/stop') ||
      integrationApi.includes('relayClose') || integrationApi.includes('REMOTE_START')) {
    throw new Error('The Home Assistant API must not expose machine actuation');
  }
  if (!integrationApi.includes('parseIntegrationQuickSettingRequest(root, parsedRequest)') ||
      !integrationApi.includes('parseIntegrationRestartRequest(root)') ||
      !integrationApi.includes('rollbackIntegrationStagedRequest(self.stagedPresetRequestId_') ||
      !integrationApi.includes('STATUS_BAD_REQUEST, "INVALID_REQUEST"')) {
    throw new Error('Integration mutations must use the host-tested bounded parsers and HTTP 400 contract');
  }
  for (const fixture of [
    'integration_snapshot.json', 'integration_presets.json',
    'webhook_end_v1.json', 'webhook_presets_changed_v1.json',
    'webhook_quick_settings_changed_v1.json',
    'webhook_controller_started_v1.json',
  ]) {
    JSON.parse(fs.readFileSync(path.join(sketchDir, 'tests', 'fixtures', fixture), 'utf8'));
  }
}
