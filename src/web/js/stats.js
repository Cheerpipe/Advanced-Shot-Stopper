"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
function formatShotTimeCsv(r) {
  if (r.hasWallTime && r.endedAtLocalSec) return R.formatWallTimeLocal(r.endedAtLocalSec);
  return "";
}
function shotFlowCurveGS(shot) {
  const model = R.buildShotSparkModel(shot);
  return model ? model.flowCurve : [];
}
function syncShotSortButtons() {
  const d = $("sortDateButton"),
    r = $("sortRatingButton"),
    desc = R.uiState.shotSortDir === "desc",
    arrow = desc ? __WEBUI_TEXT__("runtime.down") : __WEBUI_TEXT__("runtime.symbol_5"),
    // Each label names the order pressing that button will produce: the active
    // criterion flips the direction, the other one switches keeping it.
    sync = (b, on, name, after) => {
      b.setAttribute("aria-pressed", on);
      b.textContent = on ? name + " " + arrow : name;
      if (!on) {
        b.removeAttribute("aria-label");
        b.removeAttribute("title");
      } else {
        b.setAttribute("aria-label", after);
        b.title = after;
      }
    };
  if (d)
    sync(d, R.uiState.shotSort === "date", __WEBUI_TEXT__("stats.date"),
      desc ? __WEBUI_TEXT__("runtime.oldest_first") : __WEBUI_TEXT__("runtime.newest_first"));
  if (r)
    sync(r, R.uiState.shotSort === "rating", __WEBUI_TEXT__("stats.rating"),
      desc ? __WEBUI_TEXT__("runtime.lowest_rating") : __WEBUI_TEXT__("runtime.highest_rating"));
}
function setShotSort(field) {
  if (field !== "date" && field !== "rating") return;
  if (R.uiState.shotSort === field) R.uiState.shotSortDir = R.uiState.shotSortDir === "desc" ? "asc" : "desc";
  else R.uiState.shotSort = field;
  syncShotSortButtons();
  R.statsSendSubscribe();
}
const loadMoreShots = () => {
  if (R.uiState.shotHistory.hasMore && R.shotStatsViewActive() && !R.uiState.statsFetchMark && !statsExportInFlight) {
    R.uiState.statsFetchMark = { request: ++R.uiState.statsNextRequest, offset: R.uiState.shotHistory.shots.length };
    R.sendUiOperation({
      op: "stats",
      on: true,
      fetch: true,
      request: R.uiState.statsFetchMark.request,
      offset: R.uiState.statsFetchMark.offset,
      limit: R.SHOTS_PAGE_SIZE,
      sort: R.uiState.shotSort,
      dir: R.uiState.shotSortDir,
    });
  }
};
async function exportShotsCsv() {
  try {
    const list = await statsFrameWindow(0, R.SHOTS_EXPORT_LIMIT, "date", "desc", 90e3);
    if (!list) throw new Error(__WEBUI_TEXT__("runtime.invalid_status"));
    const maxW = list.reduce((m, r) => Math.max(m, r.wCg?.length || 0), 0);
    const rows = [
      [
        ...("id boot_id local_time has_wall_time ended_at_unix tz_off duration_s goal_g " +
          "yield_g error_g error_pct offset_g avg_flow_g_s first_drop_s tare_s fast_side " +
          "cut_raised slow_side cut_lowered stop verdict channeling scale_loss_source " +
          "cut_line_g arrival_s fast_before_s slow_after_s fast_margin_g slow_margin_g " +
          "delay_s wall_s cut_ramp_s shot_type cut_type yield_source rating ended_at_ms " +
          "bbw_algorithm bbw_algorithm_version bbw_alpha bbw_learning_applied preset_id " +
          "scale_name max_flow_g_s curve_truncated curve_break_before curve_held").split(" "),
        ...Array.from({ length: maxW }, (_, i) => [
          "sample_" + (i + 1) + "_time_s",
          "sample_" + (i + 1) + "_weight_g",
          "sample_" + (i + 1) + "_flow_g_s",
        ]).flat(),
      ],
    ];
    for (const r of list) {
      const w = Array.isArray(r.wCg) ? r.wCg : [],
        f = shotFlowCurveGS(r),
        actual = R.shotDisplayActualG(r.actualG, r.wCg);
      const errorG = actual === null || r.goalG == null ? null : actual - r.goalG;
      const errorPct = errorG === null || !r.goalG ? null : (errorG / r.goalG) * 100;
      rows.push([
        r.id,
        r.bootId,
        formatShotTimeCsv(r),
        r.hasWallTime ? "1" : "0",
        r.endedAtUnixSec || "",
        r.hasWallTime ? (r.timezoneOffsetMinutesAtCommit ?? "") : "",
        r.durationS,
        r.goalG.toFixed(1),
        actual ?? "",
        errorG ?? "",
        errorPct ?? "",
        r.offsetG,
        R.shotDisplayFlowGS(r) ?? "",
        r.firstDropS ?? "",
        r.tareS ?? "",
        ...("extractionGuardEnabled extractionExtended slowExtractionGuardEnabled slowExtractionExtended " +
          "stopDetail verdict channeling scaleLossSource cutLineG arrivalS fastBeforeS slowAfterS " +
          "fastMarginG slowMarginG delayS wallS cutRampS")
          .split(" ")
          .map((k) => (typeof r[k] == "boolean" ? +r[k] : (r[k] ?? ""))),
        r.shotType,
        r.cutType,
        r.actualWeightSource ?? "",
        r.rating ?? 0,
        r.endedAtMs,
        r.bbwAlgorithm ?? "",
        r.bbwAlgorithmVersion ?? "",
        r.bbwAlpha == null ? "" : Number(r.bbwAlpha).toFixed(2),
        r.bbwLearningApplied == null ? "" : r.bbwLearningApplied ? "1" : "0",
        r.presetId || "",
        r.scaleName || "",
        R.shotMaxFlowGS(r) ?? "",
        r.wTruncated ? "1" : "0",
        (r.wBreakBefore || []).join(";"),
        (r.wHeld || []).join(";"),
        ...Array.from({ length: maxW }, (_, i) =>
          i < w.length
            ? [
                r.wAtMs?.[i] == null ? "" : r.wAtMs[i] / 1000,
                w[i] / 100,
                f[i] == null ? "" : f[i].toFixed(2),
              ]
            : ["", "", ""],
        ).flat(),
      ]);
    }
    R.downloadCsv("shot-history.csv", rows);
  } catch (e) {
    R.message(
      R.formatCommandError(__WEBUI_TEXT__("runtime.could_not_export_shot_history"), e),
      "error",
    );
  }
}
// One export window at a time: a second request reuses the pending promise
// instead of orphaning the first to its timeout.

