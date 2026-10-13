"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
let ready = false;
let quickWeight = null;
let quickWeightWanted = false;
let micraBoilerTimer = 0;

// Boiler rows interpolate the warm-up estimate locally from the absolute
// readyAtUtcSec the stream delivers once; a stable estimate costs no frames.
function micraBoilerText(lm, boiler) {
  if (!lm || !lm.accountConfigured) return __WEBUI_TEXT__("runtime.not_connected");
  if (!lm.observeState) return __WEBUI_TEXT__("runtime.disabled");
  const data = lm.boiler ? lm.boiler[boiler] : null;
  const state = data ? data.state : "";
  if (state === "ready") return __WEBUI_TEXT__("runtime.ready_2");
  const label = __WEBUI_TEXT__("home.boiler_states").split("|")[
    ["heating_up", "no_water", "eco", "off", "standby"].indexOf(state)
  ];
  if (!label) return __WEBUI_TEXT__("runtime.unknown");
  const readyAt = (data && data.readyAtUtcSec ? data.readyAtUtcSec : 0) * 1000;
  if ((state === "heating_up" || state === "eco") && readyAt > Date.now())
    return (
      label +
      " · " +
      __WEBUI_TEXT__("home.ready_in").replace(
        "%1",
        String(Math.max(1, Math.ceil((readyAt - Date.now()) / 60000))),
      )
    );
  return label;
}

