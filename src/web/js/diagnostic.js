function renderMicraCloudDiagnostic(lm){
  ensureMicraRows();
const set=(id,value)=>{const el=$(id);if(el)el.textContent=value};
  const none=__WEBUI_TEXT__("diagnostic.cloud_no_call"),call=lm.cloudCall;
  set('dMicraCloudEmail',lm.email||__WEBUI_TEXT__("diagnostic.cloud_no_account"));
  set('dMicraCloudMachine',lm.accountConfigured?(lm.selectedName||lm.selectedSerial):__WEBUI_TEXT__("diagnostic.cloud_no_machine"));
  set('dMicraCloudTime',call?(call.startedAtUtcSec?R.formatWallTime(call.startedAtUtcSec,0)+' UTC':__WEBUI_TEXT__("runtime.unknown")):none);
  set('dMicraCloudApi',call?call.method+' '+call.api:none);
const label=__WEBUI_TEXT__("diagnostic.cloud_results").split('|')[['success','canceled','http_error','transport_error','invalid_response','response_too_large','setup_error'].indexOf(call?.result)];
  set('dMicraCloudResult',call?(label||call.result)+(call.httpStatus?' · HTTP '+call.httpStatus:'')+(call.transportStatus?' · '+call.transportStatus:''):none);
  set('dMicraCloudDuration',call?call.durationMs+' ms':none);
  renderMicraWebSocket(lm,set);
}

