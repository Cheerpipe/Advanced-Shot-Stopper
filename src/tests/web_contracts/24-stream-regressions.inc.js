// Compile the actual delivery helper and log producer. A source-token check
// cannot catch valid escaping embedded in invalid JSON, or a stale retry flag.
{
  const assert = require('assert').strict, {spawnSync} = require('child_process');
  const stream = fs.readFileSync(path.join(sketchDir, 'network/ShotStopperHomeStream.inc'), 'utf8');
  const webhook = fs.readFileSync(path.join(sketchDir, 'ShotStopperWebhook.h'), 'utf8');
  const networkSource = fs.readFileSync(path.join(sketchDir, 'ShotStopperNetwork.cpp'), 'utf8');
  const body = (source, signature) => {
    const start = source.indexOf(signature);
    assert(start >= 0, signature);
    return source.slice(start, source.indexOf('\n}', start) + 2);
  };
  const directory = path.resolve(sketchDir, '..', 'temp', 'ai_temp_websocket_contract');
  fs.mkdirSync(directory, {recursive: true});
  const binary = path.join(directory, 'delivery-' + process.pid);
  const statsSender = body(stream, 'bool ShotStopperNetwork::sendStatsStream(');
  const selectStats = statsSender.slice(statsSender.indexOf('  const bool initial ='),
      statsSender.indexOf('  if (!session.statsPaging && !initial'));
  const completeStats = statsSender.slice(statsSender.lastIndexOf('  session.statsPaging = false;'),
      statsSender.lastIndexOf('  return true;'));
  const native = `
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include "ShotStopperDomain.h"
using namespace shotstopper;
constexpr int ESP_OK=0, HTTPD_WS_TYPE_TEXT=1;
constexpr size_t LOG_BATCH_SIZE=32, kUiStreamFrameBudget=20480;
const char *FW_VERSION="test";
struct NetworkWorkBuf {
  static constexpr size_t kJsonItem=24576,kStatusJson=40960;
  char jsonItem[kJsonItem]{},statusJson[kStatusJson]{};
  DebugEvent logBatch[LOG_BATCH_SIZE]{};
  DebugLogReadMetadata logMetadata{};
} work;
auto *g_work=&work;
struct UiStreamSession {
  int fd=1; char clientId[8]="owner"; bool logPush=true; uint32_t logAfter=0;
  bool statsResync=true,statsFetch=false,statsPaging=false,statsPageFetch=false;
  uint32_t statsBoot=0,statsRequest=1,statsFetchRequest=2,statsPageRequest=0;
  uint32_t statsPageEpoch=0,statsEpoch=0,statsFingerprint=0;
};
struct TaskLockGuard { explicit TaskLockGuard(int){} };
struct httpd_ws_frame_t { int type=0; uint8_t *payload=nullptr; size_t len=0; };
bool uiStreamSendCleanAbort=false;
int sendResult=ESP_OK;
std::vector<std::string> delivered;
int httpd_ws_send_frame_async(int,int,httpd_ws_frame_t *frame){
  if(sendResult==ESP_OK)delivered.emplace_back(reinterpret_cast<char *>(frame->payload),frame->len);
  return sendResult;
}
DebugRingBuffer ring;
struct ShotStopperNetwork {
  int dataMux_=0,server_=1; char activeWebUiClientId_[8]="owner";
  struct {
    uint32_t (*debugLogLatestSequence)()=+[](){return ring.latestSequence();};
    size_t (*copyDebugEvents)(uint32_t,DebugEvent*,size_t,DebugLogReadMetadata*)=
      +[](uint32_t cursor,DebugEvent *events,size_t capacity,DebugLogReadMetadata *metadata){return ring.copyAfter(cursor,events,capacity,metadata);};
  } callbacks_;
  bool sendUiStreamFrame(UiStreamSession &,size_t,bool &);
  bool sendLogStream(UiStreamSession &,const ControlStatusSnapshot &);
};
${body(networkSource, 'bool __attribute__((format(printf, 2, 3)))\nstatusJsonAppend(')}
${body(webhook, 'inline bool escapeJsonString(')}
${body(stream, 'bool ShotStopperNetwork::sendUiStreamFrame(')}
${body(stream, 'bool ShotStopperNetwork::sendLogStream(')}
bool selectStatsFetch(UiStreamSession &session,const ControlStatusSnapshot &control,uint32_t epoch){
${selectStats}
  return fetch;
}
void completeStatsPage(UiStreamSession &session,bool fetch,uint32_t epoch,uint32_t fingerprint){
${completeStats}
}
int main(){
  ShotStopperNetwork network; UiStreamSession session; bool retry=true;
  strcpy(work.statusJson,"{}");
  assert(network.sendUiStreamFrame(session,2,retry)&&!retry);
  sendResult=-1;uiStreamSendCleanAbort=true;
  assert(network.sendUiStreamFrame(session,2,retry)&&retry);
  sendResult=ESP_OK;
  assert(network.sendUiStreamFrame(session,2,retry)&&!retry);
  sendResult=-1;uiStreamSendCleanAbort=false;
  assert(!network.sendUiStreamFrame(session,2,retry));
  sendResult=ESP_OK;strcpy(session.clientId,"other");
  assert(!network.sendUiStreamFrame(session,2,retry));
  strcpy(session.clientId,"owner");delivered.clear();
  ControlStatusSnapshot control{};control.bootId=193;
  // Subscribe, then export before dispatch: neither snapshot preparation nor
  // standing-page completion may silently cancel the accepted export.
  session.statsFetch=true;
  assert(!selectStatsFetch(session,control,5)&&session.statsFetch);
  session.statsResync=false;session.statsBoot=193;session.statsPaging=true;
  session.statsPageEpoch=5;session.statsPageRequest=1;
  assert(!selectStatsFetch(session,control,5)&&session.statsPaging);
  completeStatsPage(session,false,5,42);
  assert(session.statsFetch&&selectStatsFetch(session,control,5));
  session.statsPaging=true;session.statsPageFetch=true;session.statsPageRequest=2;
  assert(selectStatsFetch(session,control,5)&&session.statsPaging);
  session.statsFetchRequest=3;
  assert(selectStatsFetch(session,control,5)&&!session.statsPaging);
  completeStatsPage(session,true,5,99);
  assert(!session.statsFetch&&session.statsFingerprint==42);
  const char *texts[]={"NimBLE runtime state=2", "quotes \\\" slash \\\\ newline\\n", "", "café"};
  for(const auto *text:texts)ring.add(10,0,LogLevel::INFO,DebugCategory::SYSTEM,DebugCode::BOOT_BANNER,1,2,text);
  assert(network.sendLogStream(session,control));
  assert(session.logAfter==4&&!session.logPush);
  assert(network.sendLogStream(session,control)&&delivered.size()==1);
  for(const auto &frame:delivered)puts(frame.c_str());
  session.logAfter=100;session.logPush=true;delivered.clear();
  assert(network.sendLogStream(session,control)&&session.logAfter==0);
  assert(network.sendLogStream(session,control)&&session.logAfter==4);
  for(const auto &frame:delivered)puts(frame.c_str());
  ring.clear();session.logAfter=0;session.logPush=true;delivered.clear();
  char large[128];memset(large,1,127);large[127]=0;
  for(int i=0;i<32;++i)ring.add(10,0,LogLevel::INFO,DebugCategory::SYSTEM,DebugCode::BOOT_BANNER,0,0,large);
  sendResult=-1;uiStreamSendCleanAbort=true;
  assert(network.sendLogStream(session,control)&&session.logAfter==0&&session.logPush);
  sendResult=ESP_OK;
  while(session.logPush){assert(network.sendLogStream(session,control));}
  assert(session.logAfter==32&&delivered.size()>1);
  for(const auto &frame:delivered){assert(frame.size()<=kUiStreamFrameBudget);puts(frame.c_str());}
}
`;
  try {
    const compiled = spawnSync(process.env.CXX || 'c++', ['-std=c++17', '-Wall', '-Wextra', '-Werror',
      '-DSHOT_STOPPER_HOST_TEST=1', '-I', sketchDir, '-x', 'c++', '-', '-o', binary], {input: native, encoding: 'utf8'});
    assert.equal(compiled.status, 0, compiled.error?.message || compiled.stderr);
    const result = spawnSync(binary, [], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.error?.message || result.stderr);
    const frames = result.stdout.trim().split('\n').map(line => JSON.parse(line));
    assert.equal(frames[0].events[0].message, 'NimBLE runtime state=2');
    assert.equal(frames[0].events[3].message, 'café');
    assert(frames[1].cursorInvalid);
    assert.equal(frames[2].events.length, 4);
    assert(frames[3].hasMore, 'budget-limited log frames must advertise the remaining backlog');
    assert.equal(frames.at(-1).hasMore, false);
    assert.equal(frames.slice(3).flatMap(frame => frame.events).length, 32);
  } finally { fs.rmSync(binary, {force: true}); }
}

