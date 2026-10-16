"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
async function clearActivationHistory() {
  if (!confirm(__WEBUI_TEXT__("runtime.clear_all_recorded_activation_history_this"))) return;
  return R.withCommandGate(async () => {
    try {
      await R.api("/api/v1/history/clear", {
        method: "POST",
        body: R.body({ confirm: "CLEAR_HISTORY" }),
      });
      R.uiState.historyData = { bootId: R.uiState.historyData.bootId || 0, total: 0, hasMore: false, records: [] };
      R.renderHistory();
      R.message(__WEBUI_TEXT__("runtime.activation_history_cleared"), "ok");
    } catch (e) {
      R.message(R.formatCommandError(__WEBUI_TEXT__("runtime.could_not_clear_history"), e), "error");
    }
  });
}
// CSV export rides the standing subscription's one-shot fetch windows, like
// the Stats export: one window at a time, superseded (resolved empty) whenever
// a re-subscribe claims the session fetch slot.


function fetchHistoryWindow(offset) {
  return new Promise((resolve) => {
    const done = (page) => {
      clearTimeout(timer);
      R.uiState.historyExportWindow = null;
      resolve(page);
    };
    const timer = setTimeout(() => done(null), 12e3);
    R.uiState.historyExportWindow = { request: ++R.uiState.historyNextRequest, resolve: done };
    R.sendUiOperation({
      op: "history",
      on: true,
      fetch: true,
      request: R.uiState.historyExportWindow.request,
      offset,
    });
  });
}
async function exportActivationHistory() {
  if (R.uiState.historyExportWindow) return;
  try {
    if (!R.shotWs || R.shotWs.readyState !== 1)
      throw new Error(__WEBUI_TEXT__("runtime.invalid_status"));
    const rows = [
      ["id", "local_time", "ended_at_unix_sec", "has_wall_time", "duration_s", "type"],
    ];
    let offset = 0,
      total = Infinity,
      hasMore = true;
    while (hasMore && offset < total && rows.length - 1 < R.HISTORY_EXPORT_LIMIT) {
      const page = await fetchHistoryWindow(offset);
      if (!page) throw new Error(__WEBUI_TEXT__("runtime.invalid_status"));
      total = page.total;
      hasMore = page.hasMore;
      for (const r of page.records)
        rows.push([
          r.id,
          r.hasWallTime ? R.formatWallTimeLocal(r.endedAtLocalSec) : "",
          r.endedAtUnixSec,
          r.hasWallTime ? 1 : 0,
          r.durationS.toFixed(1),
          r.type,
        ]);
      offset += page.records.length;
      if (!page.records.length) break;
    }
    R.downloadCsv("activation-history.csv", rows);
  } catch (e) {
    R.message(R.formatCommandError(__WEBUI_TEXT__("runtime.could_not_export_history"), e), "error");
  }
}
let ready = false;
export function init() {
  if (ready) return;
  ready = true;
  $("clearHistoryButton").onclick = clearActivationHistory;
  $("exportHistoryButton").onclick = exportActivationHistory;
  $("historyFilterButton").onclick = R.toggleHistoryFilter;
  R.syncHistoryFilterButton();
  const h = $("historySort");
  if (h) {
    $("historyDirButton").onclick = R.toggleHistoryDir;
    R.syncHistoryDirButton();
  }
  R.armListSentinel("historySentinel", R.loadMoreHistory);
}
