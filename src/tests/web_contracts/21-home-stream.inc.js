// Home uses the existing owned socket, with independent scalar and curve state.
{
  const assert = require('assert').strict, vm = require('vm');
  const source = runtimeStateDecl + runtimeJs.slice(runtimeJs.search(/let\s+homeFrame\s*=/),
    runtimeJs.indexOf('function setHomeSub(')).replace(/export /g, '');
  const validator = runtimeJs.slice(runtimeJs.indexOf('function statusPageOk('),
    runtimeJs.indexOf('async function loadStatus('));
  const sockets = [], timers = new Map(), rendered = [], common = [];
  let timer = 0, mutable = false, owner = true, reachOk = 0;
  class Socket {
    static OPEN = 1;
    constructor(url) { this.url = url; this.readyState = 1; this.sent = []; sockets.push(this); }
    send(value) { this.sent.push(JSON.parse(value)); }
    close(code = 1006) { this.readyState = 3; this.onclose?.({code}); }
  }
  const context = {
    WebSocket: Socket, location: {protocol: 'http:', host: 'device.local'},
    document: {hidden: false, addEventListener() {}}, window: {addEventListener() {}},
    setTimeout: (fn, delay) => { timers.set(++timer, {fn, delay}); return timer; },
    clearTimeout: id => timers.delete(id), requestAnimationFrame: () => 1,
    webUiPollingActive: () => owner, webUiClientId: '0123456789abcdef', webUiPowerSeconds: () => 30,
    activeView: 'home', viewSeq: 1, viewReady: Promise.resolve(), lastStatusAt: 0,
    performance: {now: () => 1234}, statusUtcAnchorSec: 0, statusUtcAnchorAt: 0,
    statusLiveShot: false, viewStatusHandlers: {home: value => rendered.push(value)},
    applyCommonStatus: value => { common.push(value); mutable = !!value.configMutable; },
    applyPairingPrompt() {}, ingestPresets() {},
    setMutable: value => { mutable = value; }, noteReachOk: () => { reachOk++; }, noteReachFail() {}, hideHomeBoot() {},
    clearCupWeights() {}, updateHeaderSignals() {}, updateHomeAdminActions() {}, setHomeSub() {},
    deactivateWebUi: () => { owner = false; }, $: () => null, console: {error() {}},
  };
  vm.runInNewContext(validator + source, context);
  const status = {firmwareVersion: 'test', bootId: 7, configMutable: true, timeUtcSec: 1700000000,
    adminUnlocked: false, snapshotStale: false, config: {soundAlertsEnabled: true, revision: 1},
    safety: {state: 'READY'}, scale: {available: true, streamState: 'FRESH', observedWeightG: 12, timerMs: 1000},
    presets: {activeId: 1, items: []}, presetsTag: 42, cycle: {active: false}, lastShot: {valid: false},
    noScaleShotGuard: {enabled: true}, machineState: 'IDLE', cupPresence: {present: false}};
  const flatten = (value, prefix = '', output = {}) => {
    for (const [key, item] of Object.entries(value)) {
      const name = prefix + key;
      if (item && typeof item === 'object' && !Array.isArray(item) && key !== 'presets') flatten(item, name + '.', output);
      else output[name] = item;
    }
    return output;
  };
  const initial = {v: 1, type: 'home', boot: 7, snapshot: true, changes: flatten(status)};
  const first = context.statusStreamFrame(null, initial);
  // Increments carry only type and changes; the snapshot's identity continues.
  const patch = {type: 'home', changes: {'scale.observedWeightG': 12.1, 'machineState': 'RUNNING'}};
  const second = context.statusStreamFrame(first, patch);
  assert.equal(second.status.scale.timerMs, 1000);
  assert.equal(second.status.scale.observedWeightG, 12.1);
  assert.equal(first.status.scale.observedWeightG, 12.1, 'Home applies scalar patches to its current cache');
  assert.equal(context.statusStreamFrame(second, patch).status.scale.observedWeightG, 12.1, 'scalar assignments are idempotent');
  const lost = context.statusStreamFrame(second, {...patch,
    changes: {'scale.available': false, 'scale.observedWeightG': null, 'scale.timerMs': null}});
  assert.equal(lost.status.scale.timerMs, null);
  assert.equal(second.boot, 7, 'an increment keeps the snapshot boot');
  for (const [previous, bad] of [[null, patch],
    [first, {...patch, changes: {'scale.timerMs': Infinity}}],
    [first, {...patch, changes: {'config.constructor.prototype': {poisoned: true}}}],
    [first, {...initial, changes: {'scale.available': true}}]]) {
    assert.throws(() => context.statusStreamFrame(previous, bad), 'invalid Home messages require resync');
  }
  assert.equal(context.statusStreamFrame(first, {...initial, boot: 8, changes: {...initial.changes, bootId: 8}}).boot, 8);
  // A reconnect snapshot that omits presets keeps the copy with the same tag.
  const {presets: _omitted, ...withoutPresets} = initial.changes;
  const reconnect = {...initial, changes: withoutPresets};
  assert.equal(context.statusStreamFrame(null, reconnect, first.status).status.presets, first.status.presets);
  assert.throws(() => context.statusStreamFrame(null, {...reconnect, changes: {...withoutPresets, presetsTag: 43}}, first.status),
      'a different tag cannot reuse the kept presets');
  assert.throws(() => context.statusStreamFrame(null, reconnect), 'without a kept copy the snapshot needs its presets');
  context.startUiStream(); sockets[0].onopen();
  assert.equal(sockets[0].sent[0].op, 'bind');
  assert.equal(sockets[0].sent[1].op, 'activity', 'liveness is a socket control message, not REST polling');
  assert.equal(sockets[0].sent[1].hidden, false, 'a visible page asks for every frame');
  // Every frame proves the socket; the ping only fills gaps in the traffic.
  assert(codeIncludes(runtimeJs, 'uiFrameAt=Date.now();clearTimeout(shotDeadline);') &&
      codeIncludes(runtimeJs, 'now-uiSentAt>=12e4'));
  sockets[0].onmessage({data: JSON.stringify({v: 1, type: 'alive'})});
  sockets[0].onmessage({data: JSON.stringify(initial)});
  assert(mutable); assert.equal(rendered.at(-1).scale.observedWeightG, 12);
  assert.equal(context.statusUtcAnchorSec, status.timeUtcSec);
  assert.equal(context.statusUtcAnchorAt, 1234);
  assert.equal(rendered.at(-1).timeUtcSec, undefined, 'cached Home status must not reset the shared clock anchor');
  // A Home render bug on a valid frame is logged, not counted as a corrupt
  // frame: no resync, no close, and the next frame draws again.
  const handler = context.viewStatusHandlers.home, sentBefore = sockets[0].sent.length;
  context.viewStatusHandlers.home = () => { throw new TypeError('render bug'); };
  sockets[0].onmessage({data: JSON.stringify(patch)});
  assert(sockets[0].readyState === 1 && sockets[0].sent.length === sentBefore, 'render errors keep the stream');
  context.viewStatusHandlers.home = handler;
  sockets[0].onmessage({data: JSON.stringify(patch)});
  assert.equal(rendered.at(-1).machineState, 'RUNNING');
  vm.runInNewContext("activeView='settings'", context);
  context.statusUtcAnchorSec += 10;
  const paints = rendered.length, reachBefore = reachOk;
  sockets[0].onmessage({data: JSON.stringify({...patch, changes: {'config.revision': 2}})});
  assert.equal(rendered.length, paints, 'off-Home deltas must not change another page or its edit gate');
  assert.equal(reachOk, reachBefore + 1,
      'off-Home frames still prove reachability so the out-of-reach overlay clears on every view');
  vm.runInNewContext("activeView='home';renderHomeStream()", context);
  assert.equal(rendered.at(-1).config.revision, 2, 'navigation must render the updated cache');
  assert.equal(context.statusUtcAnchorSec, status.timeUtcSec + 10, 'Home must retain the current clock after another page updates it');
  sockets[0].close(); assert.equal(mutable, false, 'stream loss must disable Home controls');
  context.startUiStream(); sockets[1].onopen();
  assert.equal(sockets[1].sent[0].presetsTag, 42, 'a reconnect reports the presets it holds');
  sockets[1].onmessage({data: JSON.stringify(initial)});
  assert.equal(rendered.at(-1).scale.observedWeightG, 12, 'reconnect accepts an initial snapshot');
  sockets[1].onmessage({data: JSON.stringify({...patch, changes: {'__proto__.x': 1}})});
  assert.equal(mutable, false, 'malformed deltas must fail closed');
  assert.equal(sockets[1].sent.at(-1).op, 'resync');
  sockets[1].onmessage({data: JSON.stringify(patch)});
  sockets[1].onmessage({data: JSON.stringify({v: 1, snapshot: false})});
  assert.equal(sockets[1].readyState, Socket.OPEN, 'queued Home/shot deltas must be discarded during resync');
  sockets[1].onmessage({data: JSON.stringify(initial)});
  assert(mutable);
  sockets[1].close(4001); assert.equal(owner, false, 'takeover must deactivate the old Home without REST');
  const polling = runtimeJs.slice(runtimeJs.indexOf('function armStatusTimer('), runtimeJs.indexOf('function pad2('));
  assert(!polling.includes("activeView!=='home'"), 'Home must not arm the REST polling timer');
  assert(codeIncludes(runtimeJs, "s=v==='home'?await loadHomeStatus():v==='diagnostic'?await loadDiagnosticStatus():await api("),
      'Home and Diagnostic load from the stream; only the remaining views REST');
  const rest = fs.readFileSync(path.join(sketchDir, 'network/OpenBrewByWeightStatus.inc'), 'utf8');
  assert(!rest.includes('StatusPage::Home') && !rest.includes('/api/v1/status/home'),
      'Home state must be WebSocket-only; the REST metadata stub is deleted');
  assert(!rest.includes('loadControlStatus') || rest.indexOf('loadControlStatus') > rest.indexOf('// JSON / snapshot'),
      'routing must not snapshot control state');
  for (const legacy of ['homeLastShot', 'homeHistoryLinked', 'safeHomeShot', '"\\"shotCurve']) assert(!rest.includes(legacy));
  const formatCleaning = context.formatMicraCleaning;
  const lm = {accountConfigured: true, observeState: true, connectionType: 'websocket', websocket: {cleaningAvailable: true}};
  for (const [cleaning, expected] of [['inactive', 'Not running'], ['waiting_for_paddle', 'Ready · paddle to ON'], ['cleaning', 'Running']])
    assert.equal(formatCleaning({...lm, websocket: {...lm.websocket, cleaning}}), expected);
  assert.equal(formatCleaning({...lm, connectionType: 'api'}), 'Needs WebSocket');
  assert.equal(formatCleaning({...lm, observeState: false}), 'Disabled');
  assert(codeIncludes(viewJs.diagnostic, "set('Cleaning',R.formatMicraCleaning(lm))"));
  assert(codeIncludes(viewJs.home, 'R.formatMicraCleaning(s.lineaMicra)'));
}

