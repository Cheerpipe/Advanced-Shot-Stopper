"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
function scaleHistoryLabel(e) {
  const m = (e && e.mac) || "";
  if (!m) return "";
  return (
    ((e.friendlyName && String(e.friendlyName).trim()) ||
      (e.name && String(e.name).trim()) ||
      __WEBUI_TEXT__("runtime.unknown_2")) +
    __WEBUI_TEXT__("runtime.unknown_3") +
    m
  );
}
function updatePreferredScaleSelect(s) {
  const sel = $("preferredScaleSelect"),
    hint = $("preferredScalePauseHint");
  if (!sel) return;
  R.uiState.preferredScaleSelectSyncing = true;
  const pauseMs = (s.scale && s.scale.macCachePauseRemainingMs) || 0;
  if (hint)
    hint.textContent =
      pauseMs > 0
        ? __WEBUI_TEXT__("runtime.waiting") +
          Math.ceil(pauseMs / 1000) +
          __WEBUI_TEXT__("runtime.s_before_looking_for_a_scale")
        : "";
  sel.disabled = !R.controlsMutable;
  const history = s.scale && Array.isArray(s.scale.history) ? s.scale.history : [],
    preferred = (s.scale && s.scale.preferredMac) || "",
    prev = sel.value,
    keep = sel.dataset.pending === "1",
    draft = sel.selectedOptions && sel.selectedOptions[0];
  sel.innerHTML = "";
  if (!preferred || (keep && !prev)) {
    const first = document.createElement("option"),
      mode = s.config && s.config.scaleMacCacheMode;
    first.value = "";
    first.textContent =
      mode === "only" || mode === "prefer"
        ? __WEBUI_TEXT__("runtime.first_detected")
        : __WEBUI_TEXT__("runtime.no_preferred");
    sel.appendChild(first);
  }
  const seen = {};
  history.forEach((e) => {
    const mac = e && e.mac;
    if (!mac || seen[mac]) return;
    seen[mac] = 1;
    const o = document.createElement("option");
    o.value = mac;
    o.dataset.name = (e.name && String(e.name).trim()) || "";
    o.textContent = scaleHistoryLabel(e);
    sel.appendChild(o);
  });
  if (preferred && !seen[preferred]) {
    const o = document.createElement("option");
    o.value = preferred;
    o.dataset.name =
      (s.scale && s.scale.preferredName && String(s.scale.preferredName).trim()) || "";
    o.textContent =
      ((s.scale && s.scale.preferredName && String(s.scale.preferredName).trim()) ||
        __WEBUI_TEXT__("runtime.unknown_2")) +
      __WEBUI_TEXT__("runtime.unknown_3") +
      preferred;
    sel.appendChild(o);
  }
  if (keep && prev && !seen[prev] && prev !== preferred && draft) {
    const o = document.createElement("option");
    o.value = prev;
    o.dataset.name = draft.dataset.name || "";
    o.textContent = draft.textContent;
    sel.appendChild(o);
  }
  sel.value = keep ? prev : preferred || "";
  R.updateScalePreferenceOptions();
  sel.dataset.applied = preferred;
  const opt = sel.selectedOptions && sel.selectedOptions[0];
  R.updateScaleRenameUi(
    "preferredScaleRenameWrap",
    sel.value || "",
    opt && opt.dataset ? String(opt.dataset.name || "") : "",
  );
  R.uiState.preferredScaleSelectSyncing = false;
}
function markConfigDirty() {
  R.uiState.configDirty = true;
  R.setSaveDirty("saveConfigButton", "configDirtyHint", true);
}
function markBrewDirty() {
  R.uiState.brewDirty = true;
  R.setSaveDirty("saveBrewPresetButton", "brewDirtyHint", true);
}
function brewWarningList() {
  // Suppress only the empty-field "is required" result: it fires mid-typing
  // and is self-evident. Everything else is a true statement about the
  // pending set as a whole and self-clears as typing completes.
  return R.validateBrewClient().filter((e) => !e.required);
}
function refreshBrewWarnings() {
  if (!$("goalWeightG")) return;
  const warnings = brewWarningList(),
    sig = warnings.map((e) => e.id + "\u0001" + e.msg).join("\u0002");
  // Unchanged signature ⇒ zero DOM writes: poll refreshes never flicker the
  // warnings and never disturb red save errors on other fields.
  if (sig === R.uiState.brewWarnSig) return;
  R.uiState.brewWarnSig = sig;
  R.clearBrewWarningsDom();
  for (const {id, msg} of warnings) {
    const el = $(id);
    if (!el) continue;
    el.classList.add("fieldWarn");
    el.setAttribute("aria-invalid", "true");
    const s = document.createElement("small");
    s.className = "fieldWarning";
    s.id = "brewWarn" + ++R.uiState.brewWarnSeq;
    s.textContent = msg;
    (el.closest("label") || el.parentElement).appendChild(s);
    // One field can carry several warnings; link them all like markFieldError.
    const prev = el.getAttribute("aria-describedby");
    el.setAttribute("aria-describedby", (prev ? prev + " " : "") + s.id);
  }
  const chip = $("brewConflictChip");
  if (chip) {
    chip.classList.toggle("hidden", !warnings.length);
    chip.textContent =
      warnings.length === 1
        ? __WEBUI_TEXT__("settings.conflict_will_block_saving").replace("{0}", 1)
        : warnings.length > 1
          ? __WEBUI_TEXT__("settings.conflicts_will_block_saving").replace("{0}", warnings.length)
          : "";
  }
}
// After a failed save, red brew errors hand over to the live yellow state on
// the next edit anywhere in the panel. Scoped to brew fields so machine
// config red errors survive brew keystrokes.