// Replay the real diagnostic assembly/lifecycle against an asynchronous socket.
{
  const assert = require('assert').strict, vm = require('vm');
  const sockets=[],timers=new Map(),paints=[];let nextTimer=0;
  class Socket {
    static OPEN=1;
    constructor(){this.readyState=0;this.sent=[];sockets.push(this);}
    send(frame){this.sent.push(JSON.parse(frame));}
    close(){this.readyState=3;this.onclose?.({});}
  }
  const context=vm.createContext({WebSocket:Socket,location:{protocol:'http:',host:'device.local'},
    document:{hidden:false,addEventListener(){}},window:{addEventListener(){}},
    setTimeout:(fn,delay)=>{timers.set(++nextTimer,{fn,delay});return nextTimer;},clearTimeout:id=>timers.delete(id),
    requestAnimationFrame:()=>1,webUiPollingActive:()=>true,webUiClientId:'owner',webUiPowerSeconds:()=>0,
    activeView:'diagnostic',viewReady:Promise.resolve(),homeResolve(){},invalidateHomeStream(){},noteReachFail(){},
    applyDiagnosticLive(){},viewStatusHandlers:{diagnostic:s=>{assert(s.health);assert(s.tasks);paints.push(s);}}});
  vm.runInContext(runtimeJs.slice(runtimeJs.indexOf('function statusStreamFrame('),runtimeJs.indexOf('function renderHomeStream('))+
      runtimeJs.slice(runtimeJs.indexOf('let shotWs='),runtimeJs.indexOf('function formatExtractionGuard(')),context);
  (async()=>{
    let settled=false;
    const pending=context.loadDiagnosticStatus().then(value=>{settled=true;return value;});
    const socket=sockets[0];socket.readyState=1;socket.onopen();
    await Promise.resolve();assert(!settled,'opening the socket must not cancel its pending view load');
    context.startDiagnosticStream();
    assert.equal(socket.sent.filter(m=>m.op==='diagnostic').length,1,'subscribing twice must be idempotent');
    const deliver=changes=>socket.onmessage({data:JSON.stringify({v:1,type:'diagnostic',boot:193,snapshot:false,changes})});
    socket.onmessage({data:JSON.stringify({v:1,type:'diagnostic',boot:193,snapshot:true,changes:{state:'READY',machineState:'CONFIRMED_OFF',relayClosed:false,controlSource:'none','cupPresence.state':'UNKNOWN','safety.state':'OPEN','scale.streamState':'NO_SAMPLE'}})});
    context.paintDiagnosticStream();await Promise.resolve();
    assert(!settled&&paints.length===0,'the initial base frame lacks sampled metrics');
    deliver({'health.loopMaxGapMs':12,'health.hwmon':{},nvs:{}});
    deliver({tasks:{state:'never',rows:[]}});
    context.paintDiagnosticStream();await Promise.resolve();assert(!settled);
    deliver({scaleProfile:{partitionAvailable:false}});
    const status=await pending;
    assert.equal(status.health.loopMaxGapMs,12,'load returns assembled status, never a boolean');
    context.paintDiagnosticStream();assert.equal(paints.length,1);
    context.stopDiagnosticStream();
    const messages=socket.sent.length;
    deliver({'scale.constructor.prototype':{bad:true}});
    assert.equal(socket.sent.length,messages,'unsubscribed queued frames must not trigger resync');
    const waiting=context.loadDiagnosticStatus();context.stopUiStream();
    assert.equal(await waiting,null,'intentional socket stop settles the pending view load');
    context.startUiStream();const current=sockets.at(-1);current.readyState=1;current.onopen();
    const timerCount=timers.size;socket.onclose({});
    assert.equal(timers.size,timerCount,'an old close event must not clear current socket deadlines');
    const exported=context.statsFrameWindow(0,100,'date','desc',50);
    context.requestShotResync();
    assert.equal(await exported,null,'resync must settle exports canceled by the server');
    context.stopUiStream();
  })().catch(error=>{console.error(error);process.exitCode=1;});
}
