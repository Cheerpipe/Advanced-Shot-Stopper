"use strict";
import * as R from "./runtime.js?v=__FW_ASSET_TAG__";
const $ = R.$;
let ready = false;
export function init() {
  if (ready) return;
  ready = true;
  $("clearHistoryButton").onclick = R.clearActivationHistory;
  const h = $("historySort");
  if (h) {
    $("historyDirButton").onclick = R.toggleHistoryDir;
    R.syncHistoryDirButton();
  }
  R.armListSentinel("historySentinel", R.loadMoreHistory);
}