function renderMicraWebSocket(lm,set){
const ws=lm.websocket||{},unknown=__WEBUI_TEXT__("runtime.unknown");
const age=value=>value&&ws.nowMs!=null?(((ws.nowMs-value)>>>0)/1000).toFixed(1)+' s':unknown;
set('dMicraWsState',(lm.connectionType==='api'?'API':(ws.state||unknown).replace(/_/g,' '))+(ws.reason&&ws.reason!=='none'?' · '+ws.reason.replace(/_/g,' '):''));
set('dMicraWsTiming',['message','power','pong'].map(k=>k+' '+age(ws[k+'AtMs'])).join(' · ')+' · retry '+(ws.retryRemainingMs||0)+' ms · stop '+(ws.stoppedLatencyMs??'—')+' / '+(ws.maxStoppedLatencyMs??'—')+' ms');
set('dMicraWsTraffic',['rx','tx'].map(k=>k.toUpperCase()+' '+(ws[k+'BytesPerSecond']||0)+' B/s · '+(ws[k+'BytesPerMinute']||0)+' B/60s · '+(ws[k+'Bytes']||0)+' B').join('; ')+' · '+(ws.messages||0)+' messages · '+(ws.errors||0)+' errors');
set('dMicraWsPlanned',String(ws.plannedConnections||0));
set('dMicraWsUnexpected',String(ws.unexpectedConnections||0));
const label=__WEBUI_TEXT__("diagnostic.cleaning_states").split('|')[['inactive','waiting_for_paddle','cleaning'].indexOf(ws.cleaning)];
const cleaning=!lm.accountConfigured?__WEBUI_TEXT__("runtime.not_connected"):!lm.observeState?__WEBUI_TEXT__("runtime.disabled"):lm.connectionType==='api'?__WEBUI_TEXT__("diagnostic.cleaning_api"):!ws.cleaningAvailable?__WEBUI_TEXT__("diagnostic.cleaning_no_update"):label||__WEBUI_TEXT__("runtime.unknown_4")+(ws.cleaningLabel?' · '+ws.cleaningLabel:'');
set('dMicraCleaning',cleaning);
set('dMicraCleaningHint',lm.accountConfigured&&lm.observeState&&lm.connectionType!=='api'&&ws.cleaningAvailable?__WEBUI_TEXT__("diagnostic.last_reported")+' '+age(ws.cleaningAtMs)+' · WebSocket'+(ws.state!=='streaming'||ws.machineConnectedKnown&&!ws.machineConnected?' · '+__WEBUI_TEXT__("diagnostic.stale"):''):'');
set('dMicraWsHeap','WS '+(ws.retainedBytes||0)+' B PSRAM · internal Δ free/largest connect '+(ws.connectFreeDelta??unknown)+'/'+(ws.connectLargestDelta??unknown)+' B, stop '+(ws.stopFreeDelta??unknown)+'/'+(ws.stopLargestDelta??unknown)+' B · failures '+(ws.allocationFailures||0));
}
function ensureMicraRows(){
const cloud=$('micraCloudDiagnostics');
const row=(id,title,parent=cloud)=>{
if($(id)||!parent)return;
const row=document.createElement('div'),label=document.createElement('strong'),v=document.createElement('div');
row.className='metric micraOnly';label.textContent=title;v.id=id;row.append(label,v);parent.insertBefore(row,id==='dMicraCleaning'?$('dMicraMode').parentElement:null);
};
const titles=__WEBUI_TEXT__("diagnostic.cloud_titles").split('|');
['Email','Machine','Time','Api','Result','Duration','WsState','WsTraffic','WsPlanned','WsUnexpected','Cleaning'].forEach((id,i)=>row('dMicra'+(i<6?'Cloud':'')+id,titles[i],i===10?$('dMicraMode')?.parentElement?.parentElement:cloud));
for(const [id,anchor] of [['dMicraCleaningHint','dMicraCleaning'],['dMicraWsTiming','dMicraWsState'],['dMicraWsHeap','hHeapLargest']]){
if($(id)||!$(anchor))continue;
const hint=document.createElement('small');hint.id=id;hint.className='fieldHint micraOnly';$(anchor).parentElement.append(hint);
}
}
function formatScaleDisconnect(sc){return sc.lastDisconnect?.summary||sc.lastDisconnectReasonName||__WEBUI_TEXT__("diagnostic.none_2")}
function formatScaleCommandFailure(c){return c?.summary||__WEBUI_TEXT__("diagnostic.none_3")}
function applyScaleCommands(scale){const table=$('scaleCommandTable'),rows=$('scaleCommandRows');rows.replaceChildren();table.hidden=!scale.supportedCommandsKnown;for(const c of scale.supportedCommands||[]){const row=rows.insertRow();row.insertCell().textContent=c.name;row.insertCell().textContent=c.code}$('scaleCommandHint').textContent=!scale.supportedCommandsKnown?__WEBUI_TEXT__("diagnostic.commands_unknown"):scale.model==='bookoo_ultra'?__WEBUI_TEXT__("diagnostic.ultra_charging_note"):''}
'use strict';import*as R from'./runtime.js?v=__FW_ASSET_TAG__';const $=R.$;let ready=false;const flags=(o,...keys)=>keys.map(k=>k+'='+(o[k]?'1':'0')).join(' ');function fmtTaskPct(n){return Number.isFinite(n)?n.toFixed(1)+'%':'—'}function applyTaskProfiler(t){const T=$('hTaskState'),E=$('hTaskElapsed'),B=$('taskTableBody'),H=$('taskTableHint'),A=$('taskProfilerStartButton'),P=$('taskProfilerStopButton'),own=!!R.webUiOwner;if(!t||typeof t.state!='string'){if(T)T.textContent=__WEBUI_TEXT__("diagnostic.unknown");if(E)E.textContent=__WEBUI_TEXT__("diagnostic.unknown");if(B)B.replaceChildren()
;if(H)H.textContent='';if(A)A.disabled=!own;if(P)P.disabled=true;return}const run=t.state==='running';if(T){let s={never:__WEBUI_TEXT__("diagnostic.idle"),running:__WEBUI_TEXT__("diagnostic.running"),stopped:__WEBUI_TEXT__("diagnostic.stopped"),failed:__WEBUI_TEXT__("diagnostic.failed")}[t.state]||t.state;if(t.stopReason&&(t.state==='failed'&&t.stopReason!=='none'||t.state==='stopped'&&t.stopReason==='timeout'))s+=' ('+t.stopReason+')';T.textContent=s}if(E)E.textContent=['running','stopped','failed'].includes(t.state)?Math.floor((t.elapsedMs||0)/1e3)+__WEBUI_TEXT__("diagnostic.s"):__WEBUI_TEXT__("diagnostic.unknown");if(A)A.disabled=run||!own
;if(P)P.disabled=!run||!own;if(B){B.replaceChildren();for(const r of t.rows||[]){if(r.name?.startsWith('loopTask/')&&!r.sampleCount)continue;const tr=document.createElement('tr');[r.name||'—',[0,1].includes(r.core)?r.core:'—',fmtTaskPct(r.currentCpuPct),fmtTaskPct(r.averageCpuPct),r.stackMinWords<4294967295?r.stackMinWords??'—':'—'].forEach(c=>{const td=document.createElement('td');td.textContent=c;tr.appendChild(td)});B.appendChild(tr)}}if(H){const p=t.truncated?[__WEBUI_TEXT__("diagnostic.list_truncated")]:[];for(const[label,value]of[[__WEBUI_TEXT__("diagnostic.other_now"),t.unreportedCurrentCpuPct],[__WEBUI_TEXT__("diagnostic.other_avg"),t.unreportedAverageCpuPct],[__WEBUI_TEXT__("diagnostic.total_now"),t.currentTotalCpuPct]])if(Number.isFinite(value)&&(label==='Total now'||value>.05))p.push(label+' '+fmtTaskPct(value));H.textContent=p.join(__WEBUI_TEXT__("diagnostic.symbol_3"))}}function applyDiagnosticGuards(s){const g=s&&s.guards;if(!g)return
;const live=!!s.machineRunning,b=!!g.bbwEnabled,ns=g.noScale||{},atm=g.atm||{},slow=g.slowExtraction||{},fast=g.fastExtraction||{},touch=g.accidentalTouch||{},cup=g.cupProtection||{},t=(i,v)=>{const e=$(i);if(e)e.textContent=v||__WEBUI_TEXT__("diagnostic.unknown")};t('dGuardNoScaleUser',R.formatNoScaleGuard(b?{enabled:!!ns.enabled,armed:!!ns.armed}:{enabled:false}));t('dGuardNoScaleRaw',flags(ns,'enabled','armed','hold','scaleWasAvailable'))
;t('dGuardAtmUser',R.formatAtmGuard({atmEnabled:b&&!!atm.enabled,atmEnforced:live&&!!atm.enforced,atmArmed:live&&!!atm.armed,atmRemainingMs:atm.remainingMs}));t('dGuardAtmRaw',flags(atm,'enabled','armed','enforced')+__WEBUI_TEXT__("diagnostic.remainingms")+(atm.remainingMs||0));t('dGuardSlowUser',R.formatSlowExtractionGuard({guardEnabled:b&&!!slow.enabled,extended:live&&!!slow.extended,goal:slow.activeStopWeightG,inShot:live}))
;t('dGuardSlowRaw',flags(slow,'enabled','extended','targetReachedEarly')+__WEBUI_TEXT__("diagnostic.activestopweightg")+(typeof slow.activeStopWeightG==='number'?slow.activeStopWeightG.toFixed(1):__WEBUI_TEXT__("diagnostic.0_0")));t('dGuardFastUser',R.formatExtractionGuard({guardEnabled:b&&!!fast.enabled,extended:live&&!!fast.extended,goal:fast.activeStopWeightG,minBbwBrewRemainingMs:fast.minBbwBrewTimeRemainingMs,inShot:live}))
;t('dGuardFastRaw',flags(fast,'enabled','extended','targetReachedEarly')+__WEBUI_TEXT__("diagnostic.activestopweightg")+(typeof fast.activeStopWeightG==='number'?fast.activeStopWeightG.toFixed(1):__WEBUI_TEXT__("diagnostic.0_0"))+__WEBUI_TEXT__("diagnostic.minbbwbrewtimeremainingms")+(fast.minBbwBrewTimeRemainingMs||0));t('dGuardTouchUser',R.formatAccidentalTouch({enabled:b&&!!touch.enabled,holding:live&&!!touch.holding,inShot:live}))
;t('dGuardTouchRaw',flags(touch,'enabled','holding')+__WEBUI_TEXT__("diagnostic.phase")+(touch.phase||__WEBUI_TEXT__("diagnostic.startup"))+__WEBUI_TEXT__("diagnostic.class")+(touch.class||__WEBUI_TEXT__("diagnostic.ok"))+__WEBUI_TEXT__("diagnostic.pendingcount")+(touch.pendingCount||0));t('dGuardCupUser',R.formatCupProtection({enabled:b&&!!cup.enabled,requireCupToStart:!!cup.requireCupToStart,stopIfRemoved:!!cup.stopIfRemoved,present:!!cup.present,scaleUsable:!!g.scaleUsable,inShot:live,aborted:!live&&!!g.lastShotCupRemoved}))
;t('dGuardCupRaw',flags(cup,'enabled','stopIfRemoved','requireCupToStart','present','startHold','removedPending','bbwProtectionActive','bbwProtectionEnded'))}async function exportDebugData(){const btn=$('exportDebugDataButton');btn.disabled=true;R.message(__WEBUI_TEXT__("diagnostic.exporting"));try{
const data=await R.api('/api/v1/debug/export',{timeoutMs:3e4});if(!data||typeof data.exportSchemaVersion!=='number')throw new Error(__WEBUI_TEXT__("diagnostic.invalid_export"));const a=document.createElement('a'),boot=typeof data.bootId==='number'?data.bootId:R.bootId||0,url=URL.createObjectURL(new Blob([JSON.stringify(data,null,2)],{type:'application/json'}));a.href=url;a.download='shotstopper-debug-boot'+boot+'-'+(new Date).toISOString().replace(/[:.]/g,'-').slice(0,19)+'.json';document.body.appendChild(a);a.click();a.remove();URL.revokeObjectURL(url)
;R.message(__WEBUI_TEXT__("diagnostic.ok"),'ok');R.noteReachOk()}catch(e){R.message(R.formatCommandError(__WEBUI_TEXT__("diagnostic.export_failed"),e),'error');R.noteReachFail(e)}finally{btn.disabled=false}}export function applyStatus(s){R.applyDiagnosticStatus(s);if($('serialLogLevel'))$('serialLogLevel').value=(s&&s.config||{}).serialLogLevel||'none';if(!s||!(s.adminUnlocked||s.diagnosticPublic))return;if(s.lineaMicra)renderMicraCloudDiagnostic(s.lineaMicra);const sc=s.scale||{};$('hLastDisconnect').textContent=formatScaleDisconnect(sc);$('hScaleCommandFailure').textContent=formatScaleCommandFailure(sc.lastCommandFailure);applyScaleCommands(sc);applyDiagnosticGuards(s);applyTaskProfiler(s.tasks);applyScaleProfile(s.scaleProfile);applyLoopTiming(s);updateCrashRow(s);const b=$('hResetHistory');if(b){b.replaceChildren();for(const e of s.resetHistory||[]){const r=b.insertRow()
;r.insertCell().textContent=e.reason;r.insertCell().textContent=R.formatUptime(e.uptimeMs)}}}export function init(){if(ready)return;ready=true;R.registerViewStatus('diagnostic',applyStatus);const lock=$('diagnosticLockPanel'),controls=$('diagnosticControls');if(lock)lock.remove();if(controls)controls.classList.remove('hidden');$('serialLogLevel').onchange=()=>R.command('/api/v1/config',R.withBaseRev({serialLogLevel:$('serialLogLevel').value}))
;$('ringRetainLogLevel').onchange=()=>R.command('/api/v1/config',R.withBaseRev({ringRetainLogLevel:$('ringRetainLogLevel').value||'none'}));$('logFilter').onchange=R.renderLog;$('logLevelFilter').onchange=R.renderLog;$('copyLogButton').onclick=()=>navigator.clipboard&&navigator.clipboard.writeText($('log').value);$('clearLogButton').onclick=()=>R.clearLogView();$('exportDebugDataButton').onclick=()=>exportDebugData()
;if($('lineaMicraRefreshLink'))$('lineaMicraRefreshLink').onclick=e=>{e.preventDefault();R.lineaMicraAction('refresh')};const dScaleNameRename=$('dScaleNameRename');if(dScaleNameRename)dScaleNameRename.onclick=e=>{e.preventDefault();if(dScaleNameRename.getAttribute('aria-disabled')==='true')return;R.renameScale()};$('clearResetHistoryButton').onclick=()=>confirm(__WEBUI_TEXT__("diagnostic.clear_resets"))&&R.command('/api/v1/diagnostic/reset-history',{confirm:'CLEAR_RESET_HISTORY'});$('taskProfilerStartButton').onclick=()=>R.command('/api/v1/diagnostic/profiler',{enabled:true});$('taskProfilerStopButton').onclick=()=>R.command('/api/v1/diagnostic/profiler',{enabled:false});;$('scaleProfileStartButton').onclick=()=>R.command('/api/v1/diagnostic/scale-profile',{action:'start'});$('scaleProfileStopButton').onclick=()=>R.command('/api/v1/diagnostic/scale-profile',{action:'stop'});$('scaleProfileDeleteButton').onclick=()=>confirm(__WEBUI_TEXT__("diagnostic.delete_profile_confirm"))&&R.command('/api/v1/diagnostic/scale-profile',{action:'delete'});$('scaleProfileDownloadButton').onclick=()=>downloadScaleProfile();$('loopMaxResetButton').onclick=()=>R.command('/api/v1/diagnostic/loop-max/reset',{})}export function activate(){}