// D20 + B1: Home's presets blob is per-preset granular. The harness runs the
// real preset pipeline (sparse merge, applyPreset, the pending-apply defer)
// against socket frames: a delta with one changed preset merges by id, a
// deletion prunes through the ordered ids list, an activeId-only delta flips
// the active preset, and two rapid applies paint only the last target while
// the intermediate server state is deferred (B1); a failed final apply falls
// back to the server's own active preset.
{
  const assert = require('assert').strict, vm = require('vm');
  const validator = runtimeJs.slice(runtimeJs.indexOf('function statusPageOk('),
    runtimeJs.indexOf('async function loadStatus('));
  const source = 'var formRev = 2, configLoaded = true, configRevision = 5, ' +
    'bbwFormPresetId = 0, dripFieldsPresetId = 0;\n' + runtimeStateDecl +
    runtimeJs.slice(runtimeJs.search(/let\s+presetState\s*=/),
      runtimeJs.indexOf('function setHomeSub(')) +
    ';this.ps = () => presetState; this.acc = () => ({pending: homeAccPendingId, server: presetServerActiveId});';
  const sockets = [], timers = new Map(), commands = [], settle = [];
  let timer = 0, owner = true;
  class Socket {
    static OPEN = 1;
    constructor(url) { this.url = url; this.readyState = 0; this.sent = []; sockets.push(this); }
    send(value) { this.sent.push(JSON.parse(value)); }
    open() { this.readyState = 1; this.onopen(); }
    close(code = 1006) { if (this.readyState === 3) return; this.readyState = 3; this.onclose?.({code}); }
  }
  const context = {
    WebSocket: Socket, location: {protocol: 'http:', host: 'device.local'},
    document: {hidden: false, addEventListener() {}, querySelectorAll: () => []},
    window: {addEventListener() {}},
    setTimeout: (fn, delay) => { timers.set(++timer, {fn, delay}); return timer; },
    clearTimeout: id => timers.delete(id), requestAnimationFrame: () => 1,
    webUiPollingActive: () => owner, webUiClientId: '0123456789abcdef', webUiPowerSeconds: () => 30,
    activeView: 'home', viewSeq: 1, viewReady: Promise.resolve(), lastStatusAt: 0,
    performance: {now: () => 1234}, statusUtcAnchorSec: 0, statusUtcAnchorAt: 0,
    statusLiveShot: false, viewStatusHandlers: {home() {}},
    applyCommonStatus() {}, applyPairingPrompt() {}, setMutable() {}, noteReachOk() {},
    noteReachFail() {}, hideHomeBoot() {}, clearCupWeights() {}, updateHeaderSignals() {},
    updateHomeAdminActions() {}, setHomeSub() {}, deactivateWebUi: () => { owner = false; },
    controlsMutable: true,
    sub: (t) => t,
    command: (path, value) => new Promise(resolve => { commands.push({path, value}); settle.push(resolve); }),
    clearBrewDirty() {}, dropQuickPending() {},
    $: () => null, console: {error() {}},
  };
  vm.runInNewContext(validator + source, context);
  const item = (id, name, extra = {}) => ({id, name, isFactory: id <= 4, brewByWeight: true,
    doseG: 18, goalWeightG: 36, fastBeforeS: 5, slowAfterS: 30, fastMarginG: 6, slowMarginG: 2,
    operationalWallMs: 60000, bbwProtectionMs: 3000, fastExtractionGuardEnabled: true,
    slowExtractionGuardEnabled: true, grindHintsEnabled: true, scaleLossFixedS: 0,
    bbwAlgorithm: 'linear_ewma', bbwAlphaBaseline: 0.2, lineaMicraBrewTargetC: 93,
    shotStats: {n: 0, total: 0}, firstDropS: 0, firstDropN: 0, arrivalsS: [], grindHint: null,
    ...extra});
  const base = {firmwareVersion: 'test', bootId: 7, configMutable: true, timeUtcSec: 1700000000,
    adminUnlocked: false, snapshotStale: false, config: {soundAlertsEnabled: true, revision: 5},
    safety: {state: 'READY'}, scale: {available: true, streamState: 'FRESH', observedWeightG: 12, timerMs: 1000},
    cycle: {active: false}, lastShot: {valid: false}, noScaleShotGuard: {enabled: true},
    machineState: 'IDLE', cupPresence: {present: false}};
  const snapshot = (presets, tag) => ({v: 1, type: 'home', boot: 7, snapshot: true,
    changes: {...base, presets, presetsTag: tag}});
  const delta = (presets, tag) => ({type: 'home', changes: {presets, presetsTag: tag}});
  const presetFrame = frame => sockets.at(-1).onmessage({data: JSON.stringify(frame)});
  // VM-realm arrays and objects never pass host deepStrictEqual: marshal the
  // ids, and compare item identity only within the model itself (the live
  // model object is read fresh at each step, so snapshots of it capture
  // scalars and item references, never the mutating container).
  const idsOf = items => [...items].map(p => +p.id);
  context.startUiStream(); sockets[0].open();
  const A = item(1, 'Double'), B = item(2, 'Single'), C = item(5, 'Mine');
  presetFrame(snapshot({activeId: 1, cutRampS: 5, items: [A, B, C], ids: [1, 2, 5]}, 42));
  const initial = context.ps(), initialRev = initial.rev, initialItems = [...initial.items];
  assert.deepEqual(idsOf(initialItems), [1, 2, 5], 'the snapshot seeds the model');
  assert.equal(initial.activeId, 1);
  // A sparse delta with one changed preset merges by id: the untouched item
  // objects are kept by reference and only the changed preset is replaced.
  const B2 = item(2, 'Single', {goalWeightG: 18});
  presetFrame(delta({items: [B2]}, 43));
  const merged = context.ps();
  assert.equal(merged.rev, initialRev + 1, 'a content change bumps the render revision');
  assert.equal(merged.items[1].goalWeightG, 18);
  assert.equal(merged.items[1].name, 'Single', 'the changed preset is the incoming object');
  assert.equal(merged.items[0], initialItems[0], 'an unchanged preset keeps its object');
  assert.equal(merged.items[2], initialItems[2], 'an unchanged preset keeps its object');
  // A deletion travels as the ordered id list alone: no item bytes.
  presetFrame(delta({activeId: 1, ids: [1, 5]}, 44));
  assert.deepEqual(idsOf(context.ps().items), [1, 5], 'the ids list prunes the deleted preset');
  // A preset switch is one envelope scalar, not the whole bank.
  presetFrame(delta({activeId: 5}, 45));
  assert.equal(context.ps().activeId, 5, 'an activeId-only delta flips the active preset');
  assert.equal(context.ps().selectedId, 5);
  assert.equal(context.acc().server, 5);
  // B1: two rapid applies B then A — the interleaved B frame paints data but
  // never the active flip; only A paints. (The active preset is C.)
  const applied = context.applyPreset(2);
  const appliedLast = context.applyPreset(1);
  await Promise.resolve();
  assert.equal(context.acc().pending, 1, 'the last tap owns the pending target');
  assert.equal(context.ps().selectedId, 1);
  presetFrame(delta({activeId: 2}, 46));
  assert.equal(context.ps().activeId, 5, 'the intermediate server state must not paint');
  assert.equal(context.acc().server, 2, 'the deferred server state is still recorded');
  presetFrame(delta({activeId: 1}, 47));
  assert.equal(context.ps().activeId, 1, 'the pending target paints when it lands');
  assert.equal(context.acc().pending, 0, 'the matching frame clears the pending target');
  settle.splice(0).forEach(done => done(true));
  await applied; await appliedLast;
  assert.equal(context.ps().activeId, 1);
  // B1: a failed final apply falls back to the server's own active preset.
  const failing = context.applyPreset(2);
  await Promise.resolve();
  assert.equal(context.acc().pending, 2);
  presetFrame(delta({activeId: 5}, 48));
  assert.equal(context.ps().activeId, 1, 'a non-matching frame stays deferred');
  settle.splice(0).forEach(done => done(false));
  await failing;
  assert.equal(context.ps().activeId, 5, 'a failed apply falls back to the server state');
  assert.equal(context.ps().selectedId, 5);
  assert.equal(context.acc().pending, 0);
  assert.deepEqual(commands.map(c => ({action: c.value.action, id: c.value.id})), [
    {action: 'apply', id: 2}, {action: 'apply', id: 1}, {action: 'apply', id: 2}]);
  // Static firmware contracts for the same wire shape (D20): per-slot source
  // hashes keyed by preset id, the ordered ids list as the structural truth,
  // per-session sent records seeded only on delivery, and the whole-bank
  // renderer replaced by the per-item slim render.
  const homeSource = fs.readFileSync(path.join(sketchDir, 'network/OpenBrewByWeightHomeStream.inc'), 'utf8');
  const networkSource = fs.readFileSync(path.join(sketchDir, 'OpenBrewByWeightNetwork.cpp'), 'utf8');
  const homePresets = homeSource.slice(homeSource.indexOf('if (callbacks_.copyPresetBank)'),
      homeSource.indexOf('delta.hashedField("presets"'));
  assert(homePresets.includes('sent.presetSlotHashes[at] == slotHash[i]'),
      'a preset this page already holds is skipped by id, even after a slot shift');
  assert(homePresets.includes('\\"ids\\":[') && homePresets.includes('\\"items\\":[') &&
      homePresets.includes('\\"activeId\\":') && homePresets.includes('\\"cutRampS\\":'),
      'the presets envelope carries scalars, changed items, and the ordered ids');
  assert(homeSource.indexOf('sent.presetSentCount = presetCount;') >
      homeSource.indexOf('if (deferred) {'),
      'the per-session preset record seeds on delivery, after the deferred-frame arm');
  assert(networkHeader.includes('presetSlotHashes[MAX_SHOT_PRESETS]') &&
      networkHeader.includes('presetSentIds[MAX_SHOT_PRESETS]'),
      'per-session preset records live in the fingerprint table');
  assert(networkSource.includes('int formatSlimPresetJson(char *out, size_t cap, const ShotPreset &p,') &&
      !networkSource.includes('buildSlimPresetsJson'),
      'the per-item slim render replaces the whole-bank builder');
  assert(codeIncludes(runtimeJs, 'function mergePresetsFragment(pr)'),
      'the client merges sparse presets fragments by id');
}

