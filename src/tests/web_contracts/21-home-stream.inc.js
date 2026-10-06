// Home uses the existing owned socket, with independent scalar and curve state.
{
  const assert = require('assert').strict, vm = require('vm');
  const source = runtimeJs.slice(runtimeJs.indexOf('let homeFrame='),
    runtimeJs.indexOf('function formatExtractionGuard(')).replace(/export /g, '');
  const validator = runtimeJs.slice(runtimeJs.indexOf('function statusPageOk('),
    runtimeJs.indexOf('async function loadStatus('));
  const sockets = [], timers = new Map(), rendered = [], common = [];
  let timer = 0, mutable = false, owner = true;
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
    setMutable: value => { mutable = value; }, noteReachOk() {}, noteReachFail() {}, hideHomeBoot() {},
    clearCupWeights() {}, updateHeaderSignals() {}, updateHomeAdminActions() {}, setHomeSub() {},
    deactivateWebUi: () => { owner = false; }, $: () => null,
  };
  vm.runInNewContext(validator + source, context);
  const status = {firmwareVersion: 'test', bootId: 7, configMutable: true, timeUtcSec: 1700000000,
    adminUnlocked: false, snapshotStale: false, config: {soundAlertsEnabled: true, revision: 1},
    safety: {state: 'READY'}, scale: {available: true, streamState: 'FRESH', observedWeightG: 12, timerMs: 1000},
    presets: {activeId: 1, items: []}, cycle: {active: false}, lastShot: {valid: false},
    noScaleShotGuard: {enabled: true}, machineState: 'CONFIRMED_OFF', cupPresence: {present: false}};
  const flatten = (value, prefix = '', output = {}) => {
    for (const [key, item] of Object.entries(value)) {
      const name = prefix + key;
      if (item && typeof item === 'object' && !Array.isArray(item) && key !== 'presets') flatten(item, name + '.', output);
      else output[name] = item;
    }
    return output;
  };
  const initial = {v: 1, type: 'home', boot: 7, snapshot: true, changes: flatten(status)};
  const first = context.homeStreamFrame(null, initial);
  const patch = {v: 1, type: 'home', boot: 7, snapshot: false,
    changes: {'scale.observedWeightG': 12.1, 'machineState': 'CONFIRMED_ON'}};
  const second = context.homeStreamFrame(first, patch);
  assert.equal(second.status.scale.timerMs, 1000);
  assert.equal(second.status.scale.observedWeightG, 12.1);
  assert.equal(first.status.scale.observedWeightG, 12.1, 'Home applies scalar patches to its current cache');
  assert.equal(context.homeStreamFrame(second, patch).status.scale.observedWeightG, 12.1, 'scalar assignments are idempotent');
  const lost = context.homeStreamFrame(second, {...patch,
    changes: {'scale.available': false, 'scale.observedWeightG': null, 'scale.timerMs': null}});
  assert.equal(lost.status.scale.timerMs, null);
  for (const bad of [{...patch, boot: 8},
    {...patch, changes: {'scale.timerMs': Infinity}},
    {...patch, changes: {'config.constructor.prototype': {poisoned: true}}},
    {...initial, changes: {'scale.available': true}}]) {
    assert.throws(() => context.homeStreamFrame(first, bad), 'invalid Home messages require resync');
  }
  assert.equal(context.homeStreamFrame(first, {...initial, boot: 8, changes: {...initial.changes, bootId: 8}}).boot, 8);
  context.startShotStream(); sockets[0].onopen();
  assert.equal(sockets[0].sent[0].op, 'bind');
  assert.equal(sockets[0].sent[1].op, 'activity', 'liveness is a socket control message, not REST polling');
  sockets[0].onmessage({data: JSON.stringify({v: 1, type: 'alive'})});
  sockets[0].onmessage({data: JSON.stringify(initial)});
  assert(mutable); assert.equal(rendered.at(-1).scale.observedWeightG, 12);
  assert.equal(context.statusUtcAnchorSec, status.timeUtcSec);
  assert.equal(context.statusUtcAnchorAt, 1234);
  assert.equal(rendered.at(-1).timeUtcSec, undefined, 'cached Home status must not reset the shared clock anchor');
  sockets[0].onmessage({data: JSON.stringify(patch)});
  assert.equal(rendered.at(-1).machineState, 'CONFIRMED_ON');
  vm.runInNewContext("activeView='settings'", context);
  context.statusUtcAnchorSec += 10;
  const paints = rendered.length;
  sockets[0].onmessage({data: JSON.stringify({...patch, changes: {'config.revision': 2}})});
  assert.equal(rendered.length, paints, 'off-Home deltas must not change another page or its edit gate');
  vm.runInNewContext("activeView='home';renderHomeStream()", context);
  assert.equal(rendered.at(-1).config.revision, 2, 'navigation must render the updated cache');
  assert.equal(context.statusUtcAnchorSec, status.timeUtcSec + 10, 'Home must retain the current clock after another page updates it');
  sockets[0].close(); assert.equal(mutable, false, 'stream loss must disable Home controls');
  context.startShotStream(); sockets[1].onopen();
  sockets[1].onmessage({data: JSON.stringify(initial)});
  assert.equal(rendered.at(-1).scale.observedWeightG, 12, 'reconnect accepts an initial snapshot');
  sockets[1].onmessage({data: JSON.stringify({...patch, boot: 8})});
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
  assert(runtimeJs.includes("s=v==='home'?await loadHomeStatus():await api("));
  const rest = fs.readFileSync(path.join(sketchDir, 'network/ShotStopperStatus.inc'), 'utf8');
  const metadata = rest.slice(rest.indexOf('if (page == StatusPage::Home)'), rest.indexOf('// JSON / snapshot'));
  assert(metadata.includes('return sendJson')); assert(!metadata.includes('loadControlStatus'));
  for (const legacy of ['homeLastShot', 'homeHistoryLinked', 'safeHomeShot', '"\\"shotCurve']) assert(!rest.includes(legacy));
  const formatCleaning = context.formatMicraCleaning;
  const lm = {accountConfigured: true, observeState: true, connectionType: 'websocket', websocket: {cleaningAvailable: true}};
  for (const [cleaning, expected] of [['inactive', 'Inactive'], ['waiting_for_paddle', 'Waiting for paddle'], ['cleaning', 'Cleaning in progress']])
    assert.equal(formatCleaning({...lm, websocket: {...lm.websocket, cleaning}}), expected);
  assert.equal(formatCleaning({...lm, connectionType: 'api'}), 'Unavailable — requires WebSocket');
  assert.equal(formatCleaning({...lm, observeState: false}), 'Disabled');
  assert(viewJs.diagnostic.includes("set('Cleaning',R.formatMicraCleaning(lm))"));
  assert(viewJs.home.includes('R.formatMicraCleaning(s.lineaMicra)'));
}

