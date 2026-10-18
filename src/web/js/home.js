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

// Equipment row summaries: "<scale name> - 34g", "Present - 46g". Runtime
// already wrote the bare state; only fresh, valid weights get the suffix.
function grams(g) {
  return typeof g === "number" && Number.isFinite(g) ? " - " + (Math.round(g) || 0) + "g" : "";
}
function renderEquipmentWeights(s) {
  const sc = s.scale || {},
    c = s.cupPresence || {};
  if (!sc.available || sc.streamState !== "FRESH") return;
  const w = typeof sc.observedWeightG === "number" ? sc.observedWeightG : sc.currentWeightG,
    name =
      String(sc.preferredFriendlyName || "").trim() || String(sc.preferredName || "").trim();
  if (name) $("scale").textContent = name + grams(w);
  else $("scale").textContent += grams(w);
  if ((c.state === "PRESENT" || c.present) && c.weightValid === true)
    $("cupState").textContent += grams(c.weightG);
}

// " - 93°" from the brew boiler target, only on machines that report one.
function micraTargetSuffix(lm) {
  const d = lm && lm.boiler && lm.boiler.coffee ? lm.boiler.coffee.targetDeciC : 0;
  return typeof d === "number" && d > 0 ? " - " + Math.round(d / 10) + "°" : "";
}

function renderMicraBoilers() {
  // Reads the live cache: once the stream is invalidated the cache is null
  // and the ticker leaves the reset placeholders alone.
  const st = R.homeStatusCache(),
    lm = st?.lineaMicra;
  renderReadyMsg(st, lm);
  if (!lm) return;
  const brew = $("homeMicraBrewBoiler"),
    steam = $("homeMicraSteamBoiler");
  if (brew) brew.textContent = micraBoilerText(lm, "coffee");
  if (steam) steam.textContent = micraBoilerText(lm, "steam");
}