function renderMicraBoilers() {
  // Reads the live cache: once the stream is invalidated the cache is null
  // and the ticker leaves the reset placeholders alone.
  const lm = R.homeStatusCache()?.lineaMicra;
  if (!lm) return;
  const brew = $("homeMicraBrewBoiler"),
    steam = $("homeMicraSteamBoiler");
  if (brew) brew.textContent = micraBoilerText(lm, "coffee");
  if (steam) steam.textContent = micraBoilerText(lm, "steam");
}

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
  renderMicraBoilers();
  const rs = $("machineRowState"),
    st = $("state"),
    row = $("machineRow");
  if (row) {
    const lm = s.lineaMicra || {},
      micraActive =
        s.machineIntegration === "linea_micra_cloud" && lm.accountConfigured && lm.observeState,
      off = power.startsWith("OFF"),
      needsWater = lm.readiness === "needs_water",
      // With boiler evidence the lamp claims Ready only when both boilers
      // are ready; without it (or without the integration) the controller
      // state decides, as before.
      rdy = !off && (micraActive ? lm.readiness === "ready" : s.state === "READY"),
      labels = {
        warming_up: __WEBUI_TEXT__("home.warming_up"),
        waiting_for_steam: __WEBUI_TEXT__("home.waiting_for_steam"),
        needs_water: __WEBUI_TEXT__("home.needs_water"),
      };
    rs.textContent = off
      ? __WEBUI_TEXT__("home.turned_off")
      : rdy
        ? __WEBUI_TEXT__("runtime.ready_2")
        : labels[lm.readiness] ||
          (power.indexOf("ON") === 0 ? __WEBUI_TEXT__("home.on") : st.textContent);
    rs.classList.toggle("stateReady", rdy);
    rs.classList.toggle(
      "stateFault",
      !rdy && (off || needsWater || st.classList.contains("stateFault")),
    );
    row.classList.toggle("lampBad", !rdy && (!micraActive || off || needsWater));
  }
}
export function init() {
  if (ready) return;
  ready = true;
  R.registerViewStatus("home", applyStatus);
  micraBoilerTimer = setInterval(() => {
    if (!document.hidden) renderMicraBoilers();
  }, 1000);
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
    doneBtn = $("qwDone"),
    limitMin = $("qwLimitMin"),
    limitMax = $("qwLimitMax");
  // Firmware preset goal: decigrams, 10.0–200.0 g (settings validation).
  const MIN = 10,
    MAX = 200,
    PX_PER_DG = 3, // 30 px per gram: single tenths stay grabbable
    BASE_DG_PER_PX = 0.08,
    SPEED_CAP = 10,
    SPEED_K = 14;
  const inner = document.createElement("div");
  inner.className = "qwScaleIn";
  const ticks = [];
  // Ticks keep drawing past both range ends so the drum fills the strip at
  // any value; only the value itself clamps to the editable range. Majors
  // label whole grams, mediums mark halves, minors the tenths. 15 g covers
  // the half-strip of the widest sheet (760 px / 2 / (PX_PER_DG * 10)).
  const base = (MIN - 15) * 10;
  for (let d = base; d <= (MAX + 15) * 10; d++) {
    const tick = document.createElement("i");
    let label = null;
    if (d % 10 === 0) {
      tick.className = "big";
      if (d >= MIN * 10 && d <= MAX * 10) {
        label = document.createElement("b");
        label.textContent = d / 10;
        label.style.left = ((d - base) * PX_PER_DG).toFixed(1) + "px";
        inner.appendChild(label);
      }
    } else if (d % 5 === 0) tick.className = "med";
    tick.style.left = ((d - base) * PX_PER_DG).toFixed(1) + "px";
    inner.appendChild(tick);
    ticks.push({ d, node: tick, label });
  }
  scale.appendChild(inner);
  // Editable range [lo, hi] in decigrams mirrors the Settings cross-field
  // rules: while a guard is enabled the goal must stay strictly inside its
  // recovery weight, so the dial can never offer a target Save would reject.
  let lo = MIN * 10,
    hi = MAX * 10,
    value = MIN * 10,
    openValue = MIN * 10;
  const clamp = (d) => Math.max(lo, Math.min(hi, d));
  const speedMul = (v) => 1 + Math.min(SPEED_CAP - 1, Math.abs(v || 0) * SPEED_K);
  let nowIdx = -1;
  // Ticks sit at fixed offsets; one translate slides the whole ruler and only
  // the "now" tick and the readouts change per frame, so a drag never
  // restyles the strip.
  const paint = () => {
    const width = scale.getBoundingClientRect().width;
    inner.style.transform =
      "translateX(" + (width / 2 - (value - base) * PX_PER_DG).toFixed(1) + "px)";
    if (ticks[nowIdx]) ticks[nowIdx].node.classList.remove("now");
    nowIdx = value - base;
    ticks[nowIdx].node.classList.add("now");
    num.textContent = (value / 10).toFixed(1);
    zone.setAttribute("aria-valuenow", (value / 10).toFixed(1));
    zone.setAttribute("aria-valuetext", (value / 10).toFixed(1) + " g");
    limitMin.hidden = value > lo;
    limitMax.hidden = value < hi;
    def.textContent = (openValue / 10).toFixed(1);
    resetBtn.classList.toggle("show", value !== openValue);
  };
  const open = () => {
    const preset = R.presetState.items.find((x) => x.id === R.presetState.activeId);
    if (!preset || !preset.goalWeightG) return;
    // Editable range mirrors the Settings cross-field rules: the goal stays
    // strictly inside each enabled guard's recovery weight, so the dial can
    // never offer a target Save would reject. Bounds land one decigram
    // inside the recovery weights — the closest step the 0.1 form accepts.
    const slow =
        !!preset.slowExtractionGuardEnabled && Number.isFinite(+preset.minRecoveryWeightG),
      fast =
        !!preset.fastExtractionGuardEnabled && Number.isFinite(+preset.maxRecoveryWeightG);
    lo = slow ? Math.min(MAX * 10, Math.ceil(+preset.minRecoveryWeightG * 10 - 1e-6) + 1) : MIN * 10;
    hi = fast ? Math.max(MIN * 10, Math.floor(+preset.maxRecoveryWeightG * 10 + 1e-6) - 1) : MAX * 10;
    if (hi < lo) {
      lo = MIN * 10;
      hi = MAX * 10;
    } // a recipe with no valid target must not lock the dial
    // Zone tints are reference only: ticks outside the editable range adopt
    // the color of the guard that limits them; valid ticks take the BBW green.
    for (const { d, node, label } of ticks) {
      const zone = d < MIN * 10 || d > MAX * 10 ? "" : d < lo ? "zSlow" : d > hi ? "zFast" : "zOk";
      for (const el of label ? [node, label] : [node])
        ["zSlow", "zFast", "zOk"].forEach((c) => el.classList.toggle(c, c === zone));
    }
    limitMin.classList.toggle("toneSlow", slow);
    limitMax.classList.toggle("toneFast", fast);
    value = openValue = clamp(Math.round(+preset.goalWeightG * 10));
    presetEl.textContent = preset.name || "";
    zone.setAttribute("aria-valuemin", (lo / 10).toFixed(1));
    zone.setAttribute("aria-valuemax", (hi / 10).toFixed(1));
    paint();
    sheet.inert = false;
    backdrop.inert = false;
    sheet.classList.add("open");
    backdrop.classList.add("show");
    document.body.classList.add("qwOpen");
    doneBtn.focus();
  };
  const commit = async (dg) => {
    try {
      await R.ensureSettingsHydrated();
      const field = $("goalWeightG"),
        rejected = String(field.value);
      field.value = (dg / 10).toFixed(1);
      // A rejected value must not linger in the form as a phantom edit: put
      // the previous value back so the brew baseline and the live warnings
      // keep describing the real saved state.
      if (!(await R.saveBrewPreset())) field.value = rejected;
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
    if (save && value !== openValue) commit(value);
  };
  const drag = (node, handlers) => {
    node.addEventListener("pointerdown", (event) => {
      if (event.pointerType === "mouse" && event.button !== 0) return;
      // Pointer capture would retarget the click of interactive children
      // (Done lives inside the grab bar) onto the drag node, swallowing it.
      if (event.target?.closest("button,a,input")) return;
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
      value = clamp(Math.round(value + dx * BASE_DG_PER_PX * speedMul(vel.v)));
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
    value = clamp(value + step);
    paint();
  });
  pen.onclick = open;
  // Only the already-active row's "Target XX g" opens the sheet: compare the
  // stream-maintained presetState.activeId, not the .open class applyPreset
  // paints on the tapped row before this bubble listener runs.
  addEventListener("click", (e) => {
    const item = e.target.closest(".presetAccTarget")?.closest(".presetAccItem");
    if (item && +item.dataset.id === R.presetState.activeId) open();
  });
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
