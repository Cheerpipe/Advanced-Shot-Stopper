    !firmware.includes(
        'if (shotCompletionGetsLongBeep(reason)) {\n      // Completion LONG replaces the stop-timer SINGLE so ends are one cue.\n      requestCompletionAlert();\n    } else {\n      emitCircuitCycleAlert(AlertEvent::STOP_TIMER, true);') ||
    !firmware.includes('emitImmediateCommandAlertIfBuzzer(AlertEvent::TARE);\n  markRetareEnded') ||
    !firmware.includes('emitCircuitCycleAlert(AlertEvent::START_TIMER, true);')) {
  throw new Error('Command alerts must fire at circuit/paddle/retare, not after BLE');
}
if (firmware.includes('SCALE_COMPLETION_BEEP_DELAY_MS') ||
    firmware.includes('scheduleScaleCompletionBeep') ||
    firmware.includes('scaleCompletionBeepDueAtMs') ||
    !firmware.includes('void requestCompletionAlert()')) {
  throw new Error('Completion beep must fire at machine circuit open with no emission delay');
}
if (firmware.includes('maybeCaptureScaleStartLag') ||
    firmware.includes('scaleStartLagCaptured') ||
    firmware.includes('scaleTimerStopDelayMsForCycle') ||
    firmware.includes('setAdvertisingPaused(preferBluetooth)') ||
    !firmware.includes('remoteTimerStartSettled') ||
    !firmware.includes(
        'enqueueScaleCommand(command, session.remoteTimerStartSettled)')) {
  throw new Error('Scale timer stop must catch up live, wait for start, and not BLE.advertise() on machine circuit open');
}