// Execute the firmware's bounded field writer itself, including unchanged,
// single-field, disconnect, resync and capacity-failure cases.
{
  const assert = require('assert').strict, {spawnSync} = require('child_process');
  const stream = fs.readFileSync(path.join(sketchDir, 'network/ShotStopperHomeStream.inc'), 'utf8');
  const writer = stream.slice(0, stream.indexOf('// This bounded presentation'));
  const domainSource = fs.readFileSync(path.join(sketchDir, 'ShotStopperDomain.h'), 'utf8');
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
struct NetworkWorkBuf { static constexpr size_t kJsonItem=3600,kStatusJson=40960; char jsonItem[kJsonItem],statusJson[kStatusJson]; } work;
auto *g_work=&work;
bool statusJsonAppend(size_t *used,const char *fmt,...){
  va_list args;va_start(args,fmt);int n=vsnprintf(work.statusJson+*used,NetworkWorkBuf::kStatusJson-*used,fmt,args);va_end(args);
  if(n<0||size_t(n)>=NetworkWorkBuf::kStatusJson-*used)return false;*used+=n;return true;
}
${crc}
${writer}
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
  HomeStreamDelta overflow{hashes,3,0,NetworkWorkBuf::kStatusJson-1,0,true};overflow.field("extra","null");assert(!overflow.ok);
  char huge[NetworkWorkBuf::kJsonItem+1];memset(huge,'x',sizeof(huge)-1);huge[sizeof(huge)-1]=0;
  HomeStreamDelta large{hashes,3,0,0,0,true};large.field("extra","%s",huge);assert(!large.ok);
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
  assert(stream.includes('frame.payload = reinterpret_cast<uint8_t *>(g_work->statusJson)'));
}
