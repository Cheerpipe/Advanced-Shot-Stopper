"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
let ready = false;
export function applyStatus(s) {
  R.applyHomeStatus(s);
  const power = micraPower(s.lineaMicra);
  $("homeMicraPower").textContent = power;
  $("homeMicraCleaning").textContent = R.formatMicraCleaning(s.lineaMicra);
  const rs = $("machineRowState"),
    st = $("state"),
    row = $("machineRow");
  if (row) {
    const off = power.startsWith("OFF"),
      rdy = !off && s.state === "READY";
    rs.textContent = off
      ? __WEBUI_TEXT__("home.turned_off")
      : rdy
        ? __WEBUI_TEXT__("runtime.ready_2")
        : st.textContent;
    rs.classList.toggle("stateReady", rdy);
    rs.classList.toggle("stateFault", !rdy && (off || st.classList.contains("stateFault")));
    row.classList.toggle("lampBad", !rdy);
  }
}
export function init() {
  if (ready) return;
  ready = true;
  R.registerViewStatus("home", applyStatus);
  const rinseButton = $("rinseButton");
  if (rinseButton) rinseButton.onclick = () => R.command("/api/v1/control/rinse");
  const stopButton = $("stopButton");
  if (stopButton)
    stopButton.onclick = () =>
      stopButton.dataset.mode === "stop"
        ? R.command("/api/v1/control/stop")
        : R.command("/api/v1/control/paddle", { on: true });
  const forcePulseButton = $("forcePulseButton");
  if (forcePulseButton) forcePulseButton.onclick = () => R.command("/api/v1/control/force-pulse");
  [
    ["overrideIdleLink", "off"],
    ["overrideBrewingLink", "on"],
  ].forEach(([i, s]) => {
    const a = $(i);
    if (!a) return;
    a.onclick = (e) => {
      e.preventDefault();
      if (a.getAttribute("aria-disabled") === "true") return;
      R.command("/api/v1/control/state-override", { state: s });
    };
  });
  const renameScaleLink = $("renameScaleLink");
  if (renameScaleLink)
    renameScaleLink.onclick = (e) => {
      e.preventDefault();
      if (renameScaleLink.getAttribute("aria-disabled") === "true") return;
      R.renameScale();
    };
  if ($("homeBrewByWeight")) $("homeBrewByWeight").onchange = R.persistHomeBrewByWeight;
  if ($("homeNoScaleBbwEnabled")) $("homeNoScaleBbwEnabled").onchange = R.persistHomeNoScaleBbw;
  if ($("homeFastExtractionGuardEnabled"))
    $("homeFastExtractionGuardEnabled").onchange = () =>
      R.persistHomeGuard(
        "homeFastExtractionGuardEnabled",
        "homeFastExtractionGuardEnabledState",
        "fastExtractionGuardEnabled",
        1,
      );
  if ($("homeAvoidAccidentalTouchEnabled"))
    $("homeAvoidAccidentalTouchEnabled").onchange = () =>
      R.persistHomeGuard(
        "homeAvoidAccidentalTouchEnabled",
        "homeAvoidAccidentalTouchEnabledState",
        "avoidAccidentalTouchEnabled",
        1,
      );
  if ($("homeSlowExtractionGuardEnabled"))
    $("homeSlowExtractionGuardEnabled").onchange = () =>
      R.persistHomeGuard(
        "homeSlowExtractionGuardEnabled",
        "homeSlowExtractionGuardEnabledState",
        "slowExtractionGuardEnabled",
        1,
      );
  if ($("homeAutoToManualGuardEnabled"))
    $("homeAutoToManualGuardEnabled").onchange = () =>
      R.persistHomeGuard(
        "homeAutoToManualGuardEnabled",
        "homeAutoToManualGuardEnabledState",
        "autoToManualGuardEnabled",
        1,
      );
  if ($("homeCupProtectionEnabled"))
    $("homeCupProtectionEnabled").onchange = () =>
      R.persistHomeGuard(
        "homeCupProtectionEnabled",
        "homeCupProtectionEnabledState",
        "cupProtectionEnabled",
        1,
      );
}
function micraPower(m) {
  const p = m?.quality === "optimistic" && (m.optimisticOn ? "ON" : m.optimisticOff ? "OFF" : "");
  return p
    ? p + " - " + __WEBUI_TEXT__("home.optimistic")
    : ["ON", "OFF"].includes(m?.powerState)
      ? m.powerState
      : __WEBUI_TEXT__("runtime.unknown");
}