(async () => {
const generated = await webUi.generate();
if (!generated.assetTag || !generated.cacheVersion ||
    !generated.html.includes(`v=${generated.cacheVersion}`) ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssetsGzip.h'), 'utf8')
         .includes(`WEB_UI_ASSET_TAG[] = "${generated.assetTag}"`) ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperWebAssetsGzip.h'), 'utf8')
         .includes(`WEB_UI_ETAG[] = ${JSON.stringify('"' + generated.cacheVersion + '"')}`)) {
  throw new Error('Web UI cache-buster must embed FW version + asset content tag');
}
const normalizedEnglish = await webUi.generate({webUiLanguage: 'EN', write: false});
const regionalEnglish = await webUi.generate({webUiLanguage: 'En_en', write: false});
localeAssert.equal(normalizedEnglish.resolvedLanguage, 'en');
localeAssert.equal(regionalEnglish.requestedLanguage, 'en-en');
localeAssert.equal(regionalEnglish.resolvedLanguage, 'en');
localeAssert.equal(normalizedEnglish.assetTag, generated.assetTag);
localeAssert.equal(regionalEnglish.assetTag, generated.assetTag);
localeAssert.deepEqual(normalizedEnglish.gzip, generated.gzip);

const metadataFixture = makeTestLocales((catalogs) => {
  catalogs.en.language = 'English test metadata';
});
try {
  const metadataOnly = await webUi.generate({
    webUiLanguage: 'en', localesDir: metadataFixture.dir, write: false,
  });
  localeAssert.equal(metadataOnly.assetTag, generated.assetTag);
} finally {
  metadataFixture.clean();
}

const changedFixture = makeTestLocales((catalogs) => {
  const key = Object.keys(catalogs.en.strings)[0];
  catalogs.en.strings[key] += ' changed';
});
try {
  const changed = await webUi.generate({
    webUiLanguage: 'en', localesDir: changedFixture.dir, write: false,
  });
  localeAssert.notEqual(changed.assetTag, generated.assetTag);
} finally {
  changedFixture.clean();
}

const generatedHeaderPath = path.join(sketchDir, 'ShotStopperWebAssetsGzip.h');
const generatedHeader = fs.readFileSync(generatedHeaderPath, 'utf8');
for (const forbidden of ['__WEBUI_', '{{webui:', 'schemaVersion', 'shell.advanced']) {
  localeAssert.ok(!generatedHeader.includes(forbidden),
      `Generated firmware assets must not embed localization token ${forbidden}`);
}
await localeAssert.rejects(
    webUi.generate({webUiLanguage: 'es-CL'}), /no catalog for es-cl/);
localeAssert.equal(fs.readFileSync(generatedHeaderPath, 'utf8'), generatedHeader,
    'Rejected locale must not replace the generated header');
const escapedLocaleValue = `quote " apostrophe ' slash \\ newline\nUnicode café`;
const escapedLocaleJs = await webUi.minifyJs(
    `globalThis.__webUiLocaleTest=${webUiLocale.jsLiteral(escapedLocaleValue)}`);
new Function(escapedLocaleJs)();
localeAssert.equal(globalThis.__webUiLocaleTest, escapedLocaleValue);
delete globalThis.__webUiLocaleTest;
localeAssert.doesNotThrow(() => webUi.minifyCss(
    `x{content:${webUiLocale.cssLiteral(escapedLocaleValue)}}`));
const roundTrip = zlib.gunzipSync(generated.gzip).toString('utf8');
if (roundTrip !== generated.html) {
  throw new Error('Generated gzip Web UI does not round-trip to the minified HTML');
}
if (!generated.html.includes('id="view-home"') ||
    !generated.html.includes('id="stopButton"') ||
    !generated.html.includes('id="forcePulseButton"') ||
    !generated.html.includes('Start shot') ||
    generated.html.includes('virtualPaddle') ||
    generated.html.includes('<section id="view-home" class="view" data-view="home"></section>')) {
  throw new Error('Generated shell must embed home partial markup');
}
const jsRoundTrip = zlib.gunzipSync(generated.jsGzip).toString('utf8');
if (jsRoundTrip !== generated.js) {
  throw new Error('Generated gzip Web JS does not round-trip to the minified JS');
}
if (!generated.js.includes('import') || !generated.runtimeJs.includes('export') ||
    !generated.js.includes('/api/v1/control/paddle') ||
    generated.js.includes('/js/home.js') ||
    !generated.secondaryJs.includes('export') ||
    !generated.secondaryJs.includes('views') ||
    !appJsSource.includes('__homeModule') ||
    !appJsSource.includes('/js/secondary.js')) {
  throw new Error('Generated Web UI JS must remain ES modules (shell + runtime + secondary)');
}
const runtimeRoundTrip = zlib.gunzipSync(generated.runtimeGzip).toString('utf8');
if (runtimeRoundTrip !== generated.runtimeJs) {
  throw new Error('Generated gzip runtime JS does not round-trip');
}
const otaImageRoundTrip =
    zlib.gunzipSync(generated.otaImageGzip).toString('utf8');
if (otaImageRoundTrip !== generated.otaImageJs) {
  throw new Error('Generated gzip OTA image module does not round-trip');
}
const makeOtaParser = new Function(
    otaImageJs.replace(/^['"]use strict['"];\s*/m, '')
        .replace(/export\s+/g, '') +
    '; return {otaFileIdentity,imageTagScanner,hash256};');
const otaParser = makeOtaParser();
const otaDigest = otaParser.hash256();
otaDigest.add(new TextEncoder().encode('a'));
otaDigest.add(new TextEncoder().encode('bc'));
if (otaDigest.end() !==
    'ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad') {
  throw new Error('Web UI incremental OTA SHA-256 is incorrect');
}
function browserFile(buffer) {
  return {
    size: buffer.length,
    slice(start, end) {
      const bytes = buffer.subarray(start, end);
      return {arrayBuffer: async () =>
        bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength)};
    },
  };
}
function otaFixture(tagOffset) {
  const tag = Buffer.from(
      'SHOTSTOPPER_FW_TAG_V1|arch=n16r8|ver=1.2.3+abcdef0|' +
      'packed=16909056|END', 'latin1');
  const size = Math.max(tagOffset + tag.length + 64, 4096);
  const image = Buffer.alloc(size, 0x5a);
  image[0] = 0xe9;
  image[12] = 0x09;
  image[13] = 0;
  image[23] = 1;
  image.writeUInt32LE(0xabcd5432, 32);
  image.fill(0, 80, 112);
  image.write('shotstopper', 80, 'latin1');
  tag.copy(image, tagOffset);
  refreshOtaFixtureHash(image);
  return image;
}
function refreshOtaFixtureHash(image) {
  crypto.createHash('sha256').update(image.subarray(0, image.length - 32))
      .digest().copy(image, image.length - 32);
}
for (const tagOffset of [320, 65530, 262143, 262144, 270344]) {
  const fixture = otaFixture(tagOffset);
  const parsed = await otaParser.otaFileIdentity(browserFile(fixture));
  const nodeParsed = imageTag.findImageTag(fixture);
  if (parsed.tagOffset !== tagOffset || parsed.arch !== 'n16r8' ||
      parsed.version !== '1.2.3+abcdef0' || parsed.packed !== 16909056 ||
      !nodeParsed || nodeParsed.arch !== parsed.arch ||
      nodeParsed.ver !== parsed.version || Number(nodeParsed.packed) !== parsed.packed) {
    throw new Error(`Web/Node OTA identity mismatch at offset ${tagOffset}`);
  }
}
for (const badBody of [
  'arch=N16R8|arch=n16r8|ver=1.2.3|packed=1|END',
  'ignored=\0|arch=n16r8|ver=1.2.3|packed=1|END',
]) {
  const bad = Buffer.from('SHOTSTOPPER_FW_TAG_V1|' + badBody, 'latin1');
  const validOffset = 320 + bad.length + 8;
  const fixture = otaFixture(validOffset);
  bad.copy(fixture, 320);
  refreshOtaFixtureHash(fixture);
  const parsed = await otaParser.otaFileIdentity(browserFile(fixture));
  const nodeParsed = imageTag.findImageTag(fixture);
  if (parsed.tagOffset !== validOffset || !nodeParsed ||
      nodeParsed.tagOffset !== validOffset) {
    throw new Error('Web/Node parsers accepted a candidate rejected by C++');
  }
}
{
  const corrupt = otaFixture(270344);
  corrupt[1000] ^= 1;
  let rejected = false;
  try {
    await otaParser.otaFileIdentity(browserFile(corrupt));
  } catch (error) {
    rejected = /truncated or corrupt/.test(String(error && error.message));
  }
  if (!rejected) {
    throw new Error('Web OTA parser must reject a corrupt appended image hash');
  }
}
const secondaryRoundTrip =
    zlib.gunzipSync(generated.secondaryGzip).toString('utf8');
if (secondaryRoundTrip !== generated.secondaryJs) {
  throw new Error('Generated gzip secondary JS does not round-trip');
}
const settingsRoundTrip =
    zlib.gunzipSync(generated.settingsGzip).toString('utf8');
if (settingsRoundTrip !== generated.settingsJs) {
  throw new Error('Generated gzip settings JS does not round-trip');
}
for (const name of webUi.LAZY_PARTIALS) {
  const partialRt = zlib.gunzipSync(generated.partialGzip[name]).toString('utf8');
  if (partialRt !== generated.partials[name]) {
    throw new Error('Generated gzip partial does not round-trip: ' + name);
  }
}
const cssRoundTrip = zlib.gunzipSync(generated.cssGzip).toString('utf8');
if (cssRoundTrip !== generated.css) {
  throw new Error('Generated gzip Web CSS does not round-trip to the minified CSS');
}
if (generated.gzip.length > 4096) {
  throw new Error('Compressed Web UI shell HTML exceeds the 4 KiB gzip budget');
}
// Reallocate another 500 bytes of shell allowance to BBW readback/CSV.
// Complete, human-readable Settings help raises the reviewed combined budget.
if (generated.jsGzip.length > 5044) {
  throw new Error('Compressed Web UI shell JS exceeds the 5044-byte gzip budget');
}
if (generated.cssGzip.length > 6600) {
  throw new Error('Compressed Web CSS exceeds the 6.5 KiB gzip budget');
}
if (generated.runtimeGzip.length > 32200) {
  throw new Error('Compressed Web UI runtime JS exceeds the 32200-byte gzip budget');
}
if (generated.otaImageGzip.length > 3072) {
  throw new Error('Compressed OTA image module exceeds the 3 KiB gzip budget');
}
if (generated.secondaryGzip.length > 5800) {
  throw new Error('Compressed secondary view JS exceeds the 5800-byte gzip budget');
}
if (generated.settingsGzip.length > 4096) {
  throw new Error('Compressed settings view JS exceeds the 4 KiB gzip budget');
}
if (generated.combined > 66000) {
  throw new Error('Combined Web UI gzip exceeds the 66000-byte flash budget');
}
if (!network.includes('#include "ShotStopperWebAssetsGzip.h"') ||
    network.includes('#include "ShotStopperWebAssets.h"')) {
  throw new Error('Firmware must embed the gzip Web UI, not the HTML source string');
}
if (!network.includes('SHOT_STOPPER_WEB_UI_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_UI_GZIP_LEN') ||
    !network.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_JS_GZIP_LEN') ||
    !network.includes('SHOT_STOPPER_WEB_RUNTIME_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_OTA_IMAGE_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_SECONDARY_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_VIEW_SETTINGS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_PARTIAL_SETTINGS_GZIP') ||
    !network.includes('WEB_UI_ETAG') ||
    network.includes('SHOT_STOPPER_WEB_PARTIAL_HOME_GZIP') ||
    network.includes('SHOT_STOPPER_WEB_VIEW_HOME_GZIP') ||
    network.includes('formatWebUiEtag') ||
    network.includes('SHOT_STOPPER_WEB_LOGO_GZIP') ||
    !network.includes('"Content-Encoding"') ||
    !network.includes('"gzip"')) {
  throw new Error('GET /, assets, partials, and view modules must send precompressed gzip bodies without logo');
}
if (network.includes('zlib.h') || network.includes('miniz.h') ||
    /mz_compress|deflateInit|gzipCompress/.test(network)) {
  throw new Error('Firmware must not compress the Web UI at runtime');
}
if (!network.includes('sendCopiedBody(request, SHOT_STOPPER_WEB_UI_GZIP') ||
    !network.includes('serveImmutableGzip') ||
    !network.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !network.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !network.includes('return sendCopiedBody(request, json, length)') ||
    !network.includes('HTTP_DRAM_BOUNCE_BYTES') ||
    !network.includes('g_httpSendBounce') ||
    !network.includes('allocExternal(sizeof(NetworkWorkBuf), AllocationOwner::NETWORK)') ||
    !psram.includes('inline void *allocExternal(size_t bytes,') ||
    !jsonArena.includes('parseJsonDocument') ||
    !jsonArena.includes('AllocationOwner::JSON') ||
    !network.includes(
        'sendCopiedChunk(request, work.jsonItem, strlen(work.jsonItem))')) {
  throw new Error(
      'HTTP bodies must copy through internal RAM before tcp_write; large work buffers live in PSRAM heap');
}
if (!network.includes('If-None-Match')) {
  throw new Error('GET / must honor If-None-Match for cached Web UI revalidation');
}
const rootHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::rootHandler');
const jsHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::jsHandler');
const cssHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::cssHandler');
const runtimeHandlerStart =
    network.indexOf('esp_err_t ShotStopperNetwork::runtimeJsHandler');
const browserIconHandlerStart =
    network.indexOf('esp_err_t ShotStopperNetwork::browserIconHandler');
const notFoundHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::notFoundHandler');
const serveImmutableStart = network.indexOf('static esp_err_t serveImmutableGzip');
if (rootHandlerStart < 0 || jsHandlerStart < 0 || cssHandlerStart < 0 ||
    runtimeHandlerStart < 0 || browserIconHandlerStart < 0 ||
    notFoundHandlerStart < 0 ||
    statusHandlerStart < 0 || serveImmutableStart < 0 ||
    network.includes('logoHandler') ||
    network.includes('loginHandler') ||
    !(rootHandlerStart < jsHandlerStart && jsHandlerStart < cssHandlerStart &&
      cssHandlerStart < runtimeHandlerStart &&
      runtimeHandlerStart < browserIconHandlerStart &&
      browserIconHandlerStart < notFoundHandlerStart &&
      notFoundHandlerStart < statusHandlerStart)) {
  throw new Error('rootHandler/jsHandler/cssHandler/runtime/notFoundHandler order not found');
}
const rootHandler = network.slice(rootHandlerStart, jsHandlerStart);
const serveImmutable = network.slice(serveImmutableStart, rootHandlerStart);
const jsHandler = network.slice(jsHandlerStart, cssHandlerStart);
const cssHandler = network.slice(cssHandlerStart, runtimeHandlerStart);
const browserIconHandler = network.slice(browserIconHandlerStart, notFoundHandlerStart);
const notFoundHandler = network.slice(notFoundHandlerStart, statusHandlerStart);
if (rootHandler.includes('no-store') || !rootHandler.includes('no-cache') ||
    !rootHandler.includes('STATUS_NOT_MODIFIED') ||
    !rootHandler.includes('ifNoneMatchEquals') ||
    !rootHandler.includes('ETag') ||
    !rootHandler.includes("style-src 'self'") ||
    !rootHandler.includes("script-src 'self'") ||
    !rootHandler.includes('"Connection"') ||
    !rootHandler.includes('"close"') ||
    rootHandler.includes("script-src 'unsafe-inline'") ||
    rootHandler.includes('HTTPD_RESP_USE_STRLEN')) {
  throw new Error('GET / must revalidate with ETag/304, CSP script/style self, Connection close, and gzip by length');
}
if (serveImmutable.includes('no-store') ||
    !serveImmutable.includes('max-age=31536000') ||
    !serveImmutable.includes('immutable') ||
    !serveImmutable.includes('STATUS_NOT_MODIFIED') ||
    !serveImmutable.includes('"Connection"') ||
    !serveImmutable.includes('"close"')) {
  throw new Error('Immutable gzip assets must use long-cache ETag/304 and Connection close');
}
if (!jsHandler.includes('SHOT_STOPPER_WEB_JS_GZIP') ||
    !jsHandler.includes('application/javascript')) {
  throw new Error('GET /app.js must serve immutable gzip JS');
}
if (!cssHandler.includes('SHOT_STOPPER_WEB_CSS_GZIP') ||
    !cssHandler.includes('text/css')) {
  throw new Error('GET /app.css must serve immutable gzip CSS');
}
if (network.includes('logoHandler') || network.includes('SHOT_STOPPER_WEB_LOGO')) {
  throw new Error('Firmware must not serve /logo.svg');
}
if (!browserIconHandler.includes('STATUS_NO_CONTENT') ||
    !browserIconHandler.includes('max-age=31536000') ||
    !browserIconHandler.includes('immutable') ||
    browserIconHandler.includes('302 Found') ||
    browserIconHandler.includes('Location')) {
  throw new Error('Safari icon probes must 204 with long cache and must not 302 to /');
}
if (!notFoundHandler.includes('302 Found') ||
    !notFoundHandler.includes('Location') ||
    !notFoundHandler.includes('"/api/"') ||
    !notFoundHandler.includes('STATUS_NOT_FOUND') ||
    notFoundHandler.includes('!= nullptr')) {
  throw new Error('Unknown non-API routes must 302 to /, while unknown /api/* stay JSON 404');
}
if (network.includes('sendJson') &&
    !network.slice(network.indexOf('esp_err_t ShotStopperNetwork::sendJson'),
                   network.indexOf('esp_err_t ShotStopperNetwork::sendError'))
        .includes('no-store')) {
  throw new Error('JSON API responses must remain Cache-Control: no-store');
}
if (!network.includes('requireActiveWebUiClient') ||
    network.includes('heartbeatHandler') ||
    network.includes('/api/v1/heartbeat') ||
    networkHeader.includes('WEB_PADDLE_HEARTBEAT_TIMEOUT_MS') ||
    network.includes('WEB_PADDLE_HEARTBEAT_TIMEOUT_MS') ||
    network.includes('heartbeatStopSent_')) {
  throw new Error(
      'WebUI claim must replace POST /heartbeat; web paddle heartbeat circuit timeout must be gone');
}
const logHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::logHandler');
const shotsHandlerStart = network.indexOf('esp_err_t ShotStopperNetwork::shotsHandler');
const wifiScanStatusStart =
    network.indexOf('esp_err_t ShotStopperNetwork::wifiScanStatusHandler');
if (logHandlerStart < 0 || shotsHandlerStart < 0 || wifiScanStatusStart < 0 ||
    !network.slice(logHandlerStart, shotsHandlerStart).includes('"Connection"') ||
    !network.slice(logHandlerStart, shotsHandlerStart).includes('"close"') ||
    !network.slice(wifiScanStatusStart).includes('"Connection"') ||
    !network.slice(wifiScanStatusStart, wifiScanStatusStart + 800)
         .includes('"close"')) {
  throw new Error('Chunked log and Wi-Fi scan status responses must send Connection: close');
}
if (!js.includes('withPollGate(async()=>{if(scanBusy||!webUiPollingActive())return;scanBusy=true') ||
    !js.includes("withCommandGate(async()=>{try{await api('/api/v1/shots/clear'") ||
    !js.includes("withCommandGate(async()=>{try{await api('/api/v1/shots/delete'") ||
    !js.includes("await api('/api/v1/shots/rate'") ||
    js.includes("withCommandGate(async()=>{try{await api('/api/v1/logout'")) {
  throw new Error('Wi-Fi scan and shot clear/delete must use poll/command gates without login');
}

{
  const start = js.indexOf('function buildRuleChartModel(');
  const end = js.indexOf('let ruleChartSig=');
  if (start < 0 || end < 0 || end <= start) {
    throw new Error('Rule chart model helpers not found for matrix checks');
  }
  const helpers = new Function(
      js.slice(start, end) +
      ';return{buildRuleChartModel:buildRuleChartModel};')();
  const base = {
    brewByWeight: true,
    fastExtractionGuardEnabled: true,
    slowExtractionGuardEnabled: true,
    operationalWallMs: 50000,
    minBbwBrewTimeMs: 28000,
    maxBbwBrewTimeMs: 44000,
    goalWeightG: 36,
    minRecoveryWeightG: 34,
    maxRecoveryWeightG: 42.5,
  };
  const kinds = (segs) => (segs || []).map((s) => s[0]).join(',');
  const off = helpers.buildRuleChartModel({...base, brewByWeight: false});
  if (off.mode !== 'timerOnly' || kinds(off.tSeg) !== 'idle' ||
      kinds(off.wSeg) !== 'idle' || off.fs) {
    throw new Error('Rule chart: BBW off must be timerOnly idle (ignore guard flags)');
  }
  const both = helpers.buildRuleChartModel(base);
  if (both.mode !== 'active' || kinds(both.tSeg) !== 'fast,bbw,slow' ||
      kinds(both.wSeg) !== 'slow,bbw,fast' || !both.fs || both.goal !== 36) {
    throw new Error('Rule chart: BBW+Fast+Slow matrix row failed');
  }
  const fastOnly = helpers.buildRuleChartModel({
    ...base, slowExtractionGuardEnabled: false
  });
  if (kinds(fastOnly.tSeg) !== 'fast,bbw' ||
      kinds(fastOnly.wSeg) !== 'bbw,fast' || !fastOnly.fs) {
    throw new Error('Rule chart: Fast on / Slow off must extend BBW into Slow');
  }
  const slowOnly = helpers.buildRuleChartModel({
    ...base, fastExtractionGuardEnabled: false
  });
  if (kinds(slowOnly.tSeg) !== 'bbw,slow' ||
      kinds(slowOnly.wSeg) !== 'slow,bbw' || slowOnly.fs) {
    throw new Error('Rule chart: Fast off / Slow on must extend BBW into Fast');
  }
  const none = helpers.buildRuleChartModel({
    ...base, fastExtractionGuardEnabled: false,
    slowExtractionGuardEnabled: false
  });
  if (kinds(none.tSeg) !== 'bbw' || kinds(none.wSeg) !== 'bbw' ||
      none.fs || none.goal !== 36) {
    throw new Error('Rule chart: both guards off must be BBW-only with goal');
  }
  const ignoredGuards = helpers.buildRuleChartModel({
    ...base, brewByWeight: false, fastExtractionGuardEnabled: true,
    slowExtractionGuardEnabled: true
  });
  if (ignoredGuards.mode !== 'timerOnly' ||
      kinds(ignoredGuards.tSeg) !== 'idle') {
    throw new Error('Rule chart: guards must be ignored when BBW is off');
  }
}

{
  const start = js.indexOf('function lastCurveWeightG(');
  const end = js.indexOf('function populateTimezoneOptions(');
  if (start < 0 || end < 0 || end <= start) {
    throw new Error('Shot spark helpers not found for matrix checks');
  }
  const helpers = new Function(
      '"use strict";' + js.slice(start, end) +
      ';return{buildShotSparkModel:buildShotSparkModel,shotDisplayFlowGS:shotDisplayFlowGS,fillChartTicks:fillChartTicks};')();
  if (helpers.buildShotSparkModel(null) !== null) {
    throw new Error('Spark model must tolerate a null shot (idle Home panel)');
  }
  const kids = [];
  const tickEl = {
    replaceChildren(){kids.length=0},
    appendChild(n){kids.push(n)},
  };
  const prevDoc = global.document;
  global.document = {
    createElement(){return {className:'',style:{},textContent:''}},
  };
  try {
    helpers.fillChartTicks(tickEl, [[0, '0 g'], [18, '18 g'], [36, '36 g']], 36);
    const spaced = kids.map((n) => parseFloat(n.style.left));
    if (kids.length !== 3 || kids[0].textContent !== '0 g' ||
        kids.some((n) => n.className !== 'ruleTick') ||
        spaced[0] !== 0 || Math.abs(spaced[1] - 50) > 0.01 || spaced[2] !== 100) {
      throw new Error('fillChartTicks must place labels at exact values');
    }
    helpers.fillChartTicks(tickEl, [[0, '0 g'], [36, '36 g'], [18, '18 g']], 36);
    if (kids.map((n) => n.textContent).join('|') !== '0 g|18 g|36 g') {
      throw new Error('fillChartTicks must sort labels by position');
    }
    helpers.fillChartTicks(tickEl, [[0, '0 g'], [35.5, '35.5 g'], [36, '36 g']], 36);
    const close = kids.map((n) => parseFloat(n.style.left));
    if (kids.map((n) => n.textContent).join('|') !== '0 g|35.5 g|36 g' ||
        Math.abs(close[1] - 35.5 / 36 * 100) > 0.01 || close[2] !== 100) {
      throw new Error('fillChartTicks must keep close labels on exact values');
    }
  } finally {
    if (prevDoc === undefined) delete global.document;
    else global.document = prevDoc;
  }
  const fast15 = helpers.buildShotSparkModel({
    wCg: [20, 800, 1600, 2400, 3200, 3600, 4000, 4370],
    wDtS: 2,
    durationS: 15.1,
    firstDropS: 4.5,
    dropS: 4.5,
    dropCg: 50,
    extendedS: 13.3,
    extCg: 3600,
    endS: 15.1,
    endCg: 4370,
    extractionExtended: true,
    goalG: 36,
  });
  if (!fast15 || !fast15.segs.some((s) => s.color === '#d97706' &&
      s.pts[s.pts.length - 1].t >= 15)) {
    throw new Error('Spark 15.1s Fast guard must paint orange through ended');
  }
  if (fast15.pts[0].t !== 4.5 || fast15.pts[0].cg !== 50 ||
      fast15.pts.some((p) => p.t < 4.5) ||
      fast15.segs.some((s) => s.pts.some((p) => p.t < 4.5))) {
    throw new Error('Spark must start at the exact first-drop event');
  }
  const firstDropFallback = helpers.buildShotSparkModel({
    wCg: [0, 20, 90, 150], wDtS: 2, durationS: 6,
    firstDropS: 4.5, dropCg: 40,
  });
  if (!firstDropFallback || firstDropFallback.pts[0].t !== 4.5 ||
      firstDropFallback.pts[0].cg !== 40 ||
      firstDropFallback.pts[firstDropFallback.pts.length - 1].cg !== 150) {
    throw new Error('Spark fallback must preserve exact drop and final grid weights');
  }
  const laterEvent = helpers.buildShotSparkModel({
    wCg: [0, 20, 90, 150], wDtS: 2, durationS: 8,
    firstDropS: 4.5, dropCg: 40, extendedS: 7.3, extCg: 300,
    extractionExtended: true,
  });
  if (!laterEvent || laterEvent.pts[laterEvent.pts.length - 1].t !== 8 ||
      laterEvent.pts[laterEvent.pts.length - 1].cg !== 300) {
    throw new Error('Spark fallback must use the chronologically latest evidence');
  }
  const earlyMarkers = helpers.buildShotSparkModel({
    wCg: [0, 20, 90, 150], wDtS: 2, durationS: 6,
    firstDropS: 4.5, dropCg: 40, extendedS: 3, extCg: 3000,
    atmS: 2, atmCg: 2000, atmClearedS: 3, endS: 4, endCg: 90,
  });
  if (!earlyMarkers || earlyMarkers.pts.some((p) => p.t < 4.5) ||
      earlyMarkers.segs.some((s) => s.pts.some((p) => p.t < 4.5))) {
    throw new Error('Spark must reject every malformed pre-drop marker');
  }
  const noDrop = helpers.buildShotSparkModel({
    wCg: [0, 20, 90], wDtS: 2, durationS: 4,
  });
  if (!noDrop || noDrop.pts.map((p) => p.t).join(',') !== '0,2,4') {
    throw new Error('Spark without a first-drop event must keep its grid');
  }
  const oneSecond = helpers.buildShotSparkModel({
    wCg: [0, 100, 250, 200], wDtS: 1, durationS: 3,
  });
  const rates = oneSecond.flowSegs.map((s) => s.pts[0].cg / 100);
  if (rates.join(',') !== '1,1.5,0' || oneSecond.maxFlow !== 1.5 ||
      oneSecond.flowSegs.some((s) => s.pts.some((p) => !Number.isFinite(p.cg) || p.cg < 0))) {
    throw new Error('Flow curve must derive finite non-negative one-second local rates');
  }
  const partial = helpers.buildShotSparkModel({
    wCg: [0, 100, 200], wDtS: 1, durationS: 2.5, endS: 2.5, endCg: 350,
  });
  if (!partial || partial.flowSegs.at(-1).pts[0].cg !== 300 ||
      partial.flowSegs.at(-1).pts[1].t !== 2.5) {
    throw new Error('Flow curve must use the actual partial end interval');
  }
  const missing = helpers.buildShotSparkModel({
    wCg: [0, 100, null, 300], wDtS: 1, durationS: 3,
  });
  if (!missing || missing.flowSegs.some((s) => s.pts[1].t - s.pts[0].t > 1.0001)) {
    throw new Error('Flow curve must not bridge missing weight samples');
  }
  const slowLate = helpers.buildShotSparkModel({
    wCg: Array.from({length: 14}, (_, i) => (i + 1) * 200),
    wDtS: 2,
    durationS: 26.4,
    firstDropS: 8.4,
    extendedS: 24.1,
    extCg: 2600,
    endS: 26.4,
    endCg: 2800,
    slowExtractionExtended: true,
    goalG: 36,
  });
  if (!slowLate || !slowLate.segs.some((s) => s.color === '#2563eb' &&
      s.pts[0].t >= 24)) {
    throw new Error('Spark late Slow guard must paint blue from extended');
  }
  const atm = helpers.buildShotSparkModel({
    wCg: [100, 800, 1600, 2400, 2800, 3000, 3200, 3400],
    wDtS: 2,
    durationS: 18,
    firstDropS: 4,
    atmS: 12,
    atmCg: 2800,
    endS: 18,
    endCg: 3400,
    goalG: 36,
  });
  const gray = atm && atm.segs.find((s) => s.color === 'var(--mu)');
  if (!gray || gray.pts.some((p) => p.cg !== 2800) ||
      gray.pts[0].t < 12 || gray.pts[gray.pts.length - 1].t < 18) {
    throw new Error('Spark A→M must be flat gray at last scale weight through ended');
  }
  if (atm.flowSegs.some((s) => s.pts[0].t < 18 && s.pts[1].t > 12)) {
    throw new Error('Flow curve must leave the A→M no-scale interval empty');
  }
  const flow = helpers.shotDisplayFlowGS({
    avgFlowGS: null,
    actualG: 43.7,
    durationS: 15.1,
    firstDropS: 4.5,
  });
  if (!(flow > 4.12 && flow < 4.13)) {
    throw new Error('History flow fallback must use actual/(duration-firstDrop)');
  }
}

// Over-the-air update safety contract. The controller lives inside a closed
// machine, so every rule below exists to keep a bad image from becoming the
// boot image, and a bad boot from becoming permanent.
{
  const ota = fs.readFileSync(path.join(sketchDir, 'ShotStopperOta.cpp'), 'utf8');
  const otaHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperOta.h'), 'utf8');
  const otaImage = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperOtaImage.h'), 'utf8');
  const version = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperVersion.h'), 'utf8');

  // The transfer must target the spare slot, never the one that is executing.
  if (!ota.includes('esp_ota_get_next_update_partition(nullptr)') ||
      !ota.includes('target != running')) {
    throw new Error('OTA must stage into the inactive slot only');
  }
  // Any early exit has to release the OTA handle; a leaked handle would keep
  // the flash driver's state machine open until the next reboot.
  if (!ota.includes('esp_ota_abort(') || !ota.includes('esp_ota_end(') ||
      !ota.includes('clearSession(true)')) {
    throw new Error('OTA must abort or end every esp_ota_begin handle');
  }
  // The boot selection changes in exactly one place, after re-reading the
  // staged identity straight from flash.
  if ((ota.match(/esp_ota_set_boot_partition\(/g) || []).length !== 1 ||
      !/if \(!reconfirmStagedTag\(\)\) \{[\s\S]{0,400}?esp_ota_set_boot_partition\(target\)/
          .test(ota)) {
    throw new Error(
      'OTA must re-verify the staged image immediately before switching the boot partition');
  }
  // Rolling back must never reboot on its own: machine circuit has to be opened first.
  if (ota.includes('esp_ota_mark_app_invalid_rollback_and_reboot') ||
      ota.includes('ESP.restart') || ota.includes('esp_restart')) {
    throw new Error('OTA must not restart the controller directly; machine circuit is opened first');
  }
  if (!ota.includes('rejected_ = true') ||
      !/if \(rejected_\) \{\s*return false;/.test(ota)) {
    throw new Error(
      'rejectRunningImage must record the rejection so confirmRunningImage cannot cancel it');
  }
  if (!css.includes('.hidden,[hidden]{display:none!important}')) {
    throw new Error('The shared hidden rule must override displayed controls including OTA progress');
  }
  if (!ota.includes('esp_ota_check_rollback_is_possible()')) {
    throw new Error(
      'OTA must confirm a bootable alternative exists before arming a rollback');
  }
  // Running out of URI handler slots makes registration fail at runtime, which
  // stops the HTTP server outright: no Web UI, and no way to update over the
  // air. It is a silent runtime failure, so it has to be caught here.
  {
    const limit = network.match(/config\.max_uri_handlers = (\d+);/);
    if (limit == null) {
      throw new Error('Could not find config.max_uri_handlers');
    }
    const routes = (network.match(/registerHandler\(server_/g) || []).length;
    if (routes >= Number(limit[1])) {
      throw new Error(
        `${routes} routes registered with max_uri_handlers=${limit[1]}; raise ` +
        'the limit so registration keeps headroom');
    }
  }

  // The OTA object is spliced into the admin status, so an unclosed brace here
  // would make the whole Admin page unparseable, not just the OTA panel.
  if (!/const size_t tagCapacity = capacity - 1;/.test(network) ||
      !/if \(used \+ 2 > capacity\) \{[\s\S]{0,200}?"\{\\"otaProtocolVersion\\":%u,\\"available\\":false\}"/
        .test(network)) {
    throw new Error(
      'buildOtaJson must reserve room for its closing brace and fall back to a ' +
      'valid object when the OTA JSON does not fit');
  }
  if (!otaHeader.includes('OTA_PROTOCOL_VERSION = 2') ||
      !network.includes('\\"otaProtocolVersion\\":%u') ||
      !network.includes('\\"runningIdentityValid\\":%s') ||
      !network.includes('\\"sessionArch\\":\\"%s\\"') ||
      !network.includes('\\"sessionVersion\\":\\"%s\\"')) {
    throw new Error(
      'OTA status must expose protocol and complete resumable identity');
  }
  if (!runtimeJs.includes('function otaRemoteMatches(') ||
      (runtimeJs.match(/!otaRemoteMatches\(session,status\)/g) || []).length < 2) {
    throw new Error(
      'Web OTA must bind session creation and reconciliation to the requested transferId');
  }
  // Recovering an image too broken to run any firmware code is the bootloader's
  // job, so losing that configuration must break the build, not the machine.
  for (const symbol of ['CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE',
                        'CONFIG_APP_ROLLBACK_ENABLE',
                        'CONFIG_BOOTLOADER_WDT_ENABLE']) {
    if (!new RegExp(`#if !defined\\(${symbol}\\)\\s*\\n#error`).test(otaHeader)) {
      throw new Error(`OTA header must fail the build when ${symbol} is missing`);
    }
  }
  // A build that lost its identity marker could never be verified by the next
  // one, and would also accept a foreign image.
  if (!version.includes('FW_IMAGE_TAG_STRING') ||
      !version.includes('FW_BOARD_ARCH_STRING') ||
      !ota.includes('FW_IMAGE_TAG_STRING')) {
    throw new Error('Firmware must embed the Shot Stopper OTA image tag');
  }
  // The needle is assembled at run time so a compiled image contains exactly
  // one contiguous copy of the prefix: its own tag, never the search pattern.
  if (otaImage.includes('"SHOTSTOPPER_FW_TAG_V1|"') ||
      !otaImage.includes('OTA_TAG_PREFIX_PART_1') ||
      !otaImage.includes('OTA_TAG_PREFIX_PART_2')) {
    throw new Error('OTA tag prefix must stay split so it is not embedded contiguously');
  }
  if (!otaHeader.includes('PENDING_VERIFY') ||
      !ota.includes('if (!confirmed_) {')) {
    throw new Error(
      'OTA must refuse to stage while the running image is still pending verification');
  }

  // The Arduino core would confirm the image inside initArduino(), before this
  // firmware has proven anything.
  if (!/extern "C" bool verifyRollbackLater\(\) \{\s*return true;/.test(firmware)) {
    throw new Error('Firmware must defer OTA rollback verification to the network layer');
  }
  if (!firmware.includes('ShotStopperOta::instance().begin()')) {
    throw new Error('Firmware must initialise the OTA module during setup');
  }

  // The unsafe WebUI override may relax settings, never a firmware update.
  const otaHandlers = network.slice(network.indexOf('ShotStopperNetwork::otaStatusHandler'));
  if (otaHandlers.includes('webUiOverrideAllowed') ||
      otaHandlers.includes('webUiConfigurationAllowed')) {
    throw new Error('OTA handlers must not honour the unsafe WebUI override');
  }
  if ((otaHandlers.match(/authorizeOtaRequest\(request\)/g) || []).length !== 6) {
    throw new Error('Every OTA route must authenticate the request');
  }
  if (ota.includes('OTA_SAFETY_CHECK_INTERVAL_BYTES') ||
      !/while \(failure == OtaResult::OK && rangeReceived < contentLength\) \{\s*\n\s*\/\/ Shot always wins[\s\S]{0,400}?if \(!io\.stillSafe\(io\.context\)\) \{/
          .test(ota)) {
    throw new Error(
        'OTA must abort on every chunk when the paddle moves, not on a byte interval');
  }
  if (!network.includes('otaTransferStillSafe') ||
      !/bool ShotStopperNetwork::otaTransferStillSafe[\s\S]{0,400}?controlAllowsConfiguration/
          .test(network)) {
    throw new Error(
        'OTA stillSafe must keep controlAllowsConfiguration so a paddle pull aborts the transfer');
  }
  const restartHandler = network.slice(
      network.indexOf('ShotStopperNetwork::restartHandler'),
      network.indexOf('ShotStopperNetwork::factoryResetHandler'));
  if (restartHandler.includes('unsafeWebUiOverride') ||
      restartHandler.includes('webUiConfigurationAllowed') ||
      restartHandler.includes('CONFIG_LOCKED_DURING_ACTIVE_CYCLE')) {
    throw new Error(
        'Admin restart must queue during a shot and must not use the unsafe override to cut it');
  }
  if (!firmwareCore.includes('holdOrBeginPlannedRestart') ||
      !firmwareCore.includes('plannedRestartHeld') ||
      !firmwareCore.includes('servicePendingPlannedRestart')) {
    throw new Error(
        'Planned restart must be held until idle instead of machineRequestStop on an active shot');
  }
  {
    const rebootCase = firmwareCore.slice(
        firmwareCore.indexOf('case SerialCliVerb::REBOOT:'),
        firmwareCore.indexOf('case SerialCliVerb::UNKNOWN:'));
    if (!rebootCase.includes('serialCliQueueCommand') ||
        rebootCase.includes('serialCliQueueIfSafe')) {
      throw new Error(
          'Serial REBOOT must queue during a shot and wait, not reject as not-ready');
    }
  }
  if (!firmwareCore.includes('const bool faultRestart') ||
      !firmwareCore.includes(
          '!session.active && !getRelaySafetySnapshot().closed')) {
    throw new Error(
        'Planned ESP.restart must wait for an open circuit; only fault restarts cut a shot');
  }
  {
    const rollback = network.slice(
        network.indexOf('void ShotStopperNetwork::serviceOtaRollback'));
    if (!rollback.includes('control.activeCycle, control.relayClosed') ||
        !otaSource.includes('if (activeCycle || relayClosed || busy_ || sessionActive_)')) {
      throw new Error(
          'OTA confirm/rollback must not write otadata while a shot is pouring');
    }
  }
  if (!html.includes('Paddle during upload cancels it') ||
      !html.includes('Restarts after any shot')) {
    throw new Error(
        'Web UI must say the paddle aborts OTA and that restart waits for the shot');
  }
  if (!runtimeJs.includes("Flashed. Restart waits until the shot ends.") ||
      runtimeJs.includes('Locked while the machine is busy') ||
      !runtimeJs.includes('Waiting for idle (') ||
      !runtimeJs.includes('Restart after the shot.')) {
    throw new Error(
        'OTA status must wait for the shot, not claim the machine is locked');
  }
  {
    const applyOta = runtimeJs.slice(
        runtimeJs.indexOf('function applyOtaStatus'),
        runtimeJs.indexOf('function otaSend'));
    if (applyOta.includes('stopButton') || applyOta.includes('rinseButton') ||
        applyOta.includes('paddleButton') || applyOta.includes('startButton')) {
      throw new Error('OTA busy must not disable Start or Rinse');
    }
  }
  if (!network.includes(
          'The paddle moved or a shot started during the upload')) {
    throw new Error(
        'SAFETY_LOST must tell the operator the paddle aborted the upload');
  }
  if (!network.includes('devicePasswordsMatch(password, expected)')) {
    throw new Error(
      'OTA authentication must compare the device password in constant time');
  }
  if (network.includes('tokenAuthorized = !factoryPassword && otaTokensMatch') ||
      network.includes('passwordAuthorized = !factoryPassword') ||
      (network.includes('authorizeOtaRequest') &&
       network.slice(network.indexOf('ShotStopperNetwork::authorizeOtaRequest'),
                     network.indexOf('ShotStopperNetwork::buildOtaJson'))
           .includes('if (factoryPassword)'))) {
    throw new Error(
      'OTA authentication must accept the factory default device password');
  }
  const currentDeviceAt = html.indexOf('id="currentDevicePassword"');
  const newDeviceAt = html.indexOf('id="newDevicePassword"');
  const confirmDeviceAt = html.indexOf('id="confirmDevicePassword"');
  if (currentDeviceAt >= 0) {
    throw new Error('Device password form must not ask for the current password after admin unlock');
  }
  if (newDeviceAt < 0 || confirmDeviceAt < 0 || !(newDeviceAt < confirmDeviceAt)) {
    throw new Error('Device password form must ask for the new password and confirmation');
  }
  if (!js.includes("newPassword:$('newDevicePassword').value") ||
      !network.includes('"newPassword"') ||
      network.includes('"currentPassword"') ||
      !network.includes('DEVICE_PASSWORD_INVALID') ||
      !network.includes('requireAdminUnlock(request)')) {
    throw new Error('Web device password change must require admin unlock, not the current password');
  }
  if (!html.includes('id="adminLockPanel"') ||
      !html.includes('id="adminUnlockPassword"') ||
      !html.includes('id="adminUnlockButton"') ||
      !html.includes('Unlock administration') ||
      !html.includes('id="adminControls"') ||
      !html.includes('15 minutes after the last privileged action') ||
      !html.includes('Lock closes it now') ||
      !html.includes('id="navAdminLock"') ||
      !html.includes('id="adminLockButton"') ||
      html.includes('id="homeAdminLock"') ||
      !html.includes('this window will confirm automatically') ||
      html.includes('unlock to confirm') ||
      js.includes('Unlock to confirm') ||
      js.includes('unlock to confirm') ||
      !js.includes('function lockAdminUi()') ||
      !js.includes('function lockAdmin()') ||
      !js.includes("aria-expanded','false');clearTimeout(scanTimer);scanTimer=0;api('/api/v1/admin/lock'") ||
      !js.includes('function syncAdminSessionUi(unlocked,remoteEnabled=false)') ||
      !js.includes('stopViewPolls();lockAdminUi();setMutable(false)') ||
      !js.includes('/api/v1/admin/unlock') ||
      !js.includes('/api/v1/admin/lock') ||
      !js.includes("closest('#adminLockPanel") ||
      !css.includes('.hidden,[hidden]{display:none!important}') ||
      !css.includes('.textLock') ||
      !network.includes('/api/v1/admin/unlock') ||
      !network.includes('/api/v1/admin/lock') ||
      !network.includes('ShotStopperNetwork::adminLockHandler') ||
      !network.includes('ADMIN_LOCKED') ||
      !network.includes('ADMIN_UNLOCK_COOLDOWN') ||
      !network.includes('ADMIN_UNLOCK_IDLE_MS') ||
      !network.includes('grantAdminUnlock') ||
      !network.includes('secretsMatch(password, expected)') ||
      js.includes('loginHandler') ||
      network.includes('ShotStopperNetwork::loginHandler')) {
    throw new Error('Admin must gate behind a temporary device-password unlock on firmware and UI');
  }
  if (!html.includes('id="diagnosticControls"') ||
      !js.includes('showDiagnosticPage') ||
      !js.includes('diagnosticPublic') ||
      !viewJs.admin.includes("saveToggle('showDiagnosticPage')") ||
      !viewJs.admin.includes('await waitSaved(a.requestId,id,p)') ||
      !viewJs.admin.includes('R.withBaseRev({[id]:wanted})') ||
      viewJs.admin.includes('waitDiagnosticPagePersisted') ||
      (viewJs.admin.match(/\bapplyStatus\b/g) || []).length !== 2 ||
      js.includes("$('diagnosticUnlockButton').onclick") ||
      !network.includes('DIAGNOSTIC_DISABLED') ||
      !network.includes('showDiagnosticPage') ||
      !firmwareCore.includes(
          'candidate.showDiagnosticPage = command.config.showDiagnosticPage')) {
    throw new Error(
        'Diagnostic must be an Admin-controlled public opt-in, disabled by default');
  }
  if (runtimeJs.includes('row.innerHTML') ||
      runtimeJs.includes("shotType[0]!=='a'|y<1") ||
      runtimeJs.includes('pollChain=run.catch(()=>{})') ||
      !runtimeJs.includes('pollChain=run.catch(console.warn)')) {
    throw new Error(
        'Shot history must use DOM textContent, logical OR in stats, and must not swallow poll errors');
  }
  {
    const start = firmwareCore.indexOf('void completeBootRecovery(');
    const end = firmwareCore.indexOf('void resumePendingBootRecovery(');
    const body = start >= 0 && end > start ? firmwareCore.slice(start, end) : '';
    if (!body.includes('ensureRecoveryIntent') ||
        !body.includes('abandonFailedBootRecovery') ||
        !body.includes('(void)clearRecoveryIntent()') ||
        !body.includes('bootRecoveryShouldRestartAfterSuccess') ||
        firmwareCore.includes('void holdFailedBootRecovery') ||
        !firmwareCore.includes('void abandonMalformedRecoveryIntent()') ||
        !firmwareCore.includes('PendingRecoveryKind::MALFORMED')) {
      throw new Error(
          'Boot recovery must apply or abandon the latch and never hang');
    }
  }
  {
    const factoryFnStart = network.indexOf('ShotStopperNetwork::factoryResetHandler');
    const passwordFnStart = network.indexOf('ShotStopperNetwork::devicePasswordHandler');
    const restartFnStart = network.indexOf('ShotStopperNetwork::restartHandler');
    const bleFnStart = network.indexOf('ShotStopperNetwork::bleCompatHandler');
    const timeFnStart = network.indexOf('ShotStopperNetwork::timeSyncHandler');
    for (const [label, start] of [
      ['factory-reset', factoryFnStart],
      ['device-password', passwordFnStart],
      ['restart', restartFnStart],
      ['ble-compat', bleFnStart],
      ['task-profiler', network.indexOf('ShotStopperNetwork::taskProfilerHandler')],
      ['time-sync', timeFnStart],
      ['wifi-scan-start', network.indexOf('ShotStopperNetwork::wifiScanStartHandler')],
      ['wifi-scan-status', network.indexOf('ShotStopperNetwork::wifiScanStatusHandler')],
      ['paddle', network.indexOf('ShotStopperNetwork::paddleHandler')],
      ['rinse', network.indexOf('ShotStopperNetwork::rinseHandler')],
      ['stop', network.indexOf('ShotStopperNetwork::stopHandler')]
    ]) {
      if (start < 0 ||
          !network.slice(start, start + 500).includes('requireAdminUnlock(request)')) {
        throw new Error(label + ' must require admin unlock');
      }
    }
    const unlockFnStart = network.indexOf('ShotStopperNetwork::unlockHandler');
    if (unlockFnStart < 0 ||
        !network.slice(unlockFnStart, unlockFnStart + 600).includes('requireAdminUnlock(request)')) {
      throw new Error('ui/unlock must require admin unlock');
    }
    const networkFn = network.slice(
        network.indexOf('ShotStopperNetwork::networkHandler'),
        network.indexOf('ShotStopperNetwork::wifiScanStartHandler'));
    if (!networkFn.includes('requireAdminUnlock(request)') ||
        !networkFn.includes('strcmp(action, "confirm") != 0')) {
      throw new Error('Network save/forget must require admin unlock; confirm may stay ungated');
    }
    if (networkFn.includes('settingsCopy()')) {
      throw new Error(
          'networkHandler must not return PersistedSettings by value on the httpd stack');
    }
  }
  if ((statusFormat.match(/\\"adminUnlocked\\":%s/g) || []).length < 2 ||
      !statusFormat.includes('\\"adminUnlocked\\":%s,\\"diagnosticPublic\\":true')) {
    throw new Error('status/home and status/admin must report adminUnlocked; Diagnostic must identify public access');
  }
  if ((statusFormat.match(/\\"adminUnlocked\\":%s,\\"development\\":%s/g) || []).length < 2 ||
      !statusFormat.includes('\\"diagnosticPublic\\":true,\\"development\\":%s')) {
    throw new Error('status pages must report development state alongside their access state');
  }
  if (!network.includes('page == StatusPage::Admin || page == StatusPage::Home') ||
      !network.includes('page == StatusPage::Diagnostic') ||
      !ui.includes("v==='home'?!!(typeof s.adminUnlocked==='boolean'") ||
      !js.includes('function syncAdminSessionUi(unlocked,remoteEnabled=false)') ||
      !js.includes('syncAdminSessionUi(admin,remoteReady)') ||
      !js.includes('updateHomeAdminActions(on,remoteEnabled)')) {
    throw new Error('Home Actions must require adminUnlocked and remoteControlEnabled');
  }
  if (!network.includes(
          'page == StatusPage::Admin || page == StatusPage::Diagnostic') ||
      !network.includes('self.touchAdminUnlock()') ||
      /if \(adminUnlocked\) \{\s*self\.touchAdminUnlock\(\);/.test(network)) {
    throw new Error(
        'Home status must report adminUnlocked without sliding idle; Admin and Diagnostic polls may renew');
  }
  {
    const start = network.indexOf('ShotStopperNetwork::adminLockHandler');
    const end = network.indexOf('bool ShotStopperNetwork::requireActiveWebUiClient');
    const body = start >= 0 && end > start ? network.slice(start, end) : '';
    if (!body.includes('clearAdminUnlock()') ||
        body.includes('requireAdminUnlock')) {
      throw new Error('Admin lock must clear unlock without requiring a still-valid unlock');
    }
  }
  if (!statusFormat.includes('!adminUnlocked') ||
      !js.includes('s.adminUnlocked')) {
    throw new Error('Locked admin status must omit Wi-Fi/BLE/OTA bodies until unlocked');
  }
  if (!network.includes('serviceOtaRollback(now)') ||
      !otaSource.includes('decideOtaPendingVerify(') ||
      !network.includes(
          'scaleConnectingOrUp_.load(std::memory_order_relaxed)') ||
      !otaHeader.includes('decideOtaPendingVerify') ||
      !otaHeader.includes('bool flashWriteSafe = true') ||
      !otaHeader.includes('KEEP_RUNNING') ||
      !networkHeader.includes('OTA_CONFIRM_MIN_UPTIME_MS') ||
      !networkHeader.includes('OTA_CONFIRM_DEADLINE_MS')) {
    throw new Error(
        'Network service must confirm, roll back, or KEEP_RUNNING a pending OTA image without writing otadata during GATT');
  }
  if (!otaSource.includes('RejectResult::NO_ALTERNATIVE') ||
      !otaSource.includes('confirmRunningImageLocked()')) {
    throw new Error(
        'OTA must keep the running image when rollback is impossible');
  }
  for (const code of [
    'OTA_UPLOAD_STARTED', 'OTA_IMAGE_STAGED', 'OTA_UPLOAD_REJECTED',
    'OTA_FLASH_COMMITTED', 'OTA_IMAGE_CONFIRMED', 'OTA_ROLLBACK_ARMED',
    'OTA_ROLLBACK_FAILED'
  ]) {
    if (!domain.includes(`DebugCode::${code}`)) {
      throw new Error(`Diagnostic log must report OTA event: ${code}`);
    }
  }

  // Admin page layout: the update lives between Restart and Factory reset.
  const restartAt = html.indexOf('id="restartPanel"');
  const otaAt = html.indexOf('id="otaPanel"');
  const factoryAt = html.indexOf('id="factoryResetButton"');
  if (restartAt < 0 || otaAt < 0 || factoryAt < 0 ||
      !(restartAt < otaAt && otaAt < factoryAt)) {
    throw new Error('Admin OTA panel must sit between Restart and Factory reset');
  }
  for (const id of [
    'otaStatus', 'otaRunning', 'otaStaged', 'otaFile',
    'otaProgress', 'otaVerifyButton', 'otaFlashButton', 'otaDiscardButton'
  ]) {
    if (!html.includes(`id="${id}"`)) {
      throw new Error(`Admin OTA panel is missing control: ${id}`);
    }
  }
  if (html.includes('id="otaToken"') || js.includes('otaGuardToken') ||
      js.includes('X-OTA-Token') || js.includes('X-Device-Password') ||
      js.includes('OTA token') || js.includes('ota password')) {
    throw new Error(
      'Web UI OTA must not collect a separate password after admin unlock');
  }
  if (!js.includes('xhr.setRequestHeader(WEB_UI_CLIENT_HEADER,webUiClientId)') ||
      !network.includes('adminUnlockAllowed(request)') ||
      !network.includes('devicePasswordsMatch(password, expected)') ||
      !network.includes('X-Device-Password')) {
    throw new Error(
      'OTA must accept an admin unlock session or the device password header');
  }
  // Two steps: verify writes the spare slot, a separate button flashes it.
  if (!js.includes("otaSend('/api/v1/ota/session'") ||
      !js.includes("otaSend('/api/v1/ota',chunk") ||
      !js.includes("'PATCH',headers") || !js.includes('otaFileIdentity') ||
      !js.includes("otaSend('/api/v1/ota/flash'") ||
      !js.includes("otaSend('/api/v1/ota/abort'") ||
      !js.includes("$('otaFlashButton').disabled=!ready||!staged")) {
    throw new Error('Web UI must upload/verify and flash as two separate steps');
  }
  if (!js.includes('!otaBusy&&Date.now()-lastStatusAt')) {
    throw new Error('Web UI must pause status polling during a firmware transfer');
  }
}

console.log(
  `Embedded Web UI: modules+partials valid, ${htmlBytes} bytes HTML / ${jsBytes} bytes JS source, ` +
  `${generated.gzip.length} bytes shell gzip, ${generated.jsGzip.length} bytes app.js gzip, ` +
  `${generated.runtimeGzip.length} bytes runtime gzip, ${generated.otaImageGzip.length} bytes OTA image gzip, ` +
  `${generated.secondaryGzip.length} bytes secondary gzip, ` +
  `${generated.cssGzip.length} bytes CSS gzip, combined ${generated.combined} bytes gzip, ` +
  `${expected.size} routes checked`
);
})().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