let crashBusy=false;
function applyLoopTiming(s){
  const tasks=s.tasks||{},rows=(tasks.rows||[]).filter(r=>r.name?.startsWith('loopTask/'));
  const body=$('loopTimingBody');body.replaceChildren();
  const ms=us=>(Math.max(0,us)/1000).toFixed(2)+' ms';
  const add=values=>{const tr=body.insertRow();for(const value of values)tr.insertCell().textContent=value};
  const recentReady=rows.length&&Number.isFinite(tasks.recentGapMs);
  const peakReady=tasks.peakGapMs===(s.health.loopMaxGapMs||0);
  $('hLoopGap').textContent=recentReady?tasks.recentGapMs+' ms':'—';
  let recentTotal=0,peakTotal=0;
  for(const r of rows){
    const a=r.recentGapExecutionUs,b=r.peakGapExecutionUs;
    recentTotal+=a;peakTotal+=b;
    add([r.name.slice(9),recentReady?ms(a):'—',peakReady?ms(b):'—']);
  }
  const tail=(k,total)=>{const d=tasks[k+'DelayUs'],q=tasks[k+'DispatchUs'];return[d,q,tasks[k+'GapUs']-total-d-q]};
  const a=tail('recent',recentTotal),b=tail('peak',peakTotal);
  [__WEBUI_TEXT__("diagnostic.delay_call"),__WEBUI_TEXT__("diagnostic.loop_dispatch"),__WEBUI_TEXT__("diagnostic.other_timing")].forEach((label,i)=>
    add([label,recentReady?ms(a[i]):'—',peakReady?ms(b[i]):'—']));
}
let profileBusy=false;
function applyScaleProfile(p){
const set=(key,value)=>{$('hProfile'+key).textContent=value},valid=p&&p.partitionAvailable;
['Start','Stop','Delete','Download'].forEach(key=>{$('scaleProfile'+key+'Button').disabled=!valid||!p['can'+key]||!R.webUiOwner});
if(!valid){set('State',__WEBUI_TEXT__("diagnostic.profile_unavailable"));['Elapsed','Records','Capacity','Remaining'].forEach(key=>set(key,'—'));return}
const SN=__WEBUI_TEXT__("diagnostic.profile_state_names").split('|'),VN=__WEBUI_TEXT__("diagnostic.profile_saved_names").split('|'),si=['empty','preparing','recording','stopped','saved'].indexOf(p.state),vi=['none','pending','saving','saved','invalidating','failed'].indexOf(p.persistence);
set('State',(SN[si]||p.state)+(p.stopReason&&p.stopReason!=='none'?' ('+p.stopReason+')':'')+(VN[vi]?' · '+VN[vi]:''));
set('Elapsed',R.formatUptime(p.elapsedMs));
set('Capacity',p.recordCapacity?Math.floor(100*(p.recordCount+(p.reservedRecords||0))/p.recordCapacity)+'%':'—');
const eta=p.estimatedRemainingMs;
set('Remaining',p.state==='recording'?(eta==null?__WEBUI_TEXT__("diagnostic.profile_estimating"):'≈ '+R.formatUptime(Math.ceil(eta/5e3)*5e3)):'—');
set('Records',p.recordCount+' · '+p.weightCount+'w '+p.eventCount+'e'+(p.lostCount>0?' · '+__WEBUI_TEXT__("diagnostic.profile_incomplete").replace('{n}',p.lostCount):''))}
async function downloadScaleProfile(){if(profileBusy)return;profileBusy=true;R.message(__WEBUI_TEXT__("diagnostic.downloading"));try{const b=await R.apiBinary('/api/v1/diagnostic/scale-profile/download'),u=URL.createObjectURL(new Blob([b],{type:'text/plain'})),a=Object.assign(document.createElement('a'),{href:u,download:'shotstopper-scale-profile.txt'});document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(u),6e4);R.message(__WEBUI_TEXT__("diagnostic.ok"),'ok')}catch(e){R.message(R.formatCommandError(__WEBUI_TEXT__("diagnostic.profile_download_failed"),e),'error')}finally{profileBusy=false}}
function updateCrashRow(s){
  const misc=$('hResetHistory')?.closest('fieldset.statusColumn');if(!misc)return;
  let row=$('crashArchiveRow');
  if(!row){row=document.createElement('div');row.id='crashArchiveRow';row.className='metric';const label=document.createElement('strong');label.textContent=__WEBUI_TEXT__("diagnostic.coredump");row.append(label);const value=document.createElement('div');value.id='crashArchiveValue';value.setAttribute('aria-live','polite');row.append(value);misc.append(row)}
  const value=$('crashArchiveValue');value.replaceChildren();
  const count=s.crashCount||0,supported=s.crashState!==1,usable=!!R.webUiOwner&&(!!s.adminUnlocked||!!s.development)&&supported&&count>0&&!crashBusy;
  const n=document.createElement('span');n.textContent=supported?count:__WEBUI_TEXT__("diagnostic.unavailable");if(supported&&count>0)n.className='stateFault';value.append(n);
  if(usable)for(const [label,action] of [[__WEBUI_TEXT__("diagnostic.download"),downloadCrashes],[__WEBUI_TEXT__("diagnostic.empty"),emptyCrashes]]){value.append(' - ');const l=document.createElement('a');l.href='#';l.textContent=label;l.onclick=()=>{if(!crashBusy)action();return false};value.append(l)}
  if(s.crashState>1)value.append(document.createTextNode(' ('+__WEBUI_TEXT__("diagnostic.crash_capture_error")+')'));
}
async function downloadCrashes(){crashBusy=true;await R.refreshStatus();R.message(__WEBUI_TEXT__("diagnostic.downloading"));try{let last=0;const tar=await R.apiBinary('/api/v1/diagnostic/crashes',bytes=>{if(bytes-last>=65536){last=bytes;R.message(__WEBUI_TEXT__("diagnostic.downloading")+' '+Math.floor(bytes/1024)+' KiB')}}),gzip=typeof CompressionStream==='function';if(gzip)R.message(__WEBUI_TEXT__("diagnostic.compressing"));const blob=gzip?await new Response(tar.stream().pipeThrough(new CompressionStream('gzip'))).blob():tar,url=URL.createObjectURL(blob),a=document.createElement('a');a.href=url;a.download='shotstopper-crashes.tar'+(gzip?'.gz':'');document.body.append(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(url),6e4);R.message(__WEBUI_TEXT__("diagnostic.ok"),'ok')}catch(e){R.message(R.formatCommandError(__WEBUI_TEXT__("diagnostic.crash_download_failed"),e),'error')}finally{crashBusy=false;await R.refreshStatus()}}
async function emptyCrashes(){if(!confirm(__WEBUI_TEXT__("diagnostic.empty_crashes_confirm")))return;crashBusy=true;await R.refreshStatus();try{await R.api('/api/v1/diagnostic/crashes/empty',{method:'POST',body:JSON.stringify({confirm:'EMPTY_CRASH_ARCHIVE'}),timeoutMs:12e4});R.message(__WEBUI_TEXT__("diagnostic.crashes_emptied"),'ok')}catch(e){R.message(R.formatCommandError(__WEBUI_TEXT__("diagnostic.crash_empty_failed"),e),'error')}finally{crashBusy=false;await R.refreshStatus()}}