let statsExportInFlight = null;
function statsFrameWindow(offset, limit, sort, dir, timeoutMs) {
  if (statsExportInFlight) return statsExportInFlight;
  R.uiState.statsFetchMark = null;
  R.uiState.statsExportRequest = ++R.uiState.statsNextRequest;
  const pending = (statsExportInFlight = new Promise((resolve) => {
    const timer = setTimeout(() => R.uiState.statsExportResolve?.(null), timeoutMs);
    R.uiState.statsExportResolve = (rows) => {
      clearTimeout(timer);
      R.uiState.statsExportResolve = null;
      R.uiState.statsExportRequest = 0;
      resolve(rows);
    };
    R.sendUiOperation({
      op: "stats",
      on: true,
      fetch: true,
      request: R.uiState.statsExportRequest,
      offset,
      limit,
      sort,
      dir,
    });
  }));
  pending.then(() => {
    statsExportInFlight = null;
  });
  return pending;
}
async function clearShotHistory() {
  if (!confirm(__WEBUI_TEXT__("runtime.clear_all_recorded_shot_history_this_cannot"))) return;
  return R.withCommandGate(async () => {
    try {
      await R.api("/api/v1/stats/clear", {
        method: "POST",
        body: R.body({ confirm: "CLEAR_SHOT_LOG" }),
      });
      R.uiState.shotHistory = { bootId: R.uiState.shotHistory.bootId || 0, total: 0, hasMore: false, shots: [] };
      R.uiState.shotStats = {};
      R.renderShots();
      R.message(__WEBUI_TEXT__("runtime.shot_history_cleared"), "ok");
    } catch (e) {
      R.message(
        R.formatCommandError(__WEBUI_TEXT__("runtime.could_not_clear_shot_history"), e),
        "error",
      );
    }
  });
}
let ready = false;
export function init() {
  if (ready) return;
  ready = true;
  $("exportShotsButton").onclick = exportShotsCsv;
  $("clearShotsButton").onclick = clearShotHistory;
  const h = $("shotSort");
  if (h) {
    h.innerHTML =
      '<span class="sortLbl">' + __WEBUI_TEXT__("runtime.sort") + '</span>' +
      '<button id="sortDateButton" type="button" aria-pressed="true">' +
      __WEBUI_TEXT__("stats.date") +
      '</button><button id="sortRatingButton" type="button">' +
      __WEBUI_TEXT__("stats.rating") +
      "</button>";
    $("sortDateButton").onclick = () => setShotSort("date");
    $("sortRatingButton").onclick = () => setShotSort("rating");
    syncShotSortButtons();
  }
  R.armListSentinel("shotLogSentinel", loadMoreShots);
  R.renderStatsDurChart();
}
