{
  // SoftAP onboarding: captive portal + admin bypass + /setup live apply.
  const unlockBlock = blockAt(network, 'bool ShotStopperNetwork::adminUnlockAllowed(');
  const socketBlock =
      blockAt(network, 'bool ShotStopperNetwork::socketServedBySoftAp(');
  const handshake =
      network.slice(network.indexOf('esp_err_t ShotStopperNetwork::uiStreamHandshake('),
                    network.indexOf('void ShotStopperNetwork::uiStreamFree'));
  const ensureApBlock =
      blockAt(network, 'bool ShotStopperNetwork::ensureAccessPoint(');
  const stopSoftApBlock =
      blockAt(network, 'void ShotStopperNetwork::stopSoftAp(bool stopHttp) {');
  const startStationBlock =
      blockAt(network, 'void ShotStopperNetwork::startStation(');
  const stopNetworkBlock =
      blockAt(network, 'void ShotStopperNetwork::stopNetwork() {');
  const notFoundBlock =
      blockAt(network, 'esp_err_t ShotStopperNetwork::notFoundHandler(');
  const serviceBlock = blockAt(network, 'void ShotStopperNetwork::service() {');
  const staStateBlock =
      blockAt(network, 'void ShotStopperNetwork::serviceStaState(');
  const confirmBlock =
      blockAt(network, 'bool ShotStopperNetwork::confirmPendingNetwork(');
  const requestConfirmBlock = blockAt(
      network, 'void ShotStopperNetwork::requestPendingNetworkConfirm() {');
  const revertBlock =
      blockAt(network, 'bool ShotStopperNetwork::revertPendingNetwork(');
  const snapshotBlock =
      blockAt(network, 'NetworkStatusSnapshot ShotStopperNetwork::snapshot() {');
  const networkHandlerBlock =
      blockAt(network, 'esp_err_t ShotStopperNetwork::networkHandler(');
  const saveCommandBlock = network.slice(
      network.indexOf('case WebCommandType::SAVE_NETWORK:'),
      network.indexOf('case WebCommandType::FORGET_NETWORK:'));

  // Admin unlock bypass: SoftAP peers are pre-authenticated by the WPA2
  // passphrase, per client, and only while the AP is up.
  if (!networkHeader.includes('static bool socketServedBySoftAp(int socketFd);') ||
      !socketBlock.includes('getsockname') || !socketBlock.includes('getpeername') ||
      !socketBlock.includes('0xC0A80401u') || !socketBlock.includes('0xFFFFFF00u') ||
      !codeIncludes(unlockBlock, 'apActive&&socketServedBySoftAp(httpd_req_to_sockfd(request))')) {
    throw new Error(
        'SoftAP admin bypass must match the AP interface address and peer subnet, gated on apActive');
  }
  if (!codeIncludes(handshake, 'session.fromSoftAp=socketServedBySoftAp(session.fd)') ||
      (network.match(/unlocked \|= session\.fromSoftAp/g) || []).length !== 2) {
    throw new Error(
        'Home and diagnostic streams must OR the latched SoftAP session flag into adminUnlocked');
  }

  // Captive portal: wildcard DNS owned by the AP lifecycle, DHCP offers the
  // device as DNS, probe hosts 302 to /setup only while the AP is up.
  if (!sdkconfigDefaults.split('\n').includes('CONFIG_ARDUINO_SELECTIVE_DNSServer=y')) {
    throw new Error('The Arduino DNSServer component must be enabled for the captive portal');
  }
  if (!codeIncludes(network, 'captiveDns_.start(53,"*",ip)') ||
      !networkHeader.includes('DNSServer captiveDns_;') ||
      !codeIncludes(ensureApBlock, 'stopCaptivePortalDns()') ||
      !codeIncludes(ensureApBlock, 'startCaptivePortalDns()') ||
      !stopSoftApBlock.includes('stopCaptivePortalDns()') ||
      !startStationBlock.includes('stopCaptivePortalDns()') ||
      !stopNetworkBlock.includes('stopCaptivePortalDns()') ||
      !revertBlock.includes('stopCaptivePortalDns()')) {
    throw new Error(
        'Captive DNS must be wildcard, network-task owned, and paired with every AP teardown/raise path');
  }
  if (!codeIncludes(ensureApBlock,
                    'WiFi.softAPConfig(ip,ip,IPAddress(255,255,255,0),(uint32_t)0,ip)')) {
    throw new Error('SoftAP DHCP must offer the device IP as the DNS server (option 6)');
  }
  const probeHosts = [
    '"captive.apple.com"', '"connectivitycheck.gstatic.com"',
    '"connectivitycheck.android.com"', '"clients3.google.com"',
    '"www.msftconnecttest.com"', '"www.msftncsi.com"'];
  if (probeHosts.some((host) => !network.includes(host)) ||
      !notFoundBlock.includes('CAPTIVE_PORTAL_URL') ||
      !codeIncludes(notFoundBlock, 'instance_->snapshot().apActive&&captiveProbeHost(request)') ||
      !notFoundBlock.includes('"/api/"') || !notFoundBlock.includes('STATUS_NOT_FOUND') ||
      !network.includes('constexpr const char CAPTIVE_PORTAL_URL[] = "http://192.168.4.1/setup";') ||
      network.indexOf('constexpr const char CAPTIVE_PORTAL_URL[]') <
          network.indexOf('constexpr const char *AP_IP')) {
    throw new Error(
        'Captive probe hosts must 302 to /setup only while the AP is up; API 404s stay JSON');
  }

  // Live apply: /setup saves connect without a restart on the network task.
  if (!domainCore.includes('bool applyLive = false;') ||
      !codeIncludes(networkHandlerBlock, 'jsonHasOnlyUniqueFields(root,saveFields,13)') ||
      !networkHandlerBlock.includes('"apply"') ||
      !codeIncludes(networkHandlerBlock, 'strcmp(applyMode,"live")!=0') ||
      !codeIncludes(networkHandlerBlock, '!self.snapshot().apActive') ||
      !codeIncludes(networkHandlerBlock, 'command.network.applyLive=true') ||
      !codeIncludes(saveCommandBlock, 'if(command.network.applyLive){') ||
      !codeIncludes(saveCommandBlock, 'liveApplyPending_=true') ||
      !codeIncludes(saveCommandBlock, '}else{restartPending_=true;}')) {
    throw new Error(
        'apply:"live" must be schema-validated, accepted only while the AP is up, and must not schedule a restart');
  }
  if (!codeIncludes(serviceBlock, 'beginStationConnect(networkSettingsSnapshot(),now)') ||
      !codeIncludes(serviceBlock, 'apKeepRequested_=true') ||
      !codeIncludes(serviceBlock, 'armPendingConfirmWindow(now)')) {
    throw new Error('The network task must consume the live-apply flag and arm the confirm window');
  }
  // Confirmation and revert keep the onboarding AP contract: confirm is
  // allowed on the kept AP, drops the keep after persisting CONFIRMED, and
  // the unconfirmed window reverts while the SoftAP stays the fallback.
  if (!codeIncludes(requestConfirmBlock, '(!status_.apActive||status_.apKeepActive)') ||
      !codeIncludes(snapshotBlock, '(!status_.apActive||status_.apKeepActive)') ||
      !codeIncludes(confirmBlock, '(status.apActive&&!apKeepRequested_)') ||
      !codeIncludes(networkHandlerBlock, '(network.apActive&&!network.apKeepActive)') ||
      !codeIncludes(confirmBlock, 'apKeepRequested_=false') ||
      !codeIncludes(revertBlock, 'apKeepRequested_=false') ||
      !codeIncludes(revertBlock, 'status_.staState=StaState::NOT_CONFIGURED') ||
      !codeIncludes(staStateBlock, 'status.apActive&&apKeepRequested_&&staConfirmArmed_') ||
      !codeIncludes(staStateBlock, 'revertPendingNetwork(now,"confirm timeout")')) {
    throw new Error(
        'Live-apply confirm/revert must relax only on apKeepActive and revert to the SoftAP fallback');
  }

  // Scan pipeline: the HTTP handler's optimistic QUEUED must not be canceled
  // by serviceWifiScan while its own maintenance lease is still landing
  // (every /setup or Admin reload logged a spurious cancel before the scan
  // started); only task-owned (requested) or on-radio (RUNNING) scans cancel,
  // and a dropped scan command retires the optimistic state itself.
  const serviceWifiScanBlock =
      blockAt(network, 'void ShotStopperNetwork::serviceWifiScan(');
  if ((serviceWifiScanBlock.match(
          /canceled = requested \|\| state == WifiScanState::RUNNING \|\|/g) || [])
          .length !== 0 ||
      (serviceWifiScanBlock.match(
          /canceled = requested \|\| state == WifiScanState::RUNNING;/g) || [])
          .length !== 2 ||
      !network.includes('abortWifiScan(now, false);') ||
      !codeIncludes(network,
          'if(acceptedCommand_.type==WebCommandType::START_WIFI_SCAN){abortWifiScan(now,false);}')) {
    throw new Error(
        'Unsafe-control scan cancels must skip the optimistic handler QUEUED; dropped scan commands retire it');
  }

  // Onboarding attempts must fail fast and stop the retry loop: a wrong
  // password can leave the driver mid-status (AUTH_EXPIRE keeps the last
  // status), so the attempt ages at 20 s instead of 60 s, and the failed
  // attempt holds reconnects (each retry aborted every UI scan, leaving an
  // empty picker after Try again) until a new save, confirm, or revert.
  if (!networkHeader.includes('LIVE_APPLY_ATTEMPT_MS = 20000') ||
      !codeIncludes(staStateBlock,
          '(liveOnboarding?LIVE_APPLY_ATTEMPT_MS:STA_RECOVERY_ATTEMPT_MS)') ||
      !codeIncludes(staStateBlock,
          'staReconnectHeld_=true') ||
      !codeIncludes(serviceBlock, 'staReconnectHeld_=false') ||
      !codeIncludes(confirmBlock, 'staReconnectHeld_=false') ||
      !codeIncludes(revertBlock, 'staReconnectHeld_=false')) {
    throw new Error(
        'Live-apply attempts must age at 20 s and hold reconnects on failure until the next save/confirm/revert');
  }

  // /setup web view: route, menu-less shell, live-apply payload, bounded
  // connect wait, confirm on success, and the LAN ADMIN_LOCKED escape hatch.
  const setupJs = viewJs.setup || '';
  if (!codeIncludes(appJsSource, '"/setup":"setup"') ||
      !codeIncludes(appJsSource, 'setupView') ||
      !codeIncludes(appJsSource, 'jsMods.get("setup")?.stop?.()') ||
      !partialHtml.setup ||
      !codeIncludes(setupJs, 'apply:"live"') ||
      !codeIncludes(setupJs, '"/api/v1/status/admin"') ||
      !codeIncludes(setupJs, '{action:"confirm"}') ||
      !codeIncludes(setupJs, 'e.code==="ADMIN_LOCKED"') ||
      !setupJs.includes('CONNECT_WAIT_MS') ||
      // The visible 20 s attempt countdown must mirror the firmware bound.
      !codeIncludes(setupJs, 'ATTEMPT_WINDOW_MS=20000') ||
      !codeIncludes(setupJs, 'This usually takes less than') ||
      !partialHtml.setup.includes('id="setupConnectingCountdown"') ||
      !partialHtml.setup.includes('id="setupHiddenToggle"') ||
      !partialHtml.setup.includes('2.4 GHz') ||
      !css.includes('body.setupView .topBar,body.setupView .pageNav{display:none}') ||
      !css.includes('.setupCard{')) {
    throw new Error(
        '/setup must be a routed menu-less view with the live-apply save pipeline and captive-portal fallbacks');
  }
}
