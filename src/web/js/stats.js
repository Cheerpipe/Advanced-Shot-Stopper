"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
let ready = false;
export function init() {
  if (ready) return;
  ready = true;
  $("exportShotsButton").onclick = R.exportShotsCsv;
  $("clearShotsButton").onclick = R.clearShotHistory;
  const h = $("shotSort");
  if (h) {
    h.innerHTML =
      '<span class="sortLbl">' + __WEBUI_TEXT__("runtime.sort") + '</span>' +
      '<button id="sortDateButton" type="button" aria-pressed="true">' +
      __WEBUI_TEXT__("stats.date") +
      '</button><button id="sortRatingButton" type="button">' +
      __WEBUI_TEXT__("stats.rating") +
      "</button>";
    $("sortDateButton").onclick = () => R.setShotSort("date");
    $("sortRatingButton").onclick = () => R.setShotSort("rating");
    R.syncShotSortButtons();
  }
  R.armListSentinel("shotLogSentinel", R.loadMoreShots);
  R.renderStatsDurChart();
}