export function applyStatus(s) {
  R.applyHomeStatus(s);
  renderEquipmentWeights(s);
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
        ? __WEBUI_TEXT__("runtime.ready_2") + micraTargetSuffix(micraActive && lm)
        : labels[lm.readiness] ||
          (power.indexOf("ON") === 0 ? __WEBUI_TEXT__("home.on") : $("state").textContent);
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
  if ($("grindHintNewBeans")) $("grindHintNewBeans").onclick = () => R.startNewCoffee();
  if ($("grindHintLater")) $("grindHintLater").onclick = () => R.dismissCard($("grindHintCard").dataset.key);
  if ($("scaleLossDiscoveryOk"))
    $("scaleLossDiscoveryOk").onclick = () => R.dismissCard("ssCard:scaleLossDiscovery");
  quickWeight = initQuickWeight();
  quickWeightWanted = new URLSearchParams(location.search).has("edit_weight");
}
function renderReadyMsg(s, lm) {
  const msg = $("homeReadyMsg"),
    cfg = s?.config || {},
    sc = s?.scale || {},
    g = s?.noScaleShotGuard || {},
    idle = !!s && !["BREW", "RINSE", "MANUAL_NO_SCALE"].includes(s.state),
    mins = Math.ceil(((lm?.boiler?.coffee?.readyAtUtcSec || 0) * 1000 - Date.now()) / 60000);
  if (!msg) return;
  const text = !idle
    ? ""
    : cfg.brewByWeight && !(sc.available && sc.streamState === "FRESH")
      ? !(g.enabled && g.armed)
        ? __WEBUI_TEXT__("home.no_scale_manual")
        : g.mode === "require_scale"
          ? __WEBUI_TEXT__("home.no_scale_required")
          : __WEBUI_TEXT__("home.no_scale_warn")
      : s.machineIntegration === "linea_micra_cloud" && lm?.accountConfigured && lm.observeState && lm.readiness === "warming_up"
        ? __WEBUI_TEXT__("home.machine_warming") +
          (mins > 0 ? " · " + __WEBUI_TEXT__("home.ready_in").replace("%1", mins) : "")
        : "";
  if (text && msg.textContent !== text) msg.textContent = text;
  R.reveal(msg, !!text);
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
    limitMax = $("qwLimitMax"),
    ratio = $("qwRatio");
  // Firmware preset goal: decigrams, 10.0–200.0 g (settings validation).
  const MIN = 10,
    MAX = 200,
    PX_PER_DG = 6, // 60 px per gram: tenths read clearly
    BASE_DG_PER_PX = 1 / PX_PER_DG, // the ruler tracks the finger 1:1
    SPEED_CAP = 3, // gentle gearing: only quick flicks go past 1:1
    SPEED_K = 1.5;
  const inner = document.createElement("div");
  inner.className = "qwScaleIn";
  const ticks = [];
  // Ticks run 15 g past both ends so the strip stays full (760 px sheet);
  // majors label whole grams, mediums halves, minors tenths.
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
  // Editable range [lo, hi] in decigrams mirrors the Settings rules for the
  // relative margins, so the dial can never offer a target Save would reject.
  let lo = MIN * 10,
    hi = MAX * 10,
    value = MIN * 10,
    openValue = MIN * 10,
    dose = 0;
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
    ratio.hidden = !dose;
    ratio.firstChild.textContent = __WEBUI_TEXT__("home.dose_in").replace("%1", dose.toFixed(1));
    ratio.lastChild.textContent = "1:" + (value / 10 / dose).toFixed(1);
    zone.setAttribute("aria-valuenow", (value / 10).toFixed(1));
    zone.setAttribute("aria-valuetext", (value / 10).toFixed(1) + " g");
    limitMin.hidden = value > lo;
    limitMax.hidden = value < hi;
    def.textContent = (openValue / 10).toFixed(1);
    resetBtn.classList.toggle("show", value !== openValue);
    doneBtn.disabled = value === openValue;
  };
  const open = () => {
    const preset = R.presetState.items.find((x) => x.id === R.presetState.activeId);
    if (!preset || !preset.goalWeightG) return;
    // Margins are relative and follow the target: an enabled slow margin
    // keeps 5 g in the cup, an enabled fast margin stays within a quarter
    // of the target and the extra yield within the 200 g maximum.
    const slow = !!preset.slowExtractionGuardEnabled && Number.isFinite(+preset.slowMarginG),
      fast = !!preset.fastExtractionGuardEnabled && Number.isFinite(+preset.fastMarginG);
    lo = Math.ceil(Math.max(MIN, slow ? +preset.slowMarginG + 5 : 0, fast ? 4 * preset.fastMarginG : 0) * 10 - 1e-6);
    hi = Math.floor((MAX - (fast ? +preset.fastMarginG : 0)) * 10 + 1e-6);
    if (hi < lo) {
      lo = MIN * 10;
      hi = MAX * 10;
    } // a recipe with no valid target must not lock the dial
    for (const { d, node, label } of ticks)
      for (const el of label ? [node, label] : [node]) el.classList.toggle("out", d < lo || d > hi);
    limitMin.classList.toggle("toneSlow", slow);
    limitMax.classList.toggle("toneFast", fast);
    value = openValue = clamp(Math.round(+preset.goalWeightG * 10));
    dose = +preset.doseG || 0;
    presetEl.textContent = preset.name || "";
    zone.setAttribute("aria-valuemin", (lo / 10).toFixed(1));
    zone.setAttribute("aria-valuemax", (hi / 10).toFixed(1));
    paint();
    sheet.inert = false;
    backdrop.inert = false;
    sheet.classList.add("open");
    backdrop.classList.add("show");
    document.body.classList.add("qwOpen");
    zone.focus();
  };
  const commit = async (dg) => {
    try {
      if (!(await R.cleanBrewForm())) return;
      const field = $("goalWeightG"),
        rejected = String(field.value);
      field.value = (dg / 10).toFixed(1);
      // A rejected value must not linger in the form as a phantom edit: put
      // the previous value back so the brew baseline and the live warnings
      // keep describing the real saved state.
      if (!(await R.saveBrewPreset(undefined, undefined, true))) field.value = rejected;
    } catch (e) {
      R.message(
        R.formatCommandError(__WEBUI_TEXT__("runtime.could_not_save_preset"), e),
        "error",
      );
    }
  };
  const close = (save) => {
    if (!sheet.classList.contains("open")) return;
    halt();
    stop();
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
  const vel = { t: 0, v: 0 };
  let pending = 0,
    fling = 0, // signed release speed, decigrams per ms
    glide = 0;
  const halt = () => globalThis.cancelAnimationFrame?.(glide);
  // Inertia: a released flick keeps sliding and eases out with friction
  // (about 200 ms of travel at release speed), stopping at either limit.
  const coast = () => {
    let last = performance.now();
    const run = (now) => {
      const dt = now - last;
      last = now;
      fling *= Math.pow(0.995, dt);
      pending += fling * dt;
      const next = clamp(value + Math.trunc(pending));
      pending %= 1;
      if (next === value && (value === lo || value === hi)) fling = 0;
      value = next;
      paint();
      if (Math.abs(fling) > 0.002) glide = requestAnimationFrame(run);
    };
    if (Math.abs(fling) > 0.02) glide = requestAnimationFrame(run);
  };
  drag(zone, {
    start: (e) => {
      halt();
      vel.t = performance.now();
      vel.v = 0;
      fling = 0;
      pending = 0;
      return { x: e.clientX, dx: 0 };
    },
    move: (e, ctx) => {
      const dx = e.clientX - ctx.x;
      ctx.x = e.clientX;
      const now = performance.now(),
        dt = Math.max(4, now - vel.t);
      // A reversal restarts the speed estimate: stale speed from the outgoing
      // gesture would sling the value past the intended stop.
      if (dx * ctx.dx < 0) vel.v = 0;
      ctx.dx = dx;
      // Slow gestures stay surgical; sustained speed gears up to SPEED_CAP×
      // so one drag can cross the range without releasing.
      vel.v = 0.75 * vel.v + 0.25 * (Math.abs(dx) / dt);
      vel.t = now;
      // Sub-tenth motion accrues in pending instead of rounding away per
      // event, so slow drags still move the dial a tenth at a time.
      // Direct manipulation: the finger carries the ruler, so dragging left
      // brings higher grams under the needle.
      const step = -dx * BASE_DG_PER_PX * speedMul(vel.v);
      fling = 0.75 * fling + (0.25 * step) / dt;
      pending += step;
      value = clamp(value + Math.trunc(pending));
      pending %= 1;
      paint();
    },
    end: () => {
      // A finger that rested before lifting leaves no inertia.
      if (performance.now() - vel.t > 80) fling = 0;
      else if (globalThis.requestAnimationFrame) coast();
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
  const nudge = (d) => {
    halt();
    value = clamp(value + d);
    paint();
  };
  zone.addEventListener("keydown", (e) => {
    const step = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1, PageUp: 10, PageDown: -10 }[e.key];
    if (!step) return;
    e.preventDefault();
    nudge(step);
  });
  let hold = 0;
  const stop = () => clearTimeout(hold);
  for (const [id, dir] of [
    ["qwMinus", -1],
    ["qwPlus", 1],
  ]) {
    const btn = $(id);
    btn.onpointerdown = (e) => {
      e.preventDefault();
      let n = 0;
      const step = () => {
        nudge(dir * (n > 20 ? 10 : 1));
        hold = setTimeout(step, n++ ? 70 : 400);
      };
      step();
    };
    btn.onpointerup = btn.onpointerleave = btn.onpointercancel = stop;
    btn.onkeydown = (e) => (e.key === "Enter" || e.key === " ") && (e.preventDefault(), nudge(dir));
  }
  pen.onclick = open;
  addEventListener("click", (e) => e.target.closest(".presetAccEdit") && open());
  doneBtn.onclick = () => close(true);
  resetBtn.onclick = () => {
    halt();
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
