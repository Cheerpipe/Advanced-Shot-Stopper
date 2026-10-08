"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
let ready = false;
let quickWeight = null;
let quickWeightWanted = false;
export function applyStatus(s) {
  R.applyHomeStatus(s);
  const brewing = ["BREW", "RINSE", "MANUAL_NO_SCALE"].includes(s.state);
  if (quickWeight && brewing) quickWeight.close();
  // URL launcher: on a controller with no shots yet the hero card is absent,
  // so ?edit_weight=1 is the only entry point. Wait for the active preset
  // and the admin lock to settle, then open once and drop the parameter.
  if (quickWeightWanted && quickWeight && !brewing) {
    const preset = R.presetState.items.find((x) => x.id === R.presetState.activeId);
    const pen = $("shotGoalEdit");
    if (preset && preset.goalWeightG && pen && !pen.disabled) {
      quickWeightWanted = false;
      history.replaceState(null, "", location.pathname);
      quickWeight.open();
    }
  }
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
  quickWeight = initQuickWeight();
  quickWeightWanted = new URLSearchParams(location.search).has("edit_weight");
}
function initQuickWeight() {
  const sheet = $("qwSheet"),
    pen = $("shotGoalEdit"),
    zone = $("qwZone"),
    grab = $("qwGrab"),
    head = $("qwHead");
  if (!sheet || !pen || !zone || !grab || !head) return null;
  const backdrop = $("qwBackdrop"),
    scale = $("qwScale"),
    num = $("qwNum"),
    def = $("qwDef"),
    resetBtn = $("qwReset"),
    presetEl = $("qwPreset"),
    doneBtn = $("qwDone");
  // Firmware preset goal: uint8 whole grams, 10–200 (settings validation).
  const MIN = 10,
    MAX = 200,
    PX_PER_G = 9,
    BASE_G_PER_PX = 0.008,
    SPEED_CAP = 10,
    SPEED_K = 14;
  const inner = document.createElement("div");
  inner.className = "qwScaleIn";
  const ticks = [];
  for (let v = MIN; v <= MAX; v++) {
    const tick = document.createElement("i");
    if (v % 5 === 0) tick.className = "big";
    inner.appendChild(tick);
    ticks.push({ v, node: tick });
  }
  scale.appendChild(inner);
  let value = MIN,
    openValue = MIN;
  const clamp = (v) => Math.max(MIN, Math.min(MAX, v));
  const speedMul = (v) => 1 + Math.min(SPEED_CAP - 1, Math.abs(v || 0) * SPEED_K);
  const paint = () => {
    const width = scale.getBoundingClientRect().width,
      shown = Math.round(value);
    for (const tick of ticks) {
      const p = width / 2 + (tick.v - value) * PX_PER_G;
      tick.node.style.display = p > -6 && p < width + 6 ? "" : "none";
      tick.node.style.left = p.toFixed(1) + "px";
      tick.node.classList.toggle("now", tick.v === shown);
    }
    num.textContent = shown;
    zone.setAttribute("aria-valuenow", shown);
    zone.setAttribute("aria-valuetext", shown + " g");
    def.textContent = openValue;
    resetBtn.classList.toggle("show", shown !== openValue);
  };
  const open = () => {
    const preset = R.presetState.items.find((x) => x.id === R.presetState.activeId);
    if (!preset || !preset.goalWeightG) return;
    value = openValue = clamp(preset.goalWeightG);
    presetEl.textContent = preset.name || "";
    paint();
    sheet.inert = false;
    backdrop.inert = false;
    sheet.classList.add("open");
    backdrop.classList.add("show");
    document.body.classList.add("qwOpen");
    doneBtn.focus();
  };
  const commit = async (grams) => {
    try {
      await R.ensureSettingsHydrated();
      $("goalWeightG").value = String(grams);
      await R.saveBrewPreset();
    } catch (e) {
      R.message(
        R.formatCommandError(__WEBUI_TEXT__("runtime.could_not_save_brew_settings"), e),
        "error",
      );
    }
  };
  const close = (save) => {
    if (!sheet.classList.contains("open")) return;
    sheet.classList.remove("open");
    backdrop.classList.remove("show");
    sheet.inert = true;
    backdrop.inert = true;
    document.body.classList.remove("qwOpen");
    if (!pen.hidden) pen.focus();
    if (save && Math.round(value) !== openValue) commit(Math.round(value));
  };
  const drag = (node, handlers) => {
    node.addEventListener("pointerdown", (event) => {
      if (event.pointerType === "mouse" && event.button !== 0) return;
      event.preventDefault();
      try {
        node.setPointerCapture(event.pointerId);
      } catch {
        /* synthetic pointers cannot be captured */
      }
      const ctx = handlers.start ? handlers.start(event) : {};
      const move = (ev) => handlers.move && handlers.move(ev, ctx);
      const end = (ev) => {
        node.removeEventListener("pointermove", move);
        node.removeEventListener("pointerup", end);
        node.removeEventListener("pointercancel", end);
        handlers.end && handlers.end(ev, ctx);
      };
      node.addEventListener("pointermove", move);
      node.addEventListener("pointerup", end);
      node.addEventListener("pointercancel", end);
    });
  };
  const vel = { x: 0, t: 0, v: 0 };
  drag(zone, {
    start: (e) => {
      vel.x = e.clientX;
      vel.t = performance.now();
      vel.v = 0;
      return { x: e.clientX };
    },
    move: (e, ctx) => {
      const dx = e.clientX - ctx.x;
      ctx.x = e.clientX;
      const now = performance.now(),
        dt = Math.max(4, now - vel.t);
      // Slow gestures stay surgical; sustained speed gears up to SPEED_CAP×
      // so one drag can cross the range without releasing.
      vel.v = 0.75 * vel.v + 0.25 * (Math.abs(e.clientX - vel.x) / dt);
      vel.x = e.clientX;
      vel.t = now;
      value = clamp(value + dx * BASE_G_PER_PX * speedMul(vel.v));
      paint();
    },
  });
  const dismissDrag = {
    start: (e) => {
      sheet.classList.add("nodrag");
      return { y: e.clientY, dy: 0 };
    },
    move: (e, ctx) => {
      ctx.dy = Math.max(0, e.clientY - ctx.y);
      sheet.style.transform = "translateY(" + ctx.dy + "px)";
    },
    end: (e, ctx) => {
      sheet.classList.remove("nodrag");
      if (ctx.dy > 90) {
        sheet.style.transition = "transform .3s ease-in";
        sheet.style.transform = "translateY(105%)";
        setTimeout(() => {
          sheet.style.transition = "";
          sheet.style.transform = "";
          close(true);
        }, 290);
      } else sheet.style.transform = "";
    },
  };
  drag(grab, dismissDrag);
  drag(head, dismissDrag);
  zone.addEventListener("keydown", (e) => {
    const step =
      e.key === "ArrowUp" || e.key === "ArrowRight"
        ? 1
        : e.key === "ArrowDown" || e.key === "ArrowLeft"
          ? -1
          : e.key === "PageUp"
            ? 10
            : e.key === "PageDown"
              ? -10
              : 0;
    if (!step) return;
    e.preventDefault();
    value = clamp(Math.round(value) + step);
    paint();
  });
  pen.onclick = open;
  doneBtn.onclick = () => close(true);
  resetBtn.onclick = () => {
    value = openValue;
    paint();
  };
  backdrop.onclick = () => close(true);
  sheet.addEventListener("keydown", (e) => {
    if (e.key === "Escape") close(true);
  });
  addEventListener("resize", () => {
    if (sheet.classList.contains("open")) paint();
  });
  return { open, close };
}
function micraPower(m) {
  const p = m?.quality === "optimistic" && (m.optimisticOn ? "ON" : m.optimisticOff ? "OFF" : "");
  return p
    ? p + " - " + __WEBUI_TEXT__("home.optimistic")
    : ["ON", "OFF"].includes(m?.powerState)
      ? m.powerState
      : __WEBUI_TEXT__("runtime.unknown");
}
