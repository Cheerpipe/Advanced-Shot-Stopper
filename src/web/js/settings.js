"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
let ready = false;
export function applyStatus(s) {
  R.applySettingsStatus(s);
  updateCupMinGuidance(s);
  const reminder = $("paddleReturnReminder");
  if (reminder && !R.configDirty && !R.brewDirty) {
    reminder.value = s.config.paddleReturnReminderBeep
      ? s.config.paddleReturnReminderIntervalMs / 1000
      : 0;
    if (!reminder.value) {
      reminder.value = 30;
      R.markConfigDirty();
    }
  }
  const link = document.querySelector('[data-route="/diagnostic"]');
  if (link && typeof s.diagnosticPageVisible === "boolean")
    link.classList.toggle("hidden", !s.diagnosticPageVisible);
}

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
  $("saveConfigButton").onclick = R.saveMachineConfig;
  $("revertConfigButton").onclick = R.revertMachineConfig;
  if ($("presetNewBtn"))
    $("presetNewBtn").onclick = () => {
      if (
        R.brewDirty &&
        !confirm(__WEBUI_TEXT__("settings.you_have_unsaved_preset_changes_discard_them"))
      )
        return;
      R.invalidateSettingsHydration();
      R.command("/api/v1/presets", { action: "new" });
    };
  if ($("presetDupBtn"))
    $("presetDupBtn").onclick = () => {
      if (
        R.brewDirty &&
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
  if ($("revertBrewPresetButton")) $("revertBrewPresetButton").onclick = R.revertBrewPreset;
  if ($("presetResetBtn"))
    $("presetResetBtn").onclick = () => {
      const p = R.selectedPreset();
      if (!p || !p.isFactory) return;
      if (!confirm(__WEBUI_TEXT__("settings.reset_this_factory_preset_to_its_default"))) return;
      if (R.brewDirty) R.clearBrewDirty();
      R.invalidateSettingsHydration();
      R.command("/api/v1/presets", { action: "restore_factory_values", id: p.id });
    };
  if ($("presetDeleteBtn"))
    $("presetDeleteBtn").onclick = () => {
      const p = R.selectedPreset();
      if (!p || p.isFactory) return;
      if (confirm(__WEBUI_TEXT__("settings.delete_this_preset")))
        R.command("/api/v1/presets", { action: "delete", id: p.id });
    };
  if ($("scalePreference"))
    $("scalePreference").onchange = () => {
      R.updateScalePreferenceOptions();
      R.markConfigDirty();
    };
  $("forgetPairedScale").onclick = R.forgetPairedScale;
  if ($("preferredScaleSelect"))
    $("preferredScaleSelect").onchange = () => {
      R.selectPreferredScale();
      R.updateScalePreferenceOptions();
    };
  const preferredScaleRename = $("preferredScaleRename");
  if (preferredScaleRename)
    preferredScaleRename.onclick = (e) => {
      e.preventDefault();
      if (preferredScaleRename.getAttribute("aria-disabled") === "true") return;
      R.renameScale();
    };
  if ($("lineaMicraConnectButton")) $("lineaMicraConnectButton").onclick = R.connectLineaMicra;
  if ($("lineaMicraSelectButton")) $("lineaMicraSelectButton").onclick = R.selectLineaMicra;
  if ($("lineaMicraSaveButton"))
    $("lineaMicraSaveButton").onclick = () => R.saveLineaMicraSettings();
  if ($("revertLineaMicraButton")) $("revertLineaMicraButton").onclick = R.revertLineaMicra;
  if ($("lineaMicraDisconnectButton"))
    $("lineaMicraDisconnectButton").onclick = () =>
      confirm(__WEBUI_TEXT__("settings.disconnect_account_confirm")) && R.disconnectLineaMicra();
  if ($("lineaMicraShutdownWithScale"))
    $("lineaMicraShutdownWithScale").onchange = () => R.updateMicraShutdownControls();
  $("autoTare").onchange = () => {
    R.updateConfigGroups();
    R.markConfigDirty();
  };
  $("autoRetare").onchange = () => {
    R.updateConfigGroups();
    R.markConfigDirty();
  };
  $("noScaleBbwMode").onchange = () => {
    R.updateConfigGroups();
    R.markConfigDirty();
  };
  $("soundAlertsEnabled").onchange = () => {
    R.updateConfigGroups();
    R.markConfigDirty();
  };
  $("paddleReturnReminder")?.addEventListener("change", R.updateConfigGroups);
  $("alertOutputChannel").onchange = () => {
    R.updateConfigGroups();
    R.markConfigDirty();
  };
  $("bullseyeMelodyEnabled").onchange = () => {
    R.updateConfigGroups();
    R.markConfigDirty();
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
  $("fastExtractionGuardEnabled").onchange = () => {
    R.updateConfigGroups();
    R.syncHomeGuardSwitchesFromSettings();
  };
  if ($("avoidAccidentalTouchEnabled"))
    $("avoidAccidentalTouchEnabled").onchange = () => {
      R.updateConfigGroups();
      R.syncHomeGuardSwitchesFromSettings();
    };
  $("slowExtractionGuardEnabled").onchange = () => {
    R.updateConfigGroups();
    R.syncHomeGuardSwitchesFromSettings();
  };
  $("autoToManualGuardLimitMode").onchange = () => {
    R.updateConfigGroups();
  };
  $("autoToManualGuardEnabled").onchange = () => {
    R.syncHomeGuardSwitchesFromSettings();
  };
  if ($("cupProtectionEnabled"))
    $("cupProtectionEnabled").onchange = () => {
      R.updateConfigGroups();
      R.syncHomeGuardSwitchesFromSettings();
    };
  $("operationalWallS").addEventListener("input", R.updateConfigGroups);
  document
    .querySelectorAll("#workflowPanel input,#workflowPanel select,#workflowPanel textarea")
    .forEach((el) => {
      if (el.id === "presetRenameInput") return;
      const fn = () => {
        const sec = R.settingsSectionOf(el.id);
        sec === "brew"
          ? R.markBrewDirty()
          : sec === "micra"
            ? R.markLineaMicraDirty()
            : R.markConfigDirty();
        R.updateConfigGroups();
        // Failed-save red marks on brew fields hand over to the live yellow
        // preview, which re-runs over the pending set as a whole.
        R.clearBrewFieldErrors();
        R.refreshBrewWarnings();
      };
      el.addEventListener("input", fn);
      el.addEventListener("change", fn);
    });
  const resetBbw = (full) => {
    if ($(full ? "resetEwmaButton" : "resetCalibrationButton").disabled) return;
    if (
      confirm(
        __WEBUI_TEXT__("settings.reset_2") +
          __WEBUI_TEXT__("settings.ewma") +
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
  $("resetGuardSamplesButton").onclick = () => {
    if (R.brewDirty) {
      R.message(__WEBUI_TEXT__("settings.save_the_preset_first"), "warn");
      return;
    }
    const s = R.number("autoToManualGuardBaselineS");
    if (!Number.isFinite(s)) {
      R.message(__WEBUI_TEXT__("settings.set_a_valid_baseline"), "warn");
      return;
    }
    if (
      confirm(
        __WEBUI_TEXT__("settings.reset_a_to_m_samples_to_5") + s + __WEBUI_TEXT__("settings.s"),
      )
    )
      R.command("/api/v1/calibration/reset-guard-samples");
  };
}
