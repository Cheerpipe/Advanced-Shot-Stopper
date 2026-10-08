"use strict";
// First-use Wi-Fi onboarding (/setup): menu-less card that scans, saves with
// live apply, and reports the result inline. Reached through the SoftAP
// captive portal, where the WPA2 passphrase is the device password, so the
// admin unlock is bypassed per request by the firmware (peer on the SoftAP).
import * as R from "/js/runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
const CONNECT_WAIT_MS = 60000;
// Mirrors the firmware's LIVE_APPLY_ATTEMPT_MS onboarding bound: the device
// fails a wrong-password attempt by 20 s, so the countdown tells the user
// how long this can take before they see a result.
const ATTEMPT_WINDOW_MS = 20000;
const POLL_MS = 1000;
let bound = false,
  pollTimer = 0,
  pollToken = 0,
  chainBusyToken = -1,
  state = "idle",
  lastNetworks = null,
  waitStartedAt = 0,
  sawConnecting = false,
  savedWifiSleep = true,
  savedWifiSleepLoaded = false;
const els = {};

// One live poll chain at a time: stop() bumps the token so a pending tick
// cannot re-arm, and start() relaunches the chain for the current state.
function armPoll(fn, ms) {
  const token = pollToken;
  pollTimer = setTimeout(() => {
    if (token === pollToken) fn();
  }, ms);
}

function signalGlyph(rssi) {
  return rssi >= -60 ? "▂▄▆" : rssi >= -80 ? "▂▄" : "▂";
}

function show(name) {
  for (const key of ["setupScanning", "setupForm", "setupConnecting", "setupSuccess", "setupError", "setupLocked"])
    if (els[key]) els[key].hidden = key !== name;
}

function setStatusError(text) {
  const el = $("setupFieldError");
  if (!el) return;
  el.hidden = !text;
  el.textContent = text || "";
}

function selectedNetwork() {
  const open = $("setupNetwork").selectedOptions[0];
  return open && open.value ? open : null;
}

function syncPasswordVisibility() {
  const network = selectedNetwork(),
    manual = $("setupHiddenToggle").checked;
  const open = manual ? false : network !== null && network.dataset.open === "true";
  $("setupPasswordWrap").hidden = open;
  $("setupPassword").disabled = open;
}

function renderPicker(networks) {
  const select = $("setupNetwork");
  const previous = select.value;
  select.replaceChildren();
  const prompt = document.createElement("option");
  prompt.value = "";
  prompt.textContent = networks.length
    ? __WEBUI_TEXT__("setup.choose_network")
    : __WEBUI_TEXT__("setup.no_networks_found");
  select.appendChild(prompt);
  for (const n of networks) {
    const option = document.createElement("option");
    option.value = n.ssid;
    option.dataset.open = String(n.open);
    option.textContent =
      n.open
        ? signalGlyph(n.rssi) + "  " + n.ssid
        : signalGlyph(n.rssi) + "  " + n.ssid + "  🔒";
    select.appendChild(option);
  }
  if (previous && [...select.options].some((o) => o.value === previous))
    select.value = previous;
  syncPasswordVisibility();
}

async function startScan() {
  state = "scanning";
  scanStartedAt = Date.now();
  // "Try again" only makes sense once a scan already ran; the first
  // automatic scan has nothing to retry.
  $("setupRescan").hidden = lastNetworks === null;
  show("setupScanning");
  try {
    await R.api("/api/v1/network/scan", { method: "POST", body: "{}" });
  } catch (e) {
    if (e && e.code === "ADMIN_LOCKED") return showLocked();
    scanFailed(e);
    return;
  }
  pollScan();
}

const SCAN_WAIT_MS = 30000;
let scanStartedAt = 0;