function clearBrewFieldErrors() {
  for (const el of R.settingsSectionEls("brew")) {
    el.classList.remove("invalid");
    const host = el.closest("label") || el.parentElement;
    host.querySelectorAll("small.fieldError").forEach((s) => s.remove());
    // Rebuild the aria pairing from the smalls that survive the handover:
    // the signature-skip renderer will not rewrite an unchanged set.
    R.refreshFieldAria(el);
  }
}
function revertBrewPreset() {
  if (!R.uiState.brewDirty) return;
  if (!confirm(__WEBUI_TEXT__("runtime.discard_unsaved_changes"))) return;
  R.restoreSnapshot(R.uiState.brewBaseline);
  R.clearBrewDirty();
  R.clearFieldErrors();
  R.updateConfigGroups();
  refreshBrewWarnings();
  R.refreshStatus();
}
function revertMachineConfig() {
  if (!R.uiState.configDirty) return;
  if (!confirm(__WEBUI_TEXT__("runtime.discard_unsaved_changes"))) return;
  R.restoreSnapshot(R.uiState.configBaseline);
  if ($("preferredScaleSelect")) $("preferredScaleSelect").dataset.pending = "0";
  R.uiState.configBaseline = null;
  R.uiState.configDirty = false;
  R.setSaveDirty("saveConfigButton", "configDirtyHint", false);
  R.clearFieldErrors();
  R.updateConfigGroups();
  // R2's inputs (retare window, auto-retare) live in this section: restoring
  // them can change the retare floor, so the warnings re-run here.
  refreshBrewWarnings();
  R.refreshStatus();
}
function revertLineaMicra() {
  if (!R.uiState.micraDirty) return;
  if (!confirm(__WEBUI_TEXT__("runtime.discard_unsaved_changes"))) return;
  R.restoreSnapshot(R.uiState.micraBaseline);
  R.uiState.micraBaseline = null;
  R.uiState.micraDirty = false;
  R.setSaveDirty("lineaMicraSaveButton", "lineaMicraDirtyHint", false);
  R.clearFieldErrors();
  updateMicraDependentControls();
  R.refreshStatus();
}
function validateMachineClient() {
  const m = document.documentElement.classList.contains("momentaryMachine");
  const reedMachine = m && document.documentElement.classList.contains("reedMachine");
  let e =
    R.rangeCheck("rinseGestureS", 0.1, 5, __WEBUI_TEXT__("runtime.rinse_gesture"), {
      unit: __WEBUI_TEXT__("runtime.s"),
    }) ||
    R.rangeCheck("rinseDurationS", 0.5, 10, __WEBUI_TEXT__("runtime.rinse_duration"), {
      unit: __WEBUI_TEXT__("runtime.s"),
    }) ||
    (m
      ? R.rangeCheck("stopPulseMs", 50, 1000, __WEBUI_TEXT__("runtime.auto_stop_pulse"), {
          int: 1,
        }) ||
        R.rangeCheck("maxSinglePressMs", 100, 5000, __WEBUI_TEXT__("runtime.single_press_limit"), {
          int: 1,
        }) ||
        (reedMachine
          ? R.rangeCheck(
              "reedConfirmTimeoutS",
              0.2,
              5,
              __WEBUI_TEXT__("runtime.reed_confirm_timeout"),
              { unit: __WEBUI_TEXT__("runtime.s") },
            )
          : "") ||
        R.rangeCheck("shotReactTimeoutS", 3, 30, __WEBUI_TEXT__("runtime.shot_reaction_timeout"), {
          int: 1,
          unit: __WEBUI_TEXT__("runtime.s"),
        })
      : R.rangeCheck(
          "paddleReturnReminderMaxDurationMin",
          1,
          60,
          __WEBUI_TEXT__("runtime.paddle_limit"),
          { int: 1, unit: __WEBUI_TEXT__("runtime.min") },
        )) ||
    R.rangeCheck("retareWindowS", 0.5, 10, __WEBUI_TEXT__("runtime.retare_window"), {
      unit: __WEBUI_TEXT__("runtime.s"),
    }) ||
    R.rangeCheck("postTareBaselineGraceS", 0.5, 10, __WEBUI_TEXT__("runtime.post_tare_grace"), {
      unit: __WEBUI_TEXT__("runtime.s"),
    }) ||
    R.rangeCheck("minimumCupWeightG", 1, 500, __WEBUI_TEXT__("runtime.min_cup_weight"), {
      unit: __WEBUI_TEXT__("runtime.g"),
    }) ||
    R.rangeCheck("cupRemovedWeightG", -50, -0.1, __WEBUI_TEXT__("runtime.cup_removed"), {
      unit: __WEBUI_TEXT__("runtime.g"),
    }) ||
    R.rangeCheck("retareStabilitySamples", 2, 10, __WEBUI_TEXT__("runtime.retare_samples"), {
      int: 1,
    }) ||
    R.rangeCheck("retareStabilityToleranceG", 0.1, 20, __WEBUI_TEXT__("runtime.retare_tolerance"), {
      unit: __WEBUI_TEXT__("runtime.g"),
    }) ||
    R.rangeCheck("retareStabilityMaxGapS", 0.1, 5, __WEBUI_TEXT__("runtime.retare_sample_gap"), {
      unit: __WEBUI_TEXT__("runtime.s"),
    }) ||
    R.rangeCheck(
      "retareStabilityMinDurationS",
      0,
      2,
      __WEBUI_TEXT__("runtime.retare_min_stable_time"),
      { unit: __WEBUI_TEXT__("runtime.s") },
    ) ||
    R.rangeCheck(
      "scaleTimerStopExtraDelayMs",
      0,
      1000,
      __WEBUI_TEXT__("runtime.scale_timer_stop_extra_delay"),
      { int: 1, unit: __WEBUI_TEXT__("runtime.ms") },
    ) ||
    R.rangeCheck("dripDelayS", 0, 10, __WEBUI_TEXT__("runtime.drip_delay"), {
      unit: __WEBUI_TEXT__("runtime.s"),
    }) ||
    R.rangeCheck("lastShotCooldownMin", 5, 240, __WEBUI_TEXT__("runtime.last_shot_cooldown"), {
      int: 1,
      unit: __WEBUI_TEXT__("runtime.min"),
    });
  if (e) return e;
  const rg = R.number("rinseGestureS"),
    rd = R.number("rinseDurationS"),
    rw = R.number("retareWindowS"),
    wall = R.formNumber("operationalWallS"),
    samples = R.number("retareStabilitySamples"),
    gap = R.number("retareStabilityMaxGapS"),
    minStab = R.number("retareStabilityMinDurationS");
  if (Number.isFinite(wall)) {
    if (!m) {
      if (!(rg < wall))
        return {
          id: "rinseGestureS",
          msg:
            __WEBUI_TEXT__("runtime.rinse_gesture_must_be_machine_circuit_limit") + wall + " s).",
        };
      if (rd > wall)
        return {
          id: "rinseDurationS",
          msg:
            __WEBUI_TEXT__("runtime.rinse_duration_must_be_machine_circuit_limit") + wall + " s).",
        };
    }
    if (rw > wall)
      return {
        id: "retareWindowS",
        msg: __WEBUI_TEXT__("runtime.retare_window_must_be_machine_circuit_limit") + wall + " s).",
      };
  }
  if (minStab > rw)
    return {
      id: "retareStabilityMinDurationS",
      msg: __WEBUI_TEXT__("runtime.stable_time_must_fit_the_retare_window"),
    };
  if (minStab > 0 && minStab > samples * gap)
    return {
      id: "retareStabilityMinDurationS",
      msg: __WEBUI_TEXT__("runtime.stable_time_must_fit_samples_sample_gap"),
    };
  if ($("canTareStartTimer").checked && !$("autoTare").checked)
    return {
      id: "canTareStartTimer",
      msg: __WEBUI_TEXT__("runtime.bookoo_combination_requires_shot_start_tare"),
    };
  if (!m) {
    const iv = +$("paddleReturnReminder").value || 0,
      lim = $("paddleReturnReminderMaxDurationMin")
        ? R.number("paddleReturnReminderMaxDurationMin")
        : 0;
    if (lim * 60 < iv)
      return {
        id: "paddleReturnReminderMaxDurationMin",
        msg: __WEBUI_TEXT__("runtime.paddle_limit_must_be_reminder_interval") + iv + " s).",
      };
  }
  return null;
}
function machinePayload() {
  const reminder = $("paddleReturnReminder");
  return {
    rinseEnabled: $("rinseEnabled").checked,
    rinseGestureMs: R.sToMs("rinseGestureS"),
    rinseDurationMs: R.sToMs("rinseDurationS"),
    retareWindowMs: R.sToMs("retareWindowS"),
    minimumCupWeightG: R.number("minimumCupWeightG"),
    cupRemovedWeightG: R.number("cupRemovedWeightG"),
    retareStabilitySamples: R.number("retareStabilitySamples"),
    retareStabilityToleranceG: R.number("retareStabilityToleranceG"),
    retareStabilityMaxGapMs: R.sToMs("retareStabilityMaxGapS"),
    retareStabilityMinDurationMs: R.sToMs("retareStabilityMinDurationS"),
    autoTare: $("autoTare").checked,
    autoTareOutsideBrew: $("autoTareOutsideBrew").checked,
    retareAccessoryOutsideBrew: $("retareAccessoryOutsideBrew").checked,
    postTareBaselineGraceMs: R.sToMs("postTareBaselineGraceS"),
    scaleMacCacheMode: ["first", "prefer", "only"].includes($("scalePreference")?.value)
      ? $("scalePreference").value
      : "only",
    paddleMode: $("paddleMode")
      ? ["auto", "natural", "original"].includes($("paddleMode").value)
        ? $("paddleMode").value
        : "natural"
      : undefined,
    stopPulseMs: $("stopPulseMs") ? R.number("stopPulseMs") : undefined,
    maxSinglePressMs: $("maxSinglePressMs") ? R.number("maxSinglePressMs") : undefined,
    momentaryStartEdge: $("momentaryStartEdge")
      ? ["press", "release"].includes($("momentaryStartEdge").value)
        ? $("momentaryStartEdge").value
        : "press"
      : undefined,
    reedConfirmTimeoutMs: $("reedConfirmTimeoutS") ? R.sToMs("reedConfirmTimeoutS") : undefined,
    assumeIdleWhenScaleConnects: $("assumeIdleWhenScaleConnects")?.checked ?? true,
    shotReactTimeoutS: $("shotReactTimeoutS") ? R.number("shotReactTimeoutS") || 0 : undefined,
    canTareStartTimer: $("canTareStartTimer").checked,
    bookooMuteOnBuzzerOnly: $("bookooMuteOnBuzzerOnly").checked,
    bookooConnectBeepLevel: Number($("bookooConnectBeepLevel").value),
    scaleTimerStopExtraDelayMs: R.number("scaleTimerStopExtraDelayMs"),
    dripDelayMs: R.sToMs("dripDelayS"),
    soundAlertsEnabled: $("soundAlertsEnabled").checked,
    firstDropBeep: $("firstDropBeep").checked,
    scaleConnectedLed: $("scaleConnectedLed").checked,
    paddleReturnReminderBeep: reminder ? !!+reminder.value : undefined,
    buzzerScaleLostBeep: $("buzzerScaleLostBeep").checked,
    buzzerAutoToManualGuardEndBeep: $("buzzerAutoToManualGuardEndBeep").checked,
    buzzerManualNoScaleBeep: $("buzzerManualNoScaleBeep").checked,
    buzzerScaleConnectedBeep: $("buzzerScaleConnectedBeep").checked,
    buzzerExtendedPulseRate: R.extRate($("buzzerExtendedPulseRate").value),
    buzzerSlowExtendedPulseRate: R.extRate($("buzzerSlowExtendedPulseRate").value),
    alertOutputChannel: ["scale_only", "buzzer_only", "scale_priority"].includes(
      $("alertOutputChannel").value,
    )
      ? $("alertOutputChannel").value
      : "scale_priority",
    autoRetare: $("autoRetare").checked,
    paddleReturnReminderIntervalMs: reminder ? (+reminder.value || 30) * 1000 : undefined,
    paddleReturnReminderMaxDurationMs: Math.round(
      R.number("paddleReturnReminderMaxDurationMin") * 60000,
    ),
    noScaleBbwMode: $("noScaleBbwMode").value,
    noScaleAllowRinseWhileArmed: $("noScaleAllowRinseWhileArmed").checked,
    lastShotCooldownMs: Math.round(R.number("lastShotCooldownMin") * 6e4),
    ...Object.fromEntries(
      [
        "cupProtectionEnabled",
        "stopIfCupRemoved",
        "requireCupToStart",
        "avoidAccidentalTouchEnabled",
        "touchStopFallbackEnabled",
        "autoToManualGuardEnabled",
      ].map((k) => [k, $(k).checked]),
    ),
  };
}
function addBullseyePayload(p) {
  p.bullseyeMelodyEnabled = $("bullseyeMelodyEnabled").checked;
  p.bullseyeRtttl = $("bullseyeRtttl").value;
  return p;
}
function validateBullseyeClient() {
  const text = $("bullseyeRtttl").value;
  if (text.length > 500)
    return {
      id: "bullseyeRtttl",
      msg: __WEBUI_TEXT__("runtime.rtttl_must_be_at_most_500_characters"),
    };
  if ($("bullseyeMelodyEnabled").checked && !text.trim())
    return {
      id: "bullseyeRtttl",
      msg: __WEBUI_TEXT__("runtime.paste_an_rtttl_tune_before_enabling_bullseye"),
    };
  return null;
}
async function saveMachineConfig() {
  const fail = __WEBUI_TEXT__("runtime.could_not_save_shared_settings");
  try {
    await R.ensureSettingsHydrated();
  } catch (e) {
    R.message(R.formatCommandError(fail, e), "error");
    return;
  }
  const err = validateMachineClient() || validateBullseyeClient();
  if (err) {
    R.showFieldError(err.id, err.msg);
    return;
  }
  R.clearFieldErrors();
  const snapshot = R.snapshotControls(R.settingsSectionEls("config")),
    sel = $("preferredScaleSelect"),
    payload = R.withBaseRev(addBullseyePayload(machinePayload()));
  if (sel && sel.dataset.pending === "1" && sel.value !== (sel.dataset.applied || ""))
    payload.preferredScaleMac = sel.value || "";
  try {
    await R.command(
      "/api/v1/config",
      payload,
      1,
      __WEBUI_TEXT__("runtime.shared_settings_saved"),
      fail,
      "saveConfigButton",
    );
    if (
      payload.preferredScaleMac !== undefined &&
      sel.dataset.applied.toUpperCase() !== payload.preferredScaleMac.toUpperCase()
    )
      throw new Error(__WEBUI_TEXT__("runtime.device_did_not_apply_the_change"));
    if (
      JSON.stringify(R.snapshotControls(R.settingsSectionEls("config"))) === JSON.stringify(snapshot)
    ) {
      if (sel) sel.dataset.pending = "0";
      R.uiState.configDirty = false;
      R.setSaveDirty("saveConfigButton", "configDirtyHint", false);
      await R.refreshStatus();
    }
  } catch (e) {
    R.message(R.formatCommandError(fail, e), "error");
    await R.refreshStatus();
  }
}
const MICRA_SWITCHES = [
  "ApplyTemperature",
  "ObserveState",
  "BoostScaleDetectionWhenOn",
  "ReduceScaleScanningWhenOff",
  "RecognizeWake",
  "PowerOnWithScale",
  "ShutdownWithScale",
  "ScaleOffWithMachine",
];
function micraOptionField(id) {
  return id[0].toLowerCase() + id.slice(1) + (id === "RecognizeWake" ? "Gesture" : "");
}
// Shots and scale activity cancel cloud requests routinely: not an error.
function micraErrorText(error) {
  if (error === "none" || error === "canceled") return "";
  return (
    {
      invalid_auth: __WEBUI_TEXT__("runtime.micra_error_auth"),
      no_machines: __WEBUI_TEXT__("runtime.micra_error_no_machines"),
      clock_unsynchronized: __WEBUI_TEXT__("runtime.micra_error_clock"),
    }[error] || __WEBUI_TEXT__("runtime.micra_error_cloud")
  );
}
function renderMicraMachines(machines, canEdit) {
  const list = $("lineaMicraMachines"),
    key = machines.map((x) => x.serial).join();
  if (list.dataset.key !== key) {
    list.dataset.key = key;
    list.replaceChildren(
      ...machines.map((x) => {
        const label = document.createElement("label");
        label.className = "setupChoice";
        label.innerHTML = '<input type="radio" name="lineaMicraMachine"><span><b></b><small></small></span>';
        label.firstChild.value = x.serial;
        label.querySelector("b").textContent = x.name || x.serial;
        label.querySelector("small").textContent = x.serial;
        return label;
      }),
    );
  }
  list.querySelectorAll("input").forEach((r) => (r.disabled = !canEdit));
  $("lineaMicraSelectButton").disabled = !canEdit || !list.querySelector("input:checked");
}
function applyLineaMicraStatus(s) {
  const m = __LINEA_MICRA__ && s.lineaMicra;
  if (!m) return;
  document.documentElement.classList.toggle(
    "micraTemperatureEnabled",
    m.applyTemperature && m.accountConfigured,
  );
  const machines = m.machines || [],
    canEdit = R.controlsMutable,
    connected = m.accountConfigured,
    linking = R.uiState.micraSignIn === "linking" && !connected,
    busy = linking || (!connected && ["queued", "authenticating", "listing"].includes(m.phase)),
    choosing = !connected && !busy && machines.length > 0,
    offline = !m.staConnected || m.apActive,
    error = micraErrorText(m.error);
  if (!R.uiState.micraDirty) {
    for (const id of MICRA_SWITCHES) $("lineaMicra" + id).checked = !!m[micraOptionField(id)];
    $("lineaMicraShutdownGrace").value = String(m.shutdownGraceSeconds || 0);
  }
  // A sign-in started here saves a lone machine with the options above.
  if (connected) R.uiState.micraSignIn = null;
  else if (R.uiState.micraSignIn === "pending" && !busy && (machines.length || error)) {
    R.uiState.micraSignIn = machines.length === 1 ? "linking" : null;
    if (machines.length === 1) selectLineaMicra(machines[0].serial);
    else if (error) $("lineaMicraPassword").focus();
  }
  $("lineaMicraSignIn").hidden = connected || choosing;
  $("lineaMicraChoose").hidden = !choosing;
  $("lineaMicraConnected").hidden = !connected;
  $("lineaMicraWifiNote").classList.toggle("hidden", !offline);
  $("lineaMicraConnectButton").disabled = !canEdit || offline;
  $("lineaMicraConnectButton").classList.toggle("busy", busy);
  $("lineaMicraProgress").textContent = linking
    ? __WEBUI_TEXT__("runtime.micra_progress_linking")
    : busy
    ? m.phase === "listing"
      ? __WEBUI_TEXT__("runtime.micra_progress_listing")
      : __WEBUI_TEXT__("runtime.micra_progress_signing_in")
    : error;
  $("lineaMicraProgress").classList.toggle("error", !busy && !!error);
  renderMicraMachines(machines, canEdit);
  $("lineaMicraCancelButton").disabled = !canEdit;
  $("lineaMicraIdentity").innerText = connected
    ? m.selectedName + " · " + m.selectedSerial + "\n" + m.email
    : "";
  $("lineaMicraStatus").textContent = [
    m.observeState
      ? { ON: __WEBUI_TEXT__("runtime.micra_power_on"), OFF: __WEBUI_TEXT__("runtime.micra_power_standby") }[
          m.powerState
        ] || __WEBUI_TEXT__("runtime.micra_power_unknown")
      : "",
    m.targetValid
      ? __WEBUI_TEXT__("runtime.target") + " " + (m.targetDeciC / 10).toFixed(1) + " °C"
      : "",
    m.apActive ? __WEBUI_TEXT__("runtime.ap_mode_paused") : "",
    m.shotPaused ? __WEBUI_TEXT__("runtime.shot_paused") : "",
    error,
  ]
    .filter(Boolean)
    .join(" · ");
  $("lineaMicraDisconnectButton").disabled = !canEdit;
  for (const id of MICRA_SWITCHES) $("lineaMicra" + id).disabled = !canEdit;
  updateMicraDependentControls();
  R.setSaveDirty("lineaMicraSaveButton", "lineaMicraDirtyHint", R.uiState.micraDirty);
}
function markLineaMicraDirty() {
  R.uiState.micraDirty = true;
  $("lineaMicraLinkedNote").hidden = true;
  R.setSaveDirty("lineaMicraSaveButton", "lineaMicraDirtyHint", true);
}
function updateMicraDependentControls() {
  const canEdit = R.controlsMutable,
    deps = $("lineaMicraPowerDeps"),
    observe = $("lineaMicraObserveState").checked,
    on = $("lineaMicraShutdownWithScale").checked;
  deps.classList.toggle("fieldOff", !observe);
  deps.querySelectorAll("input").forEach((i) => (i.disabled = !canEdit || !observe));
  $("lineaMicraShutdownGrace").disabled = !canEdit || !on;
  $("lineaMicraShutdownGraceWrap").classList.toggle("hidden", !on);
}
async function connectLineaMicra() {
  const username = $("lineaMicraUsername").value.trim(),
    password = $("lineaMicraPassword").value;
  if (!username || !$("lineaMicraUsername").validity.valid) {
    R.showFieldError("lineaMicraUsername", __WEBUI_TEXT__("runtime.enter_valid_email"));
    return false;
  }
  if (!password) {
    R.showFieldError("lineaMicraPassword", __WEBUI_TEXT__("runtime.enter_password"));
    return false;
  }
  $("lineaMicraConnectButton").classList.add("busy");
  const ok = await R.command(
    "/api/v1/machine/linea-micra",
    { action: "connect", username, password },
    false,
    __WEBUI_TEXT__("runtime.micra_connection_queued"),
    __WEBUI_TEXT__("runtime.micra_action_failed"),
  );
  $("lineaMicraPassword").value = "";
  R.uiState.micraSignIn = ok ? "pending" : null;
  return ok;
}
async function selectLineaMicra(serial) {
  const ok = await saveLineaMicraSettings("select", serial);
  if (ok) $("lineaMicraLinkedNote").hidden = false;
  else R.uiState.micraSignIn = null;
  return ok;
}
async function saveLineaMicraSettings(action = "save", serial = "") {
  const ok = await R.command(
    "/api/v1/machine/linea-micra",
    {
      action,
      ...(action === "select" ? { serial } : {}),
      ...Object.fromEntries(
        MICRA_SWITCHES.map((id) => [micraOptionField(id), $("lineaMicra" + id).checked]),
      ),
      connectionType: "websocket",
      shutdownGraceSeconds: Number($("lineaMicraShutdownGrace").value) || 0,
    },
    false,
    __WEBUI_TEXT__("runtime.micra_settings_queued"),
    __WEBUI_TEXT__("runtime.could_not_save_micra"),
    action === "save" ? "lineaMicraSaveButton" : undefined,
  );
  if (ok) {
    R.uiState.micraDirty = false;
    R.setSaveDirty("lineaMicraSaveButton", "lineaMicraDirtyHint", false);
  }
  return ok;
}
async function disconnectLineaMicra() {
  const ok = await R.lineaMicraAction("disconnect");
  if (ok) {
    $("lineaMicraUsername").value = "";
    $("lineaMicraPassword").value = "";
    R.uiState.micraDirty = false;
  }
  return ok;
}
function loadBullseyeConfig(c) {
  if (!c || R.uiState.configDirty || !$("bullseyeMelodyEnabled")) return;
  $("bullseyeMelodyEnabled").checked = !!c.bullseyeMelodyEnabled;
  $("bullseyeRtttl").value = typeof c.bullseyeRtttl === "string" ? c.bullseyeRtttl : "";
  R.updateBullseyeControls();
}
function applySettingsStatus(s) {
  R.uiState.bbwReadback = s.config;
  updatePreferredScaleSelect(s);
  const sc = s.scale || {};
  if ($("forgetPairedScale"))
    $("forgetPairedScale").disabled = !(
      R.controlsMutable &&
      (sc.preferredMac || sc.macCachePauseRemainingMs > 0)
    );
  R.loadSettingsConfig(s.config);
  loadBullseyeConfig(s.config);
  applyLineaMicraStatus(s);
  R.ingestPresets(s);
  R.updateBbwControls();
  const sl = s.scaleLoss,
    active = R.presetState.items.find((x) => x.id === R.presetState.activeId);
  if (sl && active)
    $("scaleLossProjection").textContent = R.sub(__WEBUI_TEXT__("runtime.scale_loss_projection"), [
      active.name,
      +sl.projectedS.toFixed(1),
      {
        history: __WEBUI_TEXT__("runtime.scale_loss_from_history"),
        window: __WEBUI_TEXT__("runtime.scale_loss_from_window"),
        fixed: __WEBUI_TEXT__("runtime.scale_loss_from_fixed"),
      }[sl.source],
    ]);
  if (R.configLoaded) {
    if (!R.uiState.configDirty) R.uiState.configBaseline = R.snapshotControls(R.settingsSectionEls("config"));
    if (!R.uiState.brewDirty) R.uiState.brewBaseline = R.snapshotControls(R.settingsSectionEls("brew"));
    if (!R.uiState.micraDirty) R.uiState.micraBaseline = R.snapshotControls(R.settingsSectionEls("micra"));
  }
  refreshBrewWarnings();
}
async function forgetPairedScale() {
  if (!confirm(__WEBUI_TEXT__("runtime.clear_the_preferred_scale_discovery_pauses_for"))) return;
  await R.command("/api/v1/scale/preferred/clear", {});
}
function selectPreferredScale() {
  const sel = $("preferredScaleSelect");
  if (!sel || R.uiState.preferredScaleSelectSyncing) return;
  const mac = sel.value || "";
  sel.dataset.pending = "1";
  markConfigDirty();
  const opt = sel.selectedOptions && sel.selectedOptions[0];
  R.updateScaleRenameUi(
    "preferredScaleRenameWrap",
    mac,
    opt && opt.dataset ? String(opt.dataset.name || "") : "",
  );
}
let ready = false;
export function applyStatus(s) {
  applySettingsStatus(s);
  updateSettingsOverrides(s);
  updateCupMinGuidance(s);
  const reminder = $("paddleReturnReminder");
  if (reminder && !R.uiState.configDirty && !R.uiState.brewDirty) {
    reminder.value = s.config.paddleReturnReminderBeep
      ? s.config.paddleReturnReminderIntervalMs / 1000
      : 0;
    if (!reminder.value) {
      reminder.value = 30;
      markConfigDirty();
    }
  }
  const link = document.querySelector('[data-route="/diagnostic"]');
  if (link && typeof s.diagnosticPageVisible === "boolean")
    link.classList.toggle("hidden", !s.diagnosticPageVisible);
  // Deep link (Home's Micra hint): open the group once it is laid out.
  const group = location.hash && document.getElementById(location.hash.slice(1));
  if (group?.offsetParent) {
    history.replaceState(null, "", location.pathname);
    group.open = true;
    group.scrollIntoView({ block: "start" });
  }
}