// Execute the firmware's bounded field writer itself, including unchanged,
// single-field, disconnect, resync and capacity-failure cases.
{
  const assert = require('assert').strict, {spawnSync} = require('child_process');
  const stream = fs.readFileSync(path.join(sketchDir, 'network/OpenBrewByWeightHomeStream.inc'), 'utf8');
  const writer = stream.slice(0, stream.indexOf('// This bounded presentation'));
  const network = fs.readFileSync(path.join(sketchDir, 'OpenBrewByWeightNetwork.h'), 'utf8');
  const networkTypes = fs.readFileSync(path.join(sketchDir, 'OpenBrewByWeightNetworkTypes.h'), 'utf8');
  const signal = networkTypes.slice(networkTypes.indexOf('// Signal bars'), networkTypes.lastIndexOf('}  // namespace')) +
      network.slice(network.indexOf('inline const char *connectionSignalLevelJson('), network.indexOf('inline uint8_t wifiRssiToSignalQualityPct('));
  const domainSource = fs.readFileSync(path.join(sketchDir, 'OpenBrewByWeightDomain.h'), 'utf8');
  const crc = domainSource.slice(domainSource.indexOf('inline uint32_t crc32Update('), domainSource.indexOf('inline uint32_t crc32('));
  const directory = path.resolve(sketchDir, '..', 'temp', 'ai_temp_home_websocket_contract');
  fs.mkdirSync(directory, {recursive: true});
  const binary = path.join(directory, 'field-writer-' + process.pid);
  const native = `
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>
struct NetworkWorkBuf { static constexpr size_t kJsonItem=3600,kStatusJson=40960; char jsonItem[kJsonItem],statusJson[kStatusJson]; } work;
auto *g_work=&work;
bool statusJsonAppend(size_t *used,const char *fmt,...){
  va_list args;va_start(args,fmt);int n=vsnprintf(work.statusJson+*used,NetworkWorkBuf::kStatusJson-*used,fmt,args);va_end(args);
  if(n<0||size_t(n)>=NetworkWorkBuf::kStatusJson-*used)return false;*used+=n;return true;
}
${crc}
${writer}
${signal}
int main(){
  uint32_t hashes[3]={};
  auto sample=[&](bool initial,bool connected,double weight,unsigned timer){
    HomeStreamDelta delta{hashes,3,0,0,0,initial};
    delta.field("scale.available",connected);
    if(connected)delta.field("scale.observedWeightG",float(weight));else delta.field("scale.observedWeightG","null");
    if(connected)delta.field("scale.timerMs","%u",timer);else delta.field("scale.timerMs","null");
    assert(delta.ok);return delta.changed;
  };
  assert(sample(true,true,12,1000)==3);
  for(int i=0;i<100;++i)assert(sample(false,true,12,1000)==0);
  assert(sample(false,true,12.1,1000)==1);assert(strstr(work.statusJson,"scale.observedWeightG")&&!strstr(work.statusJson,"scale.timerMs"));
  assert(sample(false,true,12.1,1100)==1);assert(strstr(work.statusJson,"scale.timerMs")&&!strstr(work.statusJson,"scale.observedWeightG"));
  assert(sample(false,false,99,9900)==3);assert(sample(false,false,100,9999)==0);
  assert(sample(true,false,100,9999)==3);
  HomeStreamDelta full{hashes,0,0,0,0,true};full.field("extra","null");assert(!full.ok);
  {
    // skipNext keeps a field off the frame but records its fingerprint.
    uint32_t tags[2]={};
    HomeStreamDelta kept{tags,2,0,0,0,true};kept.skipNext=true;
    kept.field("presets","%s","[1]");kept.field("presetsTag","%u",7u);
    assert(kept.ok&&kept.changed==1&&!strstr(work.statusJson,"[1]")&&tags[0]!=0);
    HomeStreamDelta next{tags,2,0,0,0,false};next.field("presets","%s","[1]");next.field("presetsTag","%u",7u);
    assert(next.ok&&next.changed==0);
  }
  HomeStreamDelta overflow{hashes,3,0,NetworkWorkBuf::kStatusJson-1,0,true};overflow.field("extra","null");assert(!overflow.ok);
  char huge[NetworkWorkBuf::kJsonItem+1];memset(huge,'x',sizeof(huge)-1);huge[sizeof(huge)-1]=0;
  HomeStreamDelta large{hashes,3,0,0,0,true};large.field("extra","%s",huge);assert(!large.ok);
  for(const char *path:{"connections.wifiLevel","connections.bluetoothLevel"}){
    uint8_t level=0;
    auto link=[&](bool initial,bool connected,bool valid,int rssi){
      level=connectionSignalLevel(level,valid,static_cast<int8_t>(rssi));
      HomeStreamDelta delta{hashes,3,0,0,0,initial};
      delta.field("connected",connected);
      delta.field(path,"%s",connectionSignalLevelJson(level));
      assert(delta.ok);return delta.changed;
    };
    assert(link(true,true,true,-44)==2&&level==3);
    for(int rssi=-45;rssi>=-63;--rssi)assert(link(false,true,true,rssi)==0);
    assert(link(false,true,true,-64)==1&&level==2);
    // A reading oscillating on the -60 boundary keeps its level both ways.
    for(int i=0;i<50;++i)assert(link(false,true,true,i%2?-61:-59)==0&&level==2);
    assert(link(false,true,true,-57)==1&&level==3);
    for(int rssi=-58;rssi>=-63;--rssi)assert(link(false,true,true,rssi)==0);
    assert(link(false,true,true,-84)==1&&level==1);
    for(int i=0;i<50;++i)assert(link(false,true,true,i%2?-81:-79)==0&&level==1);
    assert(link(false,true,true,-77)==1&&level==2);
    assert(link(false,true,true,-128)==1&&level==1);
    assert(link(false,true,true,127)==1&&level==0);assert(link(false,true,true,1)==0);
    assert(link(false,true,true,-60)==1&&level==3);assert(link(false,true,false,-52)==1);
    assert(link(false,true,false,-80)==0);assert(link(false,false,false,-52)==1);
    assert(link(false,false,false,-128)==0);assert(link(false,true,true,-52)==2);
    assert(link(true,true,true,-52)==2);
  }
  // A first reading (no previous level) maps without hysteresis.
  for(const auto &test:{std::pair<int,const char *>{127,"null"},{-128,"1"},{-81,"1"},
                       {-80,"2"},{-61,"2"},{-60,"3"},{0,"3"},{1,"null"}})
    assert(strcmp(connectionSignalLevelJson(connectionSignalLevel(0,true,static_cast<int8_t>(test.first))),test.second)==0);
  assert(strcmp(connectionSignalLevelJson(connectionSignalLevel(3,false,-40)),"null")==0);
}
`;
  try {
    const compiled = spawnSync(process.env.CXX || 'c++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-x', 'c++', '-', '-o', binary], {input: native, encoding: 'utf8'});
    assert.equal(compiled.status, 0, compiled.error?.message || compiled.stderr);
    const run = spawnSync(binary, [], {encoding: 'utf8'});
    assert.equal(run.status, 0, run.error?.message || run.stderr);
  } finally { fs.rmSync(binary, {force: true}); }
  assert(stream.includes('control.scaleAvailable && control.observedWeightValid'));
  assert(stream.includes('control.scaleAvailable && control.currentTimerValid'));
  assert(stream.includes('if (!delta.changed) return true;'));
  // Only snapshots name v/boot/snapshot; increments carry type and changes.
  assert(stream.includes('statusJsonAppend(&delta.used, "{\\"type\\":\\"home\\",\\"changes\\":{")'));
  assert(stream.includes('statusJsonAppend(&delta.used, "{\\"type\\":\\"diagnostic\\",\\"changes\\":{")'));
  assert(!/"snapshot\\":false/.test(stream), 'no stream frame spells out snapshot:false');
  assert(!stream.includes('connections.wifiRssi') && !stream.includes('connections.bluetoothRssi'));
  for (const kind of ['wifi', 'bluetooth']) assert(stream.includes('connections.' + kind + 'Level'));
  assert(stream.includes('frame.payload = reinterpret_cast<uint8_t *>(g_work->statusJson)'));
  const homeFieldSlots = Number(/kHomeFields\s*=\s*(\d+)/.exec(network)[1]);
  // The diagnostic projection in this file carries its own slot budget.
  const homeFieldCalls =
      (stream.slice(0, stream.indexOf('sendDiagnosticStream')).match(/\bdelta\.field\(/g) || []).length;
  assert(homeFieldCalls > 0 && homeFieldCalls <= homeFieldSlots,
      `Home projection ${homeFieldCalls} fields exceeds ${homeFieldSlots} fingerprint slots`);
}
{
  const handshake = network.slice(
      network.indexOf('esp_err_t OpenBrewByWeightNetwork::uiStreamHandshake'),
      network.indexOf('for (auto &session : self.uiStreams_)'));
  if (!handshake.includes('httpd_req_get_hdr_value_str(request, "Host", host, sizeof(host))') ||
      !handshake.includes('!uiStreamOriginMatches(origin, host) ||') ||
      !handshake.includes('!webAccessHostAllowed(host, network.mdnsHost)') ||
      handshake.includes('network.iface.staIp') ||
      !handshake.includes('logRefusedHost(origin, network.mdnsHost)') ||
      !network.includes('constexpr size_t UI_STREAM_ORIGIN_CAPACITY = 128;')) {
    throw new Error('UI stream must accept only a same-origin page whose host names this ' +
                    'controller (the HTTP Host allowlist) and log a rejected Origin');
  }
}