async function pollScan() {
  // One chain per poll generation: a fresh generation (stop()+start()) may
  // take over while a stale continuation is still unwinding.
  if (chainBusyToken === pollToken) return;
  const token = pollToken;
  chainBusyToken = token;
  try {
    const d = await R.api("/api/v1/network/scan");
    if (state !== "scanning" || token !== pollToken) return;
    // Page-side watchdog: whatever the device does, a scan never outlives
    // this bound on the onboarding card.
    if (Date.now() - scanStartedAt >= SCAN_WAIT_MS) {
      scanFailed(null);
      return;
    }
    if (d.state === "READY") {
      lastNetworks = d.networks || [];
      state = "form";
      renderPicker(lastNetworks);
      show("setupForm");
      return;
    }
    if (d.state === "FAILED" || d.state === "CANCELED") {
      scanFailed(null);
      return;
    }
    armPoll(pollScan, 500);
  } catch (e) {
    if (e && e.code === "ADMIN_LOCKED") {
      showLocked();
      return;
    }
    scanFailed(e);
  } finally {
    if (chainBusyToken === token) chainBusyToken = -1;
  }
}

function scanFailed(error) {
  lastNetworks = lastNetworks || [];
  renderPicker(lastNetworks);
  finishError(
    error
      ? R.formatCommandError(__WEBUI_TEXT__("setup.scan_failed"), error)
      : __WEBUI_TEXT__("setup.scan_failed"),
  );
}

function updateConnectingCountdown() {
  const el = $("setupConnectingCountdown");
  if (!el) return;
  const remaining = Math.ceil((waitStartedAt + ATTEMPT_WINDOW_MS - Date.now()) / 1000);
  el.hidden = false;
  el.textContent =
    remaining > 0
      ? __WEBUI_TEXT__("setup.connecting_deadline").replace("{n}", remaining)
      : __WEBUI_TEXT__("setup.connecting_slow");
}

function showLocked() {
  state = "locked";
  show("setupLocked");
}

function payloadFromForm() {
  const network = selectedNetwork(),
    manual = $("setupHiddenToggle").checked;
  const ssid = manual ? $("setupManualSsid").value.trim() : network ? network.value : "";
  const open = manual ? false : network !== null && network.dataset.open === "true";
  return {
    action: "save",
    ssid,
    password: open ? "" : $("setupPassword").value,
    open,
    wifiSleep: savedWifiSleep,
    ipMode: "dhcp",
    apply: "live",
  };
}

async function connect() {
  const payload = payloadFromForm();
  const manual = $("setupHiddenToggle").checked;
  if (!payload.ssid) {
    setStatusError(
      manual
        ? __WEBUI_TEXT__("setup.network_name_required")
        : __WEBUI_TEXT__("setup.choose_network"),
    );
    return;
  }
  if (!payload.open && payload.password.length < 8) {
    setStatusError(__WEBUI_TEXT__("setup.password_required"));
    return;
  }
  setStatusError("");
  state = "connecting";
  waitStartedAt = Date.now();
  sawConnecting = false;
  $("setupConnectingText").textContent =
    __WEBUI_TEXT__("setup.connecting_to").replace("{x}", payload.ssid);
  show("setupConnecting");
  updateConnectingCountdown();
  try {
    await R.api("/api/v1/network", { method: "POST", body: R.body(payload) });
  } catch (e) {
    if (e && e.code === "ADMIN_LOCKED") return showLocked();
    state = "form";
    show("setupForm");
    setStatusError(R.formatCommandError(__WEBUI_TEXT__("setup.save_failed"), e));
    return;
  }
  pollConnect();
}

async function pollConnect() {
  if (chainBusyToken === pollToken) return;
  const token = pollToken;
  chainBusyToken = token;
  let status = null;
  try {
    status = await R.api("/api/v1/status/admin");
  } catch (e) {
    /* transient poll failure: keep waiting within the bounded window */
  } finally {
    if (chainBusyToken === token) chainBusyToken = -1;
  }
  if (state !== "connecting" || token !== pollToken) return;
  updateConnectingCountdown();
  const network = status && status.network;
  if (network && network.staState === "CONNECTED" && network.staIp) {
    await finishSuccess(status);
    return;
  }
  // A failed attempt only reads as failure once the device has actually
  // tried to connect: before the saved command is applied the state is still
  // NOT_CONFIGURED/DISCONNECTED, and one lost fetch is not a verdict.
  if (network && (network.staState === "CONNECTING" || network.staState === "CONNECTED"))
    sawConnecting = true;
  const failed =
    sawConnecting &&
    network &&
    (network.staState === "DISCONNECTED" ||
      network.staState === "FAILED" ||
      network.staState === "NOT_CONFIGURED");
  if (Date.now() - waitStartedAt >= CONNECT_WAIT_MS || failed) {
    finishError(
      network && network.wifiConfigured === false
        ? __WEBUI_TEXT__("setup.error_reverted")
        : __WEBUI_TEXT__("setup.error_could_not_connect"),
    );
    return;
  }
  armPoll(pollConnect, POLL_MS);
}

