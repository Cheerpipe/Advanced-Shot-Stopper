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
  if ($("homeBrewByWeight"))
    $("homeBrewByWeight").onchange = $("homeBrewManual").onchange = R.persistHomeBrewByWeight;
  if ($("homeNoScaleBbwEnabled")) $("homeNoScaleBbwEnabled").onchange = R.persistHomeNoScaleBbw;
  initProtections();
  if ($("grindHintNewBeans")) $("grindHintNewBeans").onclick = () => R.startNewCoffee();
  if ($("grindHintLater"))
    $("grindHintLater").onclick = () =>
      R.dismissCard($("grindHintCard").dataset.key, $("grindHintCard").dataset.median);
  if ($("scaleLossDiscoveryOk"))
    $("scaleLossDiscoveryOk").onclick = () => R.dismissCard("ssCard:scaleLossDiscovery");
  quickWeight = initQuickWeight();
  quickWeightWanted = new URLSearchParams(location.search).has("edit_weight");
}
// Shared safety protections (the device-wide switches) ask before turning
// off; turning anything on, or a preset's fast/slow side, stays one tap.
function initProtections() {
  const surface = $("stopSurface"),
    row = $("protRow"),
    confirmBar = $("protConfirm");
  if (!surface || !row || !confirmBar) return;
  let turnOff = null;
  const settle = (off) => {
    confirmBar.hidden = true;
    if (off && turnOff) turnOff();
    turnOff = null;
  };
  row.onclick = () => row.setAttribute("aria-expanded", surface.classList.toggle("open"));
  $("protKeep").onclick = () => settle(false);
  $("protOff").onclick = () => settle(true);
  confirmBar.onkeydown = (e) => e.key === "Escape" && settle(false);
  for (const [h, s, c, , shared] of R.HOME_GUARD_SWITCHES)
    if ($(h))
      $(h).onchange = () => {
        if (!shared || $(h).checked) return R.persistHomeGuard(h, s, c);
        $(h).checked = true;
        turnOff = () => {
          $(h).checked = false;
          R.persistHomeGuard(h, s, c);
        };
        $(h).closest(".switchRow").after(confirmBar);
        confirmBar.hidden = false;
        $("protKeep").focus();
      };
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
  msg.hidden = !text;
  if (msg.textContent !== text) msg.textContent = text;
}
function initQuickWeight() {
  const sheet = $("qwSheet"),
    pen = $("shotGoalEdit"),
    range = $("qwRange"),
    grab = $("qwGrab"),
    head = $("qwHead");
  if (!sheet || !pen || !range || !grab || !head) return null;
  const backdrop = $("qwBackdrop"),
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
    MAX = 200;
  // Editable range [lo, hi] in decigrams mirrors the Settings rules for the
  // relative margins, so the sheet can never offer a target Save would reject.
  let lo = MIN * 10,
    hi = MAX * 10,
    value = MIN * 10,
    openValue = MIN * 10,
    dose = 0;
  const clamp = (d) => Math.max(lo, Math.min(hi, d));
  const paint = () => {
    const g = (value / 10).toFixed(1);
    num.textContent = g;
    ratio.textContent = dose
      ? __WEBUI_TEXT__("home.ratio_dose")
          .replace("%1", (value / 10 / dose).toFixed(1))
          .replace("%2", dose.toFixed(1))
      : "";
    range.value = g;
    range.setAttribute("aria-valuetext", g + " g");
    limitMin.hidden = value > lo;
    limitMax.hidden = value < hi;
    def.textContent = (openValue / 10).toFixed(1);
    resetBtn.classList.toggle("show", value !== openValue);
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
    } // a recipe with no valid target must not lock the sheet
    limitMin.classList.toggle("toneSlow", slow);
    limitMax.classList.toggle("toneFast", fast);
    range.min = (lo / 10).toFixed(1);
    range.max = (hi / 10).toFixed(1);
    value = openValue = clamp(Math.round(+preset.goalWeightG * 10));
    dose = +preset.doseG || 0;
    presetEl.textContent = preset.name || "";
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
    stop();
    sheet.classList.remove("open");
    backdrop.classList.remove("show");
    sheet.inert = true;
    backdrop.inert = true;
    document.body.classList.remove("qwOpen");
    if (!pen.hidden) pen.focus();
    if (save && value !== openValue) commit(value);
  };
  range.oninput = () => {
    value = clamp(Math.round(+range.value * 10));
    paint();
  };
  // −/+ step a tenth per tap; holding repeats and speeds up to a gram per
  // step so a long press can cross the range without the slider.
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
        value = clamp(value + dir * (n > 20 ? 10 : 1));
        paint();
        hold = setTimeout(step, n++ ? 70 : 400);
      };
      step();
    };
    btn.onpointerup = btn.onpointerleave = btn.onpointercancel = stop;
    btn.onkeydown = (e) => {
      if (e.key !== "Enter" && e.key !== " ") return;
      e.preventDefault();
      value = clamp(value + dir);
      paint();
    };
  }
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
      const ctx = handlers.start(event);
      const move = (ev) => handlers.move(ev, ctx);
      const end = (ev) => {
        node.removeEventListener("pointermove", move);
        node.removeEventListener("pointerup", end);
        node.removeEventListener("pointercancel", end);
        handlers.end(ev, ctx);
      };
      node.addEventListener("pointermove", move);
      node.addEventListener("pointerup", end);
      node.addEventListener("pointercancel", end);
    });
  };
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
  pen.onclick = open;
  addEventListener("click", (e) => e.target.closest(".presetAccEdit") && open());
  doneBtn.onclick = () => close(true);
  resetBtn.onclick = () => {
    stop();
    value = openValue;
    paint();
  };
  backdrop.onclick = () => close(true);
  sheet.addEventListener("keydown", (e) => {
    if (e.key === "Escape") close(true);
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