// A page hidden for 5 minutes drops its stream and reconnects on return; a
// shorter absence keeps the socket with only a liveness ping, a live shot
// defers the close, and none of it touches the 15-minute inactivity timer.
{
  const assert = require('assert').strict, vm = require('vm');
  const source = runtimeStateDecl + runtimeJs.slice(runtimeJs.search(/let\s+homeFrame\s*=/),
    runtimeJs.indexOf('function setHomeSub(')).replace(/export /g, '');
  const sockets = [], timers = new Map(), listeners = {}, windowListeners = {};
  let timer = 0, clock = 1000000, perf = 5000, owner = true, live = false, inactivityCalls = 0, reachFails = 0,
    mutableOff = 0, resets = 0;
  class Socket {
    static OPEN = 1;
    constructor() { this.readyState = 1; this.sent = []; sockets.push(this); }
    send(value) { this.sent.push(JSON.parse(value)); }
    close(code = 1006) { if (this.readyState === 3) return; this.readyState = 3; this.onclose?.({code}); }
  }
  const document = {hidden: false, addEventListener: (name, fn) => { listeners[name] = fn; }};
  const context = {
    WebSocket: Socket, location: {protocol: 'http:', host: 'device.local'}, document,
    window: {addEventListener: (name, fn) => { windowListeners[name] = fn; }}, Date: {now: () => clock},
    setTimeout: (fn, delay) => { timers.set(++timer, {fn, delay}); return timer; },
    clearTimeout: id => timers.delete(id), requestAnimationFrame: () => 1,
    webUiPollingActive: () => owner, webUiClientId: '0123456789abcdef', webUiPowerSeconds: () => 30,
    sessionShotLive: () => live, shotFsActive: false, keepShotFsAwake() {},
    resetWebUiInactivity: () => { inactivityCalls++; }, deactivateWebUi: () => { inactivityCalls++; owner = false; },
    activeView: 'settings', viewSeq: 1, viewReady: Promise.resolve(), lastStatusAt: 0,
    performance: {now: () => perf}, statusUtcAnchorSec: 0, statusUtcAnchorAt: 0, statusLiveShot: false,
    viewStatusHandlers: {}, applyCommonStatus() {}, setMutable: on => { if (!on) mutableOff++; }, noteReachOk() {},
    noteReachFail: () => { reachFails++; },
    hideHomeBoot() {}, clearCupWeights() {}, updateHeaderSignals() {}, updateHomeAdminActions() {},
    setHomeSub() {}, $: () => null, filterResets: [() => resets++],
  };
  vm.runInNewContext('var setupSession=false;' + source + ';this.setSetup=v=>{setupSession=v};this.ui=()=>uiState;' +
      'this.anchor=v=>{if(v!==undefined)statusUtcAnchorAt=v;return statusUtcAnchorAt};', context);
  const visibility = hidden => { document.hidden = hidden; listeners.visibilitychange(); };
  const closeTimer = () => [...timers].find(([, t]) => t.delay === 300000);
  context.startUiStream(); sockets[0].onopen();
  // Short absence: the socket stays and only the liveness ping goes out.
  visibility(true); assert(closeTimer(), 'hiding arms the 5-minute close');
  clock += 299000; visibility(false);
  assert(!closeTimer() && sockets.length === 1 && sockets[0].readyState === 1);
  assert.equal(sockets[0].sent.at(-1).op, 'activity');
  assert(!sockets[0].sent.some(op => op.op === 'resync'), 'an open socket needs no snapshots');
  // The timer closes a page still hidden after 5 minutes, without reconnecting.
  visibility(true); closeTimer()[1].fn();
  assert.equal(sockets[0].readyState, 3);
  assert(![...timers.values()].some(t => t.fn === context.startUiStream), 'no reconnect while hidden');
  clock += 300000; visibility(false);
  assert.equal(sockets.length, 2, 'the return reconnects');
  sockets[1].onopen();
  // Frozen timers (a locked phone): the return itself closes and reconnects.
  visibility(true); timers.clear(); clock += 360000; visibility(false);
  assert(sockets[1].readyState === 3 && sockets.length === 3);
  sockets[2].onopen();
  // A live shot defers the close and rechecks it every 30 s.
  live = true; visibility(true); closeTimer()[1].fn();
  assert(sockets[2].readyState === 1 && [...timers.values()].some(t => t.delay === 30000),
      'a live shot keeps the stream');
  clock += 360000; visibility(false);
  assert(sockets[2].readyState === 1 && sockets.length === 3, 'returning during a live shot keeps it too');
  live = false;
  // A page the inactivity timer deactivated stays deactivated.
  visibility(true); closeTimer()[1].fn(); owner = false; clock += 360000; visibility(false);
  assert.equal(sockets.length, 3, 'an inactive page does not reconnect');
  assert.equal(inactivityCalls, 0, 'the hidden close never touches the inactivity deactivation');
  // A setup session arms nothing and never loses its controls to a hidden close.
  owner = true; context.setSetup(true); timers.clear();
  const offBefore = mutableOff;
  visibility(true); assert(!closeTimer(), 'a setup session arms no hidden close');
  clock += 360000; visibility(false);
  assert.equal(mutableOff, offBefore, 'a hidden setup page keeps its controls');
  context.setSetup(false);
  // The firmware's 4002 close reconnects quietly when visible and waits when hidden.
  context.startUiStream(); const quiet = sockets.at(-1); quiet.onopen();
  const fails = reachFails, count = sockets.length;
  quiet.close(4002);
  assert(sockets.length === count + 1 && reachFails === fails, '4002 reconnects without the out-of-reach overlay');
  sockets.at(-1).onopen();
  document.hidden = true; sockets.at(-1).close(4002);
  assert.equal(sockets.length, count + 1, 'a hidden page waits for its return');
  document.hidden = false; listeners.visibilitychange();
  assert.equal(sockets.length, count + 2);
  sockets.at(-1).onopen();
  // A suspended page whose monotonic clock stood still moves the wall-clock anchor.
  context.anchor(4000); visibility(true); clock += 120000; perf += 1000; visibility(false);
  assert.equal(context.anchor(), 4000 - 119000, 'the anchor absorbs the missed sleep');
  context.anchor(4000); visibility(true); clock += 60000; perf += 60000; visibility(false);
  assert.equal(context.anchor(), 4000, 'a running clock keeps the anchor');
  // A page restored by pageshow starts once; its visibility return does not redo it.
  visibility(true); clock += 360000; windowListeners.pagehide?.();
  const restored = sockets.length; windowListeners.pageshow(); visibility(false);
  assert.equal(sockets.length, restored + 1, 'one socket after a restore');
  // 4003 (an owner op failed validation) clears the list views and retries
  // with backoff, without the out-of-reach overlay.
  sockets.at(-1).onopen(); const rejectedFails = reachFails; context.ui().statsWindow = 50;
  sockets.at(-1).close(4003);
  assert(resets === 1 && context.ui().statsWindow === 10 && reachFails === rejectedFails &&
      [...timers.values()].some((t) => t.fn === context.startUiStream), '4003 resets the list views and retries');
  // The firmware closes a bound session silent for 5 minutes, outside a live
  // cycle, with a plain close (not the 4001 takeover) and one log line.
  const stream = fs.readFileSync(path.join(sketchDir, 'network/OpenBrewByWeightUiStream.inc'), 'utf8');
  assert(stream.includes('const bool live = control.activeCycle || control.homePending || control.relayClosed;') &&
      stream.includes('silent = allowed && session.bound && !live &&') &&
      stream.includes('millis() - session.heardAtMs >= kUiStreamSilentMs;'));
  // A hidden page pauses its frames outside a live shot (owed snapshots still
  // go out), and an accepted owned-API request also renews its silence timer.
  assert(stream.includes('if (!session.paused || live || session.homeResync || session.resync)') &&
      stream.includes('resumed = session->paused && cJSON_IsFalse(hidden);') &&
      stream.includes('if (resumed) self.uiStreamUrgent_.store(true, std::memory_order_release);'));
  assert(fs.readFileSync(path.join(sketchDir, 'network/OpenBrewByWeightHttpAuthAssets.inc'), 'utf8')
      .includes('if (active && session.bound && strcmp(session.clientId, clientId) == 0)'));
  assert(stream.includes('if (accepted) session->heardAtMs = millis();'));
  const silentBranch = stream.slice(stream.indexOf('if (silent) {'), stream.indexOf('} else if (!allowed) {'));
  assert(silentBranch.includes('kUiStreamCloseSilent') && !silentBranch.includes('kUiStreamCloseTakeover'));
  assert(stream.includes('constexpr uint32_t kUiStreamSilentMs = 330000;') &&
      stream.includes('silent = allowed && session.bound &&'));
  // Bind reports the presets the page holds; only the first Home snapshot may
  // leave them out, and only for the same tag.
  const home = fs.readFileSync(path.join(sketchDir, 'network/OpenBrewByWeightHomeStream.inc'), 'utf8');
  assert(stream.includes('cJSON_GetObjectItemCaseSensitive(message, "presetsTag")'));
  assert(home.includes('delta.skipNext = initial && session.knownPresetsTag != 0 &&') &&
      home.includes('session.knownPresetsTag == renderedPresetsHash;') &&
      home.indexOf('session.knownPresetsTag = 0;') < home.indexOf('delta.hashedField("presets"'));
}