async function finishSuccess(status) {
  const network = status.network,
    name = network.mdnsHost || network.deviceName || "",
    links = [];
  if (name) links.push("http://" + name + ".local");
  if (network.staIp) links.push("http://" + network.staIp);
  $("setupSuccessText").textContent =
    __WEBUI_TEXT__("setup.success_text")
      .replace("{x}", network.ssid || $("setupNetwork").value || "")
      .replace("{y}", network.staIp || "");
  $("setupSuccessLinks").replaceChildren();
  for (const url of links) {
    const a = document.createElement("a");
    a.href = url;
    a.textContent = url;
    $("setupSuccessLinks").appendChild(a);
    $("setupSuccessLinks").appendChild(document.createElement("br"));
  }
  state = "success";
  show("setupSuccess");
  if (network.configState === "PENDING") {
    // Tell the device to keep this network; it then finishes on its own and
    // the setup network goes away moments later.
    try {
      await R.api("/api/v1/network", {
        method: "POST",
        body: R.body({ action: "confirm" }),
      });
    } catch (e) {
      /* the 180 s confirm window still reverts if this never lands */
    }
  }
}

function finishError(text) {
  state = "error";
  $("setupErrorText").textContent = text;
  // The form stays visible below the diagnosis so the next attempt is one
  // edit away; the device keeps retrying (and auto-reverts) on its own.
  $("setupConnecting").hidden = true;
  $("setupError").hidden = false;
  $("setupForm").hidden = false;
}

function bind() {
  $("setupRescan").onclick = () => {
    setStatusError("");
    startScan();
  };
  $("setupRetry").onclick = () => {
    setStatusError("");
    startScan();
  };
  $("setupHiddenToggle").onchange = () => {
    const manual = $("setupHiddenToggle").checked;
    $("setupManualSsid").hidden = !manual;
    $("setupManualLabel").hidden = !manual;
    $("setupNetwork").disabled = manual;
    syncPasswordVisibility();
  };
  $("setupNetwork").onchange = syncPasswordVisibility;
  const passToggle = $("setupPasswordShow");
  const passShowText = __WEBUI_TEXT__("setup.show");
  const passHideText = __WEBUI_TEXT__("setup.hide");
  passToggle.onclick = () => {
    const input = $("setupPassword");
    const show = input.type === "password";
    input.type = show ? "text" : "password";
    passToggle.textContent = show ? passHideText : passShowText;
    passToggle.setAttribute("aria-pressed", String(show));
  };
  $("setupForm").onsubmit = (e) => {
    e.preventDefault();
    connect();
  };
}

export function stop() {
  clearTimeout(pollTimer);
  pollToken++;
}

export async function start() {
  // Also resumes after the tab was hidden or another view was open: the
  // scan/connect chains are paused by stop(), never abandoned.
  captureSleepPreference();
  if (state === "idle") return startScan();
  if (state === "scanning") {
    // Timers were suspended while hidden: settle an expired scan now
    // instead of re-entering a dead wait.
    if (Date.now() - scanStartedAt >= SCAN_WAIT_MS) scanFailed(null);
    else pollScan();
  } else if (state === "connecting") pollConnect();
  return true;
}

function captureSleepPreference() {
  savedWifiSleepLoaded = true;
  R.api("/api/v1/status/admin")
    .then((s) => {
      if (s && s.network && typeof s.network.wifiSleep === "boolean")
        savedWifiSleep = s.network.wifiSleep;
    })
    .catch(() => {});
}

export function init() {
  if (bound) return;
  bound = true;
  for (const id of [
    "setupScanning",
    "setupForm",
    "setupConnecting",
    "setupSuccess",
    "setupError",
    "setupLocked",
  ])
    els[id] = $(id);
  bind();
  if (!document.hidden && R.webUiPollingActive()) start();
  else captureSleepPreference();  // hidden now; start() runs on return
}