// Settings shows saved values; a note flags the ones a header-panel override
// currently hides (mask bits follow the firmware QuickSettingField order).
const QUICK_FIELDS = [
  "brewByWeight",
  "noScaleBbwMode",
  "autoToManualGuardEnabled",
  "slowExtractionGuardEnabled",
  "fastExtractionGuardEnabled",
  "avoidAccidentalTouchEnabled",
  "cupProtectionEnabled",
];
let overrideStatus = {};
function updateSettingsOverrides(s = overrideStatus) {
  overrideStatus = s;
  const mask = s.quickRevert?.overrides | 0,
    activePreset = (R.presetState.selectedId || R.presetState.activeId) === R.presetState.activeId;
  QUICK_FIELDS.forEach((k, bit) => {
    const label = $(k)?.closest("label");
    if (!label) return;
    let note = label.nextElementSibling?.classList.contains("qsOverride") ? label.nextElementSibling : null;
    const presetField = bit === 0 || bit === 3 || bit === 4;
    if (!(mask & (1 << bit)) || (presetField && !activePreset)) return note?.remove();
    if (!note) {
      note = document.createElement("p");
      note.className = "fieldWarning qsOverride";
      note.innerHTML = `${__WEBUI_TEXT__("settings.quick_override_note")} <button type="button" class="qsReset" data-saved="${k}">${__WEBUI_TEXT__("settings.quick_override_use_saved")}</button>`;
      label.after(note);
    }
    note.querySelector("button").disabled = !R.controlsMutable || !!s.cycle?.active;
  });
}
// The control shows the saved value, so sending it drops that override.
document.addEventListener("click", async (e) => {
  const button = e.target.closest?.(".qsOverride button[data-saved]");
  if (!button) return;
  const input = $(button.dataset.saved);
  button.disabled = true;
  try {
    await R.api("/api/v1/integration/quick-settings", {
      method: "PUT",
      body: R.body({
        baseRevision: overrideStatus.config?.revision,
        [input.id]: input.type === "checkbox" ? input.checked : input.value,
      }),
    });
  } catch (err) {
    R.message(err.message, "error");
    button.disabled = false;
  }
});

// Setup help: something rests on the platform but reads below the configured
// cup minimum, so cup detection and late tare will not see it as a cup.
function updateCupMinGuidance(s) {
  const hint = $("cupMinGuidance");
  if (!hint) return;
  const observed = s.cupSetup && typeof s.cupSetup.observedG === "number" ? s.cupSetup.observedG : null,
    min = R.number("minimumCupWeightG");
  const show =
    observed !== null && Number.isFinite(min) && observed > 0.5 && observed < min;
  hint.hidden = !show;
  if (show)
    hint.textContent = __WEBUI_TEXT__("settings.cup_min_guidance")
      .replaceAll("{x}", observed.toFixed(1))
      .replace("{n}", min.toFixed(1));
}
export function init() {
  if (ready) return;
  ready = true;
  R.registerViewStatus("settings", applyStatus);
  $("presetCards")?.addEventListener("click", () => setTimeout(updateSettingsOverrides));
  $("saveConfigButton").onclick = saveMachineConfig;
  $("revertConfigButton").onclick = revertMachineConfig;
  if ($("presetNewBtn"))
    $("presetNewBtn").onclick = () => {
      if (
        R.uiState.brewDirty &&
        !confirm(__WEBUI_TEXT__("settings.you_have_unsaved_preset_changes_discard_them"))
      )
        return;
      R.invalidateSettingsHydration();
      R.command("/api/v1/presets", { action: "new" });
    };
  if ($("presetDupBtn"))
    $("presetDupBtn").onclick = () => {
      if (
        R.uiState.brewDirty &&
        !confirm(__WEBUI_TEXT__("settings.you_have_unsaved_preset_changes_discard_them_2"))
      )
        return;
      R.invalidateSettingsHydration();
      R.command("/api/v1/presets", {
        action: "duplicate",
        id: R.presetState.selectedId || R.presetState.activeId,
      });
    };
  if ($("saveBrewPresetButton")) $("saveBrewPresetButton").onclick = () => R.saveBrewPreset();
  if ($("revertBrewPresetButton")) $("revertBrewPresetButton").onclick = revertBrewPreset;
  if ($("presetResetBtn"))
    $("presetResetBtn").onclick = async () => {
      const p = R.selectedPreset();
      if (!p || !p.isFactory) return;
      if (!(await R.askConfirm(R.sub(__WEBUI_TEXT__("settings.restore_confirm"), [p.name]), __WEBUI_TEXT__("settings.restore"))))
        return;
      if (R.uiState.brewDirty) R.clearBrewDirty();
      R.invalidateSettingsHydration();
      R.command("/api/v1/presets", { action: "restore_factory_values", id: p.id });
    };
  if ($("presetNewCoffeeBtn")) $("presetNewCoffeeBtn").onclick = () => R.startNewCoffee(R.selectedPreset().id);
  if ($("presetDeleteBtn"))
    $("presetDeleteBtn").onclick = async () => {
      const p = R.selectedPreset();
      if (!p || p.isFactory) return;
      if (await R.askConfirm(R.sub(__WEBUI_TEXT__("settings.delete_confirm"), [p.name]), __WEBUI_TEXT__("settings.delete"), true))
        R.command("/api/v1/presets", { action: "delete", id: p.id });
    };
  if ($("scalePreference"))
    $("scalePreference").onchange = () => {
      R.updateScalePreferenceOptions();
      markConfigDirty();
    };
  $("forgetPairedScale").onclick = forgetPairedScale;
  if ($("preferredScaleSelect"))
    $("preferredScaleSelect").onchange = () => {
      selectPreferredScale();
      R.updateScalePreferenceOptions();
    };
  const preferredScaleRename = $("preferredScaleRename");
  if (preferredScaleRename)
    preferredScaleRename.onclick = (e) => {
      e.preventDefault();
      if (preferredScaleRename.getAttribute("aria-disabled") === "true") return;
      R.renameScale();
    };
  if ($("lineaMicraConnectButton")) $("lineaMicraConnectButton").onclick = connectLineaMicra;
  if ($("lineaMicraSelectButton"))
    $("lineaMicraSelectButton").onclick = () =>
      selectLineaMicra($("lineaMicraMachines").querySelector("input:checked")?.value);
  if ($("lineaMicraMachines"))
    $("lineaMicraMachines").onchange = () => ($("lineaMicraSelectButton").disabled = false);
  if ($("lineaMicraCancelButton"))
    $("lineaMicraCancelButton").onclick = () => R.lineaMicraAction("disconnect");
  if ($("lineaMicraSaveButton"))
    $("lineaMicraSaveButton").onclick = () => saveLineaMicraSettings();
  if ($("revertLineaMicraButton")) $("revertLineaMicraButton").onclick = revertLineaMicra;
  if ($("lineaMicraDisconnectButton"))
    $("lineaMicraDisconnectButton").onclick = () =>
      confirm(__WEBUI_TEXT__("settings.disconnect_account_confirm")) && disconnectLineaMicra();
  for (const id of ["lineaMicraObserveState", "lineaMicraShutdownWithScale"])
    if ($(id)) $(id).onchange = updateMicraDependentControls;
  $("autoTare").onchange = () => {
    R.updateConfigGroups();
    markConfigDirty();
  };
  $("autoRetare").onchange = () => {
    R.updateConfigGroups();
    markConfigDirty();
  };
  $("noScaleBbwMode").onchange = () => {
    R.updateConfigGroups();
    markConfigDirty();
  };
  $("soundAlertsEnabled").onchange = () => {
    R.updateConfigGroups();
    markConfigDirty();
  };
  $("paddleReturnReminder")?.addEventListener("change", R.updateConfigGroups);
  $("alertOutputChannel").onchange = () => {
    R.updateConfigGroups();
    markConfigDirty();
  };
  $("bullseyeMelodyEnabled").onchange = () => {
    R.updateConfigGroups();
    markConfigDirty();
  };
  {
    const tune = $("bullseyeRtttl"),
      hint = tune && tune.nextElementSibling;
    if (hint && !$("bullseyeTestLink")) {
      const link = document.createElement("a");
      link.id = "bullseyeTestLink";
      link.className = "rtttlTest";
      link.href = "#";
      link.textContent = __WEBUI_TEXT__("settings.test_it");
      link.onclick = (e) => {
        e.preventDefault();
        if (!R.controlsMutable) return;
        R.command(
          "/api/v1/bullseye/test",
          { bullseyeRtttl: tune.value },
          false,
          __WEBUI_TEXT__("settings.playing_rtttl_test"),
          __WEBUI_TEXT__("settings.could_not_play_rtttl_test"),
        );
      };
      hint.parentNode.insertBefore(link, hint.nextSibling);
    }
  }
  $("fastExtractionGuardEnabled").onchange = R.updateConfigGroups;
  if ($("avoidAccidentalTouchEnabled")) $("avoidAccidentalTouchEnabled").onchange = R.updateConfigGroups;
  $("slowExtractionGuardEnabled").onchange = R.updateConfigGroups;
  // The device backup's per-preset fixed time lives in Shot limits.
  $("scaleLossFixedLink").onclick = () => {
    const field = $("scaleLossFixedS");
    field.closest("details").open = true;
    field.scrollIntoView({ block: "center" });
    field.focus({ preventScroll: true });
  };
  if ($("cupProtectionEnabled")) $("cupProtectionEnabled").onchange = R.updateConfigGroups;
  $("operationalWallS").addEventListener("input", R.updateConfigGroups);
  document
    .querySelectorAll("#workflowPanel input,#workflowPanel select,#workflowPanel textarea")
    .forEach((el) => {
      if (el.id === "presetRenameInput" || el.closest("#lineaMicraSignIn,#lineaMicraChoose")) return;
      const fn = () => {
        const sec = R.settingsSectionOf(el.id);
        sec === "brew"
          ? markBrewDirty()
          : sec === "micra"
            ? markLineaMicraDirty()
            : markConfigDirty();
        R.updateConfigGroups();
        // Failed-save red marks on brew fields hand over to the live yellow
        // preview, which re-runs over the pending set as a whole.
        clearBrewFieldErrors();
        refreshBrewWarnings();
        R.renderPresetReview();
      };
      el.addEventListener("input", fn);
      el.addEventListener("change", fn);
    });
  const resetBbw = (full) => {
    if ($(full ? "resetEwmaButton" : "resetCalibrationButton").disabled) return;
    if (
      confirm(
        __WEBUI_TEXT__("settings.reset_2") +
          __WEBUI_TEXT__("settings.drip_learning") +
          __WEBUI_TEXT__("settings.offset_to") +
          R.number("weightOffsetBaselineG").toFixed(2) +
          __WEBUI_TEXT__("settings.g") +
          (full
            ? __WEBUI_TEXT__("settings.and_to") + R.number("bbwAlphaBaseline").toFixed(2)
            : "") +
          __WEBUI_TEXT__("settings.symbol_6"),
      )
    )
      R.command("/api/v1/calibration/reset", { fullEwma: full, baseRevision: R.formRev });
  };
  $("resetCalibrationButton").onclick = () => resetBbw(false);
  $("resetEwmaButton").onclick = () => resetBbw(true);
}
