{
  const cupAt = runtimeJs.indexOf('function formatCupState(');
  const format = new Function(runtimeJs.slice(cupAt, runtimeJs.indexOf('\n}', cupAt) + 2) +
    ';return formatCupState;')();
  for (const cupPresence of [{state:'ABSENT',present:false}, {state:'PRESENT',present:true}]) {
    for (const scale of [undefined, {available:false}, {available:true}]) {
      const expected = scale?.available ? (cupPresence.present ? 'Present' : 'Absent') : 'Unknown';
      if (format({scale,cupPresence}) !== expected)
        throw new Error('Disconnected cup status must be Unknown; connected presence must be preserved');
    }
  }
}
{
  const helpers = runtimeJs.slice(runtimeJs.indexOf('function lastCurveWeightG('),
      runtimeJs.indexOf('async function populateTimezoneOptions('));
  const renderer = runtimeJs.slice(runtimeJs.indexOf('function renderShotSpark('),
      runtimeJs.indexOf('function renderStatsDurChart('));
  const ticks = [[], []], markers = [], charts = [];
  let markup = '';
  const axisStub = {setAttribute() {}, dataset: {}};
  const document = {createElement(){return {style:{},dataset:{},setAttribute(){}}}};
  const host = {
    get innerHTML(){return markup},
    set innerHTML(value){markup=value;markers.length=0;charts.length=0;for(const part of value.split('<div class="shotCurve">').slice(1)){const y=(part.match(/class="shotYTick"/g)||[]).map(()=>({style:{},dataset:{},textContent:'0'})),style={setProperty(k,v){this[k]=v}};charts.push({style,querySelectorAll(){return y},querySelector(){return axisStub}})}},
    hidden:true, replaceChildren(){this.innerHTML=''},
    querySelector(){return {appendChild(item){markers.push(item)},dataset:{}}},
    querySelectorAll(selector){if(selector==='.shotSparkHost')return charts;return ticks.map((items) => ({
      replaceChildren(){items.length=0}, appendChild(item){items.push(item)},
      setAttribute() {}, removeAttribute() {}, dataset: {}
    }))},
  };
  const render = new Function('document', 'ResizeObserver', '$', 'requestAnimationFrame',
      helpers + renderer + ';return renderShotSpark;')(
      document, class{observe(){}}, ()=>({}), ()=>{});
  const observed = render(host, {wCg:[0,100,100,200], wAtMs:[0,100,600,600],
    wBreakBefore:[2], wTruncated:true, durationS:1});
  if (!observed || observed.flowCurve.some(v=>v!==null) ||
      observed.segs.length !== 2 || !host.innerHTML.includes('Incomplete curve'))
    throw new Error('Actual observation times and breaks cannot fabricate startup rates');
  const maximum = render(host, {wCg:Array(1201).fill(100),
    wAtMs:Array.from({length:1201},(_,i)=>i*50),durationS:60});
  if (maximum?.flowCurve.length !== 1201 || maximum.maxFlow !== 0)
    throw new Error('Full-capacity actual-time curves must remain usable');
  const basic = render(host, {wCg:[0,0,50,150,250], wAtMs:[0,1000,2000,3000,4000],
    durationS:4, firstDropS:2, dropCg:50});
  const [weight, flow] = host.innerHTML.split('<div class="shotCurve">').slice(1);
  if (host.hidden || !basic || basic.timeMax !== 10 || basic.maxW !== 10 || basic.flowMax !== 1 ||
      !weight.includes('Weight (g)') || !flow.includes('Flow rate (g/s)') ||
      !weight.includes('class="shotSparkY"') || !flow.includes('class="shotSparkY shotSparkFlowY"') ||
      !flow.includes('d="M1.5 34.5 L48.9 34.5 L60.8 1.5"') ||
      !flow.includes('d="M60.8 1.5 L84.4 1.5"') ||
      !weight.includes('shotDropOverlay') || flow.includes('shotDropOverlay') ||
      markers.length !== 1 || !markers[0].innerHTML.includes('fill="#38bdf8"') ||
      !markers[0].innerHTML.includes('2.0 s') || markers[0].innerHTML.includes('1st') ||
      flow.includes('shotFirstDrop') ||
      host.innerHTML.indexOf('class="shotGrid"') > host.innerHTML.indexOf('class="shotTrace"') ||
      !host.innerHTML.includes('M1.5 1.5V34.5M238.5 1.5V34.5') ||
      charts[0].style['--shot-plot-min'] !== '1.10rem' || charts[1].style['--shot-plot-min'] !== '2.20rem' ||
      ticks.some(items=>items.map(item=>item.textContent).join('|')!=='0 s|10 s')) {
    throw new Error('Shot charts must retain axes/drop annotation and plot measured rates at window midpoints');
  }
  if (weight.includes('<circle') || !weight.includes('d="M48.9 32.9h0"') ||
      !weight.includes('stroke-linecap="round"'))
    throw new Error('Event annotations must use line-width non-scaling strokes, not stretched filled circles');
  for (const interruption of [
    {wBreakBefore:[3]}, {wCg:[0,0,50,NaN,250,350,450]},
    {atmS:2.5,atmClearedS:3.5},
  ]) {
    const broken=render(host,{wCg:[0,0,50,150,250,350,450],
      wAtMs:[0,1000,2000,3000,4000,5000,6000],durationS:6,firstDropS:2,...interruption});
    if(broken.flowStart!==null || !host.innerHTML.includes('d="M1.5 34.5 L48.9 34.5"'))
      throw new Error('Startup reference must not bridge rejection, tare or scale-loss gaps');
  }
  const annotations=render(host,{wCg:[0,100,200],wAtMs:[0,500,1000],durationS:1,endS:1,endCg:500});
  if(host.innerHTML.includes('<circle') || host.innerHTML.includes('d="M25.2 18.0h0"') ||
      annotations.segs.length!==1 || annotations.pts.at(-1).cg!==200 ||
      annotations.maxFlow!==2 || annotations.flowCurve[2]!==2 ||
      new Function(helpers+';return shotDisplayActualG(5,[0,100,200]);')()!==5)
    throw new Error('Weight trace must end at its last sample while summary yield and flow stay unchanged');
  const single = render(host, {wCg:[0,100,200],wAtMs:[0,500,1000],durationS:1});
  if(single.flowSegs[0].pts.length!==1 || host.innerHTML.includes('<circle') ||
      !host.innerHTML.includes('d="M13.4 1.5h0"'))
    throw new Error('A single supported flow estimate must render as a point without invented endpoints');
  const dripShot={wCg:[0,100,200,240,250],wAtMs:[0,500,1000,1500,2000],
    durationS:1,endS:1,endCg:250};
  const drip=render(host,dripShot),[dripWeight,dripFlow]=host.innerHTML.split('<div class="shotCurve">').slice(1);
  if(drip.flowCurve.join('|')!=='||2|1.4|0.5' || drip.maxFlow!==2 || drip.segs[0].pts.length!==3 ||
      drip.dripSegs[0].pts.length!==3 || drip.finalPoint.t!==2 || drip.finalPoint.cg!==250 ||
      !dripWeight.includes('class="shotTrace shotDripTrace"') || !dripWeight.includes('stroke-dasharray="3 3" opacity=".55"') ||
      markers.length!==1 || markers[0].className!=='shotFinalPoint' ||
      (dripWeight.match(/fill-opacity/g)||[]).length!==1 || !dripFlow.includes('shotDripTrace') ||
      (dripFlow.match(/fill-opacity/g)||[]).length!==1 || drip.dripFlowSegs[0].pts[0].t!==1 ||
      dripFlow.includes('shotDropOverlay') || markers[0].style.width!=='6px' || markers[0].style.height!=='6px')
    throw new Error('Real drip readings must form unfilled dashed weight/flow tails and a fixed-size weight ring');
  const interrupted=render(host,{...dripShot,wBreakBefore:[3]});
  if(interrupted.dripSegs[0].pts[0].t!==1.5 || interrupted.dripFlowSegs.length ||
      interrupted.flowCurve.slice(3).some(v=>v!==null))
    throw new Error('Drip tail must not connect across a recorded interruption');
  for(const extra of [{wTruncated:true},{endCg:300}]) {
    if(render(host,{...dripShot,...extra}).finalPoint || markers.length)
      throw new Error('An incomplete or unsettled tail cannot claim a final-yield marker');
  }
  const longDrip=render(host,{...dripShot,wAtMs:[0,500,1000,10000,11000]});
  if(longDrip.timeMax!==20 || longDrip.maxFlow!==2)
    throw new Error('Both chart time axes must include drip times without changing extraction Max flow');
  const spikeDrip=render(host,{...dripShot,wCg:[0,100,200,1000,2000]});
  if(spikeDrip.maxFlow!==2 || spikeDrip.flowMax!==18 || spikeDrip.flowCurve[4]!==18)
    throw new Error('Drip rates must fit the axis and CSV without becoming extraction Max flow');
  const crossCut=render(host,{wCg:[0,100,200,220,260],wAtMs:[0,500,1000,1100,1600],endS:1,durationS:1});
  if(crossCut.dripFlowSegs[0].pts[0].t!==1 || crossCut.dripFlowSegs.some(s=>s.pts.some(p=>p.t<1)))
    throw new Error('Midpoint estimates from crossing windows must never draw dashed flow before cutoff');
  const recovered=render(host,{wCg:[0,100,200,300,400,500],wAtMs:[0,500,1000,1500,2000,2500],endS:1,durationS:1,wBreakBefore:[3]});
  if(recovered.dripFlowSegs[0].pts[0].t!==2 || recovered.flowCurve[3]!==null || recovered.flowCurve[4]!==null)
    throw new Error('Drip flow must regain a full supported window after an interruption without bridging cutoff');
  render(host, {wCg:[0,0,0],wAtMs:[0,1000,2000],durationS:2});
  if(host.hidden || markers.length || host.innerHTML.includes('fill-opacity') ||
      !host.innerHTML.includes('d="M13.4 34.5 L37.0 34.5"'))
    throw new Error('Flat measured zero flow must remain visible');
  const tared=render(host,{wCg:[0,100,200],wAtMs:[0,500,1000],durationS:10,
    firstDropS:2,dropCg:50,tareS:3.4});
  if(tared.tareS!==3.4 || markers.length!==2 ||
      !markers[1].innerHTML.includes('fill="var(--fg)"') ||
      !markers[1].innerHTML.includes('>3.4 s</span>') ||
      Math.abs(parseFloat(markers[1].style.left)-34.2)>.001)
    throw new Error('Tare marker must retain its recorded time and theme color');
  render(host,{wCg:[0,100,200],wAtMs:[0,500,1000],durationS:3,tareS:1});
  if(markers.length!==1 || !markers[0].innerHTML.includes('>1.0 s</span>'))
    throw new Error('Whole-second tare labels must retain one decimal');
  const rounded=render(host,{wCg:[0,1200,2500,3900],wAtMs:[0,3775,7550,11325],
    durationS:15.1,goalG:36});
  if(rounded.timeMax!==20 || rounded.maxW!==40 || rounded.flowMax!==4 ||
      rounded.maxFlow<=3.7 || rounded.maxFlow>=3.8 ||
      charts[0].style['--shot-plot-min']!=='4.40rem' || charts[1].style['--shot-plot-min']!=='8.80rem')
    throw new Error('Non-multiple domains must round up and grow chart axes independently');
  const exact=render(host,{wCg:[1000,2000,3000,4000],wAtMs:[0,10000,20000,30000],durationS:40});
  if(exact.timeMax!==40 || exact.maxW!==40 || exact.flowMax!==1 ||
      charts[0].style['--shot-plot-min']!=='4.40rem' || charts[1].style['--shot-plot-min']!=='2.20rem')
    throw new Error('Exact axis boundaries must retain compact minimums');
  render(host, null);
  if (!host.hidden || host.innerHTML) throw new Error('Missing shot data must still hide the charts');
}

{
  const clearSource = runtimeJs.slice(runtimeJs.indexOf('function clearShotHero('),
      runtimeJs.indexOf('function shotPresetName('));
  const hero = {hidden: false};
  new Function('$','runShot', clearSource + ';clearShotHero();')(id => hero, () => {});
  if (!hero.hidden) {
    throw new Error('Clearing Home must hide the shot hero');
  }
  const heroElements=new Map(),lookup=id=>{
    if(!heroElements.has(id))heroElements.set(id,{hidden:false,textContent:'',style:{setProperty(){}},
      classList:{toggle(){}},setAttribute(){},replaceChildren(){}});
    return heroElements.get(id);
  };
  const paint=new Function('$','buildShotSparkModel','formatShotEnded','ms',
    runtimeJs.slice(runtimeJs.indexOf('function renderShotHero('),runtimeJs.indexOf('function shotPresetName('))+
    ';return renderShotHero;')(lookup,()=>null,()=>'',(v,n)=>(v/1000).toFixed(n));
  const card={live:true,weight:2,goal:36,elapsedMs:1000,firstDropMs:0,tareMs:null,averageFlowGps:null};
  paint(card);
  if(lookup('shotHeroDrop').hidden||lookup('shotHeroDrop').textContent!=='first drop 0.0 s'||
      !lookup('shotHeroFlow').hidden)throw new Error('A measured zero event must remain visible without inventing flow');
  paint({...card,firstDropMs:null,averageFlowGps:1.23});
  if(!lookup('shotHeroDrop').hidden||lookup('shotHeroFlow').hidden||
      lookup('shotHeroFlow').textContent!=='Avg flow 1.23 g/s')throw new Error('Home must render firmware event/flow validity');
  paint({...card,weight:36.2,elapsedMs:36800});
  if(lookup('shotHeroWeight').textContent!=='36.2 g'||lookup('shotHeroGoal').textContent!==' / 36 g'||
      lookup('shotHeroElapsed').textContent!=='36.8 s'||lookup('shotHeroError').hidden||
      lookup('shotHeroError').textContent!=='Err 0.6%')throw new Error('Home must label units and signed target error');
  paint({...card,weight:35});
  if(lookup('shotHeroError').textContent!=='Err -2.8%')throw new Error('Home must preserve undershoot error');
  for(const missing of [{weight:null},{goal:0}]){
    paint({...card,...missing});
    if(!lookup('shotHeroError').hidden)throw new Error('Home must hide unavailable target error');
  }
  paint({...card,scaleAvailable:false,weight:null,elapsedMs:27400,firstDropMs:2000,averageFlowGps:1.5});
  if(lookup('shotHeroWeight').textContent!=='27.4 s'||lookup('shotHeroGoal').textContent!==''||
      ['Elapsed','Drop','Flow','Error'].some(id=>!lookup('shotHero'+id).hidden)||
      lookup('shotHeroMode').hidden||lookup('shotHeroMode').textContent!=='No scale')
    throw new Error('A shot started without a scale must turn the hero into a timer');
  paint({...card,scaleAvailable:true});
  if(lookup('shotHeroWeight').textContent!=='2.0 g'||lookup('shotHeroGoal').textContent!==' / 36 g'||
      lookup('shotHeroElapsed').hidden)throw new Error('A scale shot must keep the weight layout');
  paint({...card,touchHold:true});
  if(lookup('shotHeroTouch').hidden||lookup('shotHeroTouch').textContent!=='touch hold')
    throw new Error('A live sustained touch must surface on the shot hero');
  paint({...card,touchHold:true,live:false});
  if(!lookup('shotHeroTouch').hidden)throw new Error('The touch-hold chip must be live-only');
  paint({...card,touchHold:true,scaleAvailable:false,weight:null,elapsedMs:27400,firstDropMs:2000,averageFlowGps:1.5});
  if(!lookup('shotHeroTouch').hidden)throw new Error('A shot without a scale must never claim a touch hold');
  paint({...card});
  if(!lookup('shotHeroTouch').hidden)throw new Error('The touch-hold chip needs the firmware flag');
  const chips=['Elapsed','Drop','Flow','Error','Touch','Mode'].map(id=>html.indexOf('id="shotHero'+id+'"'));
  if(chips.some((pos,i)=>pos<0||(i&&pos<=chips[i-1])))throw new Error('Home shot chips must keep their reading order');
  const apply = new Function(runtimeJs.slice(runtimeJs.indexOf('function uiStreamFrame('),
    runtimeJs.indexOf('function startUiStream('))+';return uiStreamFrame;')();
  const snapshot={v:1,boot:7,seq:1,base:0,revision:1,snapshot:true,cycle:19,shotId:0,
    phase:'active',curveBase:0,cursor:2,card:{valid:true,live:true,weight:1,elapsedMs:1000,averageFlowGps:null,firstDropMs:null,tareMs:null},
    curve:{wCg:[100,100],wAtMs:[100,700],wBreakBefore:[],dropS:null,endS:null}};
  const first=apply(null,snapshot);
  const update={...snapshot,seq:2,base:1,snapshot:false,curveBase:2,cursor:4,
    card:{...snapshot.card,elapsedMs:1100},curve:{wCg:[100,120],wAtMs:[800,900],wBreakBefore:[2],dropS:.8,endS:null}};
  const next=apply(first,update);
  if(next.curve.wCg.join()!=='100,100,100,120'||next.curve.wAtMs.join()!=='100,700,800,900'||
      next.curve.wBreakBefore.join()!=='2'||next.curve.dropS!==.8||first.cursor!==2)
    throw new Error('Stream must atomically append all samples, equal weights, breaks and metadata');
  if(apply(next,update)!==next)throw new Error('Duplicates must not append');
  const corrected=apply(next,{...snapshot,seq:3,revision:3,curve:{...snapshot.curve,dropS:null}});
  if(corrected.cursor!==2||corrected.curve.dropS!==null)throw new Error('Corrected markers require replacement');
  const reboot=apply(next,{...snapshot,boot:8});
  if(reboot.boot!==8||reboot.cursor!==2)throw new Error('Boot snapshots must discard old cursors');
  for(const bad of [{...update,seq:4},{...update,base:0},{...update,cycle:20},
    {...update,curveBase:1},{...snapshot,curveBase:1},
    {...snapshot,cursor:1202},{...snapshot,curve:{...snapshot.curve,wAtMs:[700,100]}},
    {...snapshot,curve:{...snapshot.curve,wCg:[true,100]}},
    {...snapshot,curve:{...snapshot.curve,wCg:['100',100]}},
    {...snapshot,card:{...snapshot.card,weight:NaN}},
    {...snapshot,card:{...snapshot.card,scaleAvailable:'false'}},
    {...snapshot,card:{...snapshot.card,scaleAvailable:null}},
    ...[undefined,-1,NaN,Infinity,60001,'0'].map(firstDropMs=>({...snapshot,card:{...snapshot.card,firstDropMs}})),
    ...[-1,NaN,'0'].map(tareMs=>({...snapshot,card:{...snapshot.card,tareMs}}))]){
    if(bad.snapshot)bad.seq=3;
    let rejected=false;try{apply(first,bad)}catch(_){rejected=true}
    if(!rejected)throw new Error('Invalid/gapped stream must request recovery');
  }
  const maximum=apply(null,{...snapshot,cursor:1201,curve:{wCg:Array(1201).fill(100),
    wAtMs:Array.from({length:1201},(_,i)=>i*50),wBreakBefore:[],wTruncated:true}});
  if(maximum.curve.wCg.length!==1201)throw new Error('Maximum curve must stay bounded and complete');
  const events=apply(null,{...snapshot,card:{...snapshot.card,firstDropMs:0,tareMs:60000}});
  if(events.card.firstDropMs!==0||events.card.tareMs!==60000||first.card.firstDropMs!==null)
    throw new Error('Scalar events must distinguish measured zero from unavailable data');
  const timed=apply(null,{...snapshot,card:{...snapshot.card,scaleAvailable:false}});
  if(timed.card.scaleAvailable!==false)
    throw new Error('The no-scale card flag must survive stream framing');
  const streamSource=fs.readFileSync(path.join(sketchDir,'network/ShotStopperUiStream.inc'),'utf8');
  const handler=streamSource.slice(streamSource.indexOf('esp_err_t ShotStopperNetwork::uiStreamHandler('),
    streamSource.indexOf('void ShotStopperNetwork::serviceUiStream('));
  if(handler.includes('sendUiStream(')||!handler.includes('session->resync = true;')||
      !streamSource.includes('uiStreamWorkPending_.exchange(true')||
      !streamSource.includes('now - uiStreamDispatchAtMs_ < cadence')||
      !streamSource.includes('kUiStreamLiveMs = 100, kUiStreamIdleMs = 250')||
      !streamSource.includes('controlCriticalRfActive_.load'))
    throw new Error('Bind/resync must share the coalesced publication cadence');
  const sockets=[],timers=new Map();let timerId=0,owner=true;
  class Socket{static OPEN=1;constructor(url){this.url=url;this.readyState=1;this.sent=[];sockets.push(this)}send(body){this.sent.push(JSON.parse(body))}close(){this.readyState=3;this.onclose?.()}}
  const listeners={};
  const controller=new Function('WebSocket','location','document','window','setTimeout','clearTimeout',
    'requestAnimationFrame','webUiPollingActive','webUiClientId','activeView','$','webUiPowerSeconds','invalidateHomeStream',
    'let homeFrame=null,homeStale=true,homeReady,homeResolve=()=>{};function noteReachFail(){}'+runtimeJs.slice(runtimeJs.search(/let\s+shotWs\s*=/),runtimeJs.indexOf('function formatExtractionGuard('))+
    ';return{start:startUiStream,stop:stopUiStream,frame:()=>shotFrame,stale:()=>shotStale};')(
    Socket,{protocol:'http:',host:'device.local'},
    {hidden:false,addEventListener:(name,fn)=>listeners[name]=fn},
    {addEventListener:(name,fn)=>listeners[name]=fn},
    (fn,delay)=>{timers.set(++timerId,{fn,delay});return timerId},id=>timers.delete(id),()=>1,
    ()=>owner,'0123456789abcdef','stats',()=>null,()=>0,()=>{});
  controller.start();controller.start();
  if(sockets.length!==1)throw new Error('Navigation must preserve one socket');
  const socket=sockets[0];socket.onopen();
  if(socket.sent[0].op!=='bind'||socket.sent[0].client!=='0123456789abcdef')throw new Error('Bind requires claimed identity');
  socket.onmessage({data:JSON.stringify(snapshot)});
  if(controller.stale()||controller.frame().seq!==1)throw new Error('Initial snapshot must replace stale cache');
  socket.onmessage({data:JSON.stringify({...update,seq:4})});
  if(!controller.stale()||socket.sent.at(-1).op!=='resync')throw new Error('Gap must mark stale and request one resync');
  socket.onmessage({data:JSON.stringify({...snapshot,seq:5})});
  if(controller.stale())throw new Error('Replacement snapshot must recover');
  socket.close();
  const retry=[...timers.values()].find(t=>t.delay>=400&&t.delay<=600);
  if(!retry||!controller.stale())throw new Error('Failure must freeze state and back off');
  retry.fn();const rebound=sockets.at(-1);rebound.onopen();
  if(rebound.sent[0].cycle!==19||rebound.sent[0].boot!==7)throw new Error('Reconnect must preserve observed identity');
  rebound.onmessage({data:JSON.stringify({...snapshot,phase:'pending',card:{...snapshot.card,live:false}})});
  owner=false;controller.stop();controller.start();
  if(sockets.length!==2||rebound.readyState!==3)throw new Error('Inactive owner cannot retain/reopen stream');
  if(codeIncludes(runtimeJs, 'let shotTick=')||codeIncludes(runtimeJs, 'runShot(live?s:0)')||
      !codeIncludes(runtimeJs, 'stopUiStream();')||!codeIncludes(runtimeJs, 'startUiStream();')||
      !codeIncludes(runtimeJs, "op:'bind',client:webUiClientId")||
      !codeIncludes(runtimeJs, '"op":"resync"'))
    throw new Error('Firmware timer and stream must follow ownership/visibility lifecycle');
}

{
  const assert=require('assert').strict,frames=new Map(),labels=[];
  let now=0,id=0,owner=true,reducedMotion=false;
  const weight={},animations=[];
  const document={hidden:false};
  const timer=new Function('performance','requestAnimationFrame','cancelAnimationFrame','document',
    'webUiPollingActive','setHomeSub','$','ms','paintUiStream','window',
    "let shotStale=false,activeView='home',shotFrame=null;"+
    runtimeJs.slice(runtimeJs.search(/let\s+noScaleClock\s*=/),runtimeJs.indexOf('// The diagnostic stream rides'))+
    ';return{sync:f=>{syncNoScaleTimer(f);shotFrame=f;revealNoScaleFinish()},elapsed:noScaleTimerElapsed,schedule:scheduleNoScaleTimer,'+
    'finish:finishNoScaleTimer,ending:()=>noScaleFinish,clock:()=>noScaleClock,stale:()=>shotStale,setStale:v=>shotStale=v,view:v=>activeView=v};')(
    {now:()=>now},fn=>{frames.set(++id,fn);return id},id=>frames.delete(id),document,
    ()=>owner,(_id,label)=>labels.push(label),()=>weight,
    (v,n)=>(v/1000).toFixed(n),()=>{},
    {matchMedia:()=>({matches:reducedMotion})});
  const card={valid:true,live:true,scaleAvailable:false,elapsedMs:1000};
  const frame={boot:1,cycle:1,phase:'active',card};
  const tick=()=>{const [id,fn]=frames.entries().next().value;frames.delete(id);fn()};
  timer.sync(frame);timer.schedule();
  now=500;tick();
  assert.equal(labels.at(-1),'1.5 s','Timer must advance without another message');
  timer.sync({...frame,card:{...card,elapsedMs:1300}});
  now=600;tick();
  assert.equal(timer.elapsed(),1590,'A late message must slow the clock gradually, without a backwards jump');
  timer.sync({...frame,card:{...card,elapsedMs:1900}});
  now=700;tick();
  assert.equal(timer.elapsed(),1700,'An ahead message must correct gradually');
  now=2100;tick();
  assert(timer.stale(),'Silence must freeze the timer after 1.5 seconds');
  const frozen=timer.elapsed();now=4000;
  assert.equal(timer.elapsed(),frozen,'Frozen timer must not advance indefinitely');
  timer.sync({...frame,card:{...card,elapsedMs:4500}});timer.setStale(false);timer.schedule();
  assert.equal(timer.elapsed(),4500,'Recovery must reanchor to firmware time');
  timer.setStale(true);now=4100;tick();
  assert.equal(timer.elapsed(),4500,'Explicit disconnect must freeze immediately');
  timer.sync({...frame,card:{...card,elapsedMs:4600}});timer.setStale(false);timer.schedule();
  document.hidden=true;tick();assert.equal(frames.size,0,'Hidden page must stop animation');
  document.hidden=false;timer.schedule();timer.view('stats');tick();
  assert.equal(frames.size,0,'Leaving Home must stop animation');
  timer.view('home');timer.schedule();owner=false;tick();
  assert.equal(frames.size,0,'Losing UI ownership must stop animation');
  owner=true;
  timer.sync({...frame,cycle:2,card:{...card,elapsedMs:100}});
  assert.equal(timer.elapsed(),100,'New cycle must reset the timer');
  timer.sync({...frame,boot:2,card:{...card,elapsedMs:200}});
  assert.equal(timer.elapsed(),200,'New boot must reset the timer');
  timer.schedule();
  timer.sync({...frame,phase:'pending',card:{...card,live:false,elapsedMs:195}});
  assert.equal(timer.clock(),null,'End must use exact firmware duration rather than local estimate');
  assert.equal(frames.size,0,'End must cancel animation through the retained window');
  timer.sync({...frame,card:{...card,scaleAvailable:true}});timer.schedule();
  assert.equal(timer.clock(),null,'Scale shots must never use the local clock');
  assert.equal(frames.size,0);
  assert.equal(timer.elapsed(),1000,'Unanimated final values must come from the firmware card');
  weight.animate=(keyframes,options)=>{
    const a={keyframes,options,playState:'running',finished:{then(fn){a.complete=()=>{a.playState='finished';fn()}}},
      cancel(){a.playState='idle';a.cancelled=true}};
    animations.push(a);return a;
  };
  const brew={...frame,cycle:10,card:{...card,elapsedMs:2000}};
  now=5000;timer.sync(brew);timer.schedule();now=5100;tick();
  timer.finish();
  assert.equal(timer.clock(),null,'First Home stop signal must freeze the local clock');
  assert.equal(timer.elapsed(),2100);
  assert.equal(animations[0].options.duration,400);
  assert.deepEqual(animations[0].keyframes,[{opacity:1},{opacity:0}]);
  const ended={...brew,phase:'pending',card:{...brew.card,live:false,elapsedMs:1950}};
  timer.sync(ended);
  assert.equal(timer.elapsed(),2100,'Final duration must remain hidden until fade out completes');
  animations[0].complete();
  assert.equal(timer.elapsed(),1950,'Swap to authoritative time at zero opacity');
  assert.equal(animations[1].options.duration,400);
  assert.deepEqual(animations[1].keyframes,[{opacity:0},{opacity:1}]);
  animations[1].complete();assert.equal(timer.ending(),null);
  timer.sync(ended);assert.equal(animations.length,2,'Retained updates must not repeat the fade');
  timer.sync({...brew,cycle:11});timer.finish();animations[2].complete();
  assert.equal(animations.length,3,'Fade in must wait for the final firmware frame');
  timer.sync({...ended,cycle:11});assert.equal(animations.length,4);
  timer.sync({...brew,cycle:12});
  assert(animations[3].cancelled,'A new cycle must cancel the old fade');
  assert.equal(timer.elapsed(),2000);
  reducedMotion=true;timer.sync({...ended,cycle:12});
  assert.equal(timer.ending(),null,'Reduced motion must skip the transition');
  assert.equal(animations.length,4);
  assert.equal(timer.elapsed(),1950);
  reducedMotion=false;timer.sync({...brew,cycle:13});timer.finish();
  timer.view('stats');timer.sync({...ended,cycle:13});animations[4].complete();
  assert.equal(timer.ending(),null,'Leaving Home must cancel a pending reveal');
}

{
  const assert=require('assert').strict,{spawnSync}=require('child_process');
  const stream=fs.readFileSync(path.join(sketchDir,'network/ShotStopperUiStream.inc'),'utf8');
  const wifi=fs.readFileSync(path.join(sketchDir,'network/ShotStopperWifi.inc'),'utf8');
  const service=stream.slice(stream.indexOf('void ShotStopperNetwork::serviceUiStream('),
    stream.indexOf('void ShotStopperNetwork::uiStreamDispatch('));
  const sync=wifi.slice(wifi.indexOf('void ShotStopperNetwork::syncControlCriticalRf('),
    wifi.indexOf('void ShotStopperNetwork::syncScaleHuntRf('));
  const directory=path.resolve(sketchDir,'..','temp','ai_temp_no_scale_stop_fade');
  fs.mkdirSync(directory,{recursive:true});
  const binary=path.join(directory,'dispatch-'+process.pid);
  const native=`
#include <atomic>
#include <cassert>
#include <cstdint>
constexpr int ESP_OK=0;
constexpr uint32_t kUiStreamLiveMs=100,kUiStreamIdleMs=250;
int queued=0,queueResult=ESP_OK,notified=0;
int httpd_queue_work(int,void (*)(void *),void *){++queued;return queueResult;}
void xTaskNotifyGive(void *){++notified;}
struct TaskLockGuard { explicit TaskLockGuard(int &){} };
struct ShotStopperNetwork {
  int server_=1,dataMux_=0;
  void *taskHandle_=this;
  struct {int fd=-1;} uiStreams_[2];
  struct {void setControlCritical(bool){}} webhooks_;
  std::atomic<bool> uiStreamWorkPending_{false},uiStreamUrgent_{false},
    controlCriticalRfActive_{false},ntpCallbackAccepting_{true},ntpAbortRequested_{false};
  std::atomic<uint32_t> rfGateGeneration_{0};
  uint32_t uiStreamDispatchAtMs_=100;
  static void uiStreamDispatch(void *){}
  void serviceUiStream(uint32_t);
  void syncControlCriticalRf(bool,bool=false);
};
${service}
${sync}
int main(){
  ShotStopperNetwork n;n.uiStreams_[0].fd=4;
  n.serviceUiStream(150);assert(queued==0);
  n.syncControlCriticalRf(true);assert(notified==1);
  n.serviceUiStream(151);assert(queued==1&&!n.uiStreamUrgent_);
  n.syncControlCriticalRf(false);
  n.serviceUiStream(152);assert(queued==1&&n.uiStreamUrgent_);
  n.uiStreamWorkPending_=false;
  n.serviceUiStream(153);assert(queued==2&&!n.uiStreamUrgent_);
  n.uiStreamWorkPending_=false;n.serviceUiStream(200);assert(queued==2);
  n.syncControlCriticalRf(true);queueResult=-1;
  n.serviceUiStream(201);assert(queued==3&&n.uiStreamUrgent_&&!n.uiStreamWorkPending_);
  queueResult=ESP_OK;n.serviceUiStream(202);assert(queued==4&&!n.uiStreamUrgent_);
  n.uiStreamWorkPending_=false;n.uiStreams_[0].fd=-1;
  n.syncControlCriticalRf(false);n.serviceUiStream(203);assert(queued==4&&n.uiStreamUrgent_);
  n.uiStreams_[0].fd=4;n.serviceUiStream(204);assert(queued==5);
  n.uiStreamWorkPending_=false;
  n.syncControlCriticalRf(false,true);n.serviceUiStream(205);assert(queued==6);
  n.uiStreamWorkPending_=false;n.syncControlCriticalRf(true);n.serviceUiStream(206);
  assert(queued==7);n.uiStreamWorkPending_=false;
  n.syncControlCriticalRf(true,true);n.serviceUiStream(207);assert(queued==8);
}
`;
  try{
    const compiled=spawnSync(process.env.CXX||'c++',['-std=c++17','-Wall','-Wextra','-Werror','-x','c++','-','-o',binary],{input:native,encoding:'utf8'});
    assert.equal(compiled.status,0,compiled.error?.message||compiled.stderr);
    const run=spawnSync(binary,[],{encoding:'utf8'});
    assert.equal(run.status,0,run.error?.message||run.stderr);
  }finally{fs.rmSync(binary,{force:true})}
  const lockFailure=stream.slice(stream.indexOf('if (xSemaphoreTake(statusResponseMux_'),
    stream.indexOf('callbacks_.refreshControlStatus();'));
  assert(lockFailure.includes('uiStreamUrgent_.store(true'),'Workspace contention must preserve urgent delivery');
  assert(stream.includes('(control.activeCycle || control.relayClosed) !='),
    'A notification preceding the committed snapshot must preserve urgent delivery');
  assert(firmwareCore.includes('syncControlCriticalRf(next.activeCycle || next.relayClosed, true)'),
    'Every changed control gate, including activator edges, must request urgent delivery');
  assert(network.includes('delta.field("physicalActivatorOn", bool(control.physicalActivatorOn))'),
    'Home must include physical activator edges even when the cycle state does not change');
  assert(codeIncludes(runtimeJs, "homeFrame.status.machineType==='paddle'&&paddleOff"),
    'Only paddle OFF, never a momentary button release, may trigger an early timer fade');
  const edgeSource=/const\s+paddleOff\s*=([\s\S]*?);/.exec(runtimeJs)[1];
  const paddleOff=new Function('homeFrame','data','return ('+edgeSource+')');
  assert(paddleOff({status:{physicalActivatorOn:true}},{changes:{physicalActivatorOn:false}}));
  assert(!paddleOff({status:{physicalActivatorOn:false}},{changes:{physicalActivatorOn:false}}),
    'A remote start with the paddle already OFF must keep counting');
  assert(!paddleOff({status:{physicalActivatorOn:true}},{changes:{'cycle.active':true}}));
  assert(!paddleOff(null,{changes:{physicalActivatorOn:false}}));
}

if (!statusSection || !statusSection[1].includes('class="lamp"') ||
    statusSection[1].includes('class="statusColumn"') ||
    statusSection[1].includes('class="row"') ||
    (statusSection[1].match(/class="metric[ "]/g) || []).length !== 4 ||
    !statusSection[1].includes('id="machineRowState"') ||
    !statusSection[1].includes('id="machineStateValue"') ||
    !statusSection[1].includes('<strong>Machine</strong>') ||
    !statusSection[1].includes('<strong>Brew</strong>') ||
    statusSection[1].includes('<strong>Cup</strong>') ||
    statusSection[1].includes('data-label="Machine"') ||
    !statusSection[1].includes('id="machineState"') ||
    !statusSection[1].includes('id="state"') ||
    !statusSection[1].includes('id="homeMicraPower"') ||
    statusSection[1].includes('id="cupState"') ||
    statusSection[1].includes('id="paddle"') ||
    statusSection[1].includes('id="relay"') ||
    statusSection[1].includes('id="safety"') ||
    statusSection[1].includes('id="statusExtractionGuard"') ||
    !scaleSection || !scaleSection[1].includes('class="lampState"') ||
    (scaleSection[1].match(/class="metric"/g) || []).length !== 3 ||
    !scaleSection[1].includes('<strong>Preferred</strong>') ||
    !scaleSection[1].includes('<strong>Weight</strong>') ||
    !scaleSection[1].includes('<strong>Timer</strong>') ||
    scaleSection[1].includes('data-label="Status"') ||
    !scaleSection[1].includes('id="scale"') ||
    !scaleSection[1].includes('id="preferredScale"') ||
    !scaleSection[1].includes('id="scaleWeight"') ||
    !scaleSection[1].includes('id="scaleTimer"') ||
    !codeIncludes(ui, 's.physicalActivatorOn?') ||
    !codeIncludes(ui, 's.relayClosed?') || !codeIncludes(ui, 'ON') || !codeIncludes(ui, 'OFF') ||
    !codeIncludes(ui, 'function formatScaleWeight(') ||
    !codeIncludes(ui, 'function formatScaleStatus(') ||
    !codeIncludes(ui, 'function formatScaleTimer(') ||
    !codeIncludes(ui, 'function formatMachineState(') ||
    !codeIncludes(ui, 'CONFIRMED_OFF:') || !codeIncludes(ui, 'Idle') ||
    !codeIncludes(ui, 'ASSUMED_ON:') || !codeIncludes(ui, 'Assumed on') ||
    !codeIncludes(ui, 'CONFIRMED_ON:') || !codeIncludes(ui, 'Confirmed on') ||
    !codeIncludes(ui, 'ASSUMED_OFF:') || !codeIncludes(ui, 'Assumed off') ||
    !codeIncludes(ui, 'function formatCupState(') ||
    !codeIncludes(ui, 'lastDisconnectReasonName') ||
    !codeIncludes(ui, 'Stale') || !codeIncludes(ui, 'No sample') ||
    !codeIncludes(ui, 'formatScaleStatus(s)') ||
    !codeIncludes(ui, 'id="preferredScale"') ||
    !codeIncludes(ui, 'id="preferredScaleSelect"') ||
    !codeIncludes(ui, 'id="preferredScalePauseHint"') ||
    !codeIncludes(ui, 'id="preferredScaleBootstrapHint"') ||
    !codeIncludes(ui, 'id="scalePreference"') ||
    !codeIncludes(ui, 'id="forgetPairedScale"') ||
    !codeIncludes(ui, 'Scale preference') ||
    !codeIncludes(ui, 'First available') ||
    !codeIncludes(ui, 'First detected') ||
    !codeIncludes(ui, 'Prefer selected') ||
    !codeIncludes(ui, 'Preferred only') ||
    !codeIncludes(ui, 'Preferred scale') ||
    !codeIncludes(ui, 'Clear preferred') ||
    !codeIncludes(ui, 'scaleMacCacheMode') ||
    !codeIncludes(ui, '/api/v1/scale/preferred/clear') ||
    codeIncludes(ui, "command('/api/v1/scale/preferred/select'") ||
    !codeIncludes(ui, 'function formatPreferredScale(') ||
    !codeIncludes(ui, 'function updatePreferredScaleSelect(') ||
    !codeIncludes(ui, 'function updateScalePreferenceOptions(') ||
    !codeIncludes(ui, "if(!preferred||(keep&&!prev)){const first=document.createElement('option')") ||
    !codeIncludes(ui, "first.textContent=mode==='only'||mode==='prefer'?") ||
    !codeIncludes(ui, 'empty.textContent=bootstrap?') ||
    !codeIncludes(ui, 'First detected') || !codeIncludes(ui, 'No preferred') ||
    codeIncludes(ui, "msg:'Select a preferred scale first.'") ||
    !codeIncludes(ui, '<option value="prefer" selected>Prefer selected</option>') ||
    codeIncludes(ui, '<option value="first" selected>First available</option>') ||
    codeIncludes(ui, '<option value="only" selected>Preferred only</option>') ||
    codeIncludes(ui, "if(!canPrefer&&(sel.value==='prefer'||sel.value==='only'))") ||
    codeIncludes(ui, "o.disabled=!canPrefer||!controlsMutable") ||
    // Regression: missing ';' after `prev` concatenated into
    // `prevupdateScalePreferenceOptions` and broke Settings status refresh.
    codeIncludes(ui, ':prevupdateScalePreferenceOptions') ||
    !codeIncludes(ui, "sel.value=keep?prev:preferred||'';updateScalePreferenceOptions()") ||
    !codeIncludes(ui, 'preferredScaleSelectSyncing') ||
    !codeIncludes(ui, "sel.dataset.pending='1'") ||
    !codeIncludes(ui, "scaleMacCacheMode:['first','prefer','only'].includes($('scalePreference')?.value)?$('scalePreference').value:'only'") ||
    !codeIncludes(ui, 'payload.preferredScaleMac=sel.value') ||
    codeIncludes(ui, 'id="alwaysUseThisScale"') ||
    codeIncludes(ui, 'Always use this scale') ||
    !codeIncludes(ui, 'function selectPreferredScale(') ||
    !codeIncludes(ui, 'function forgetPairedScale(') ||
    !codeIncludes(ui, 'Saved scale history is kept') ||
    !codeIncludes(ui, 'formatPreferredScale(s)') ||
    !codeIncludes(ui, 'macCachePauseRemainingMs>0') ||
    codeIncludes(ui, 'id="preferredScaleSettings"') ||
    codeIncludes(ui, 'id="scaleMacCacheMode"') ||
    codeIncludes(ui, 'id="clearPreferredScale"') ||
    codeIncludes(ui, 'id="scaleMacCacheFullWarn"') ||
    codeIncludes(ui, 'Use scale MAC cache') ||
    codeIncludes(ui, 'Paired scale') ||
    codeIncludes(ui, 'Forget this scale') ||
    !network.includes('preferredScaleClearHandler') ||
    !network.includes('preferredScaleSelectHandler') ||
    !network.includes('/api/v1/scale/preferred/clear') ||
    !network.includes('/api/v1/scale/preferred/select') ||
    !network.includes('Scale preference must be first, prefer, or only.') ||
    network.includes('Always use this scale must be on or off.') ||
    network.includes('scaleMacCacheMode must be disabled or full.') ||
    !network.includes('scaleMacCacheMode must be first, prefer, or only.') ||
    firmwareCore.includes('scaleMacCacheModeRequiresPreferred(candidate.scaleMacCacheMode)') ||
    scaleWorker.includes('coerceScalePreferenceModeToFirst') ||
    !scaleWorker.includes('First detected scale adopted: %s — %s') ||
    !network.includes('The paired scale cannot be forgotten while a cycle') ||
    !network.includes('\\"history\\"') ||
    network.includes('Preferred scale cache cannot be cleared') ||
    !network.includes('\\"timerMs\\"')) {
  throw new Error('Home Status must show Machine/Brew/Cup and a Scale panel with one value per label');
}
if (!codeIncludes(ui, 'id="renameScaleLink"') ||
    !codeIncludes(ui, 'id="preferredScaleRenameWrap"') ||
    !codeIncludes(ui, 'id="dScaleName"') ||
    !codeIncludes(ui, 'id="dScaleNameRename"') ||
    !codeIncludes(ui, 'id="dScaleNameRenameWrap"') ||
    !codeIncludes(ui, 'Connected scale') ||
    !codeIncludes(ui, '(rename)') ||
    !codeIncludes(ui, 'function renameScale(') ||
    !codeIncludes(ui, 'function updateScaleRenameUi(') ||
    !codeIncludes(ui, 'function scaleDisplayName(') ||
    !codeIncludes(ui, 'function validScaleFriendlyNameClient(') ||
    !codeIncludes(ui, '/api/v1/scale/friendly-name') ||
    !codeIncludes(ui, 'preferredFriendlyName') ||
    !codeIncludes(ui, 'Name this scale') ||
    !codeIncludes(ui, 'e.friendlyName&&String(e.friendlyName).trim()') ||
    !codeIncludes(ui, "o.dataset.name=(e.name&&String(e.name).trim())||''") ||
    !codeIncludes(ui, "opt.dataset?String(opt.dataset.name||'')") ||
    !network.includes('scaleFriendlyNameHandler') ||
    !network.includes('/api/v1/scale/friendly-name') ||
    !network.includes('UNKNOWN_SCALE') ||
    !network.includes('The scale name cannot be changed while a cycle') ||
    !network.includes('connectedFriendlyName') ||
    !network.includes('connectedMac') ||
    !codeIncludes(runtimeJs, "updateScaleRenameUi('dScaleNameRenameWrap',sc.connectedMac||'',sc.connectedFriendlyName||''") ||
    !network.includes('\\"preferredFriendlyName\\"') ||
    !scaleWorker.includes('bool setScaleFriendlyName(') ||
    codeIncludes(ui, "label.split(' — ')")) {
  throw new Error(
      'Scale names must support a friendly-name override with rename links in Home and Diagnostic');
}
{
  const wrap = {hidden:false, classList:{toggle(_, hidden){wrap.hidden=hidden}},
    querySelector(){return {classList:{toggle(){}},setAttribute(){}}}};
  const helpers = runtimeJs.slice(runtimeJs.indexOf('function scaleDisplayName('),
      runtimeJs.indexOf('function validScaleFriendlyNameClient('));
  const {update, selected} = new Function('$', 'controlsMutable', helpers +
      ';return {update:updateScaleRenameUi,selected:()=>scaleRename}')(id => wrap, true);
  update('dScaleNameRenameWrap', 'AA:BB:CC:DD:EE:02', 'Connected');
  if (selected().mac !== 'AA:BB:CC:DD:EE:02' || selected().current !== 'Connected' || wrap.hidden)
    throw new Error('Diagnostic rename must target the connected scale');
  update('dScaleNameRenameWrap', '', '');
  if (selected().mac || !wrap.hidden)
    throw new Error('Diagnostic rename must disappear when no scale is connected');
}
if (codeIncludes(ui, 'id="shotPanel"') ||
    codeIncludes(ui, 'id="shotBar"') ||
    codeIncludes(ui, 'id="shotBarFast"') ||
    codeIncludes(ui, 'id="shotBarTicks"') ||
    partialHtml.home.includes('id="shotBarTicks"') ||
    partialHtml.home.includes('<legend>Current / Last Shot</legend>') ||
    partialHtml.home.includes('class="ruleChartLabel">Weight (g)</div>') ||
    partialHtml.home.includes('id="shotIdle"') ||
    css.includes('content:"Weight (g)"') ||
    css.includes('#shotIdle') ||
    codeIncludes(ui, 'shotMark') ||
    !codeIncludes(ui, 'Math.max(goal,wt)') ||
    css.includes('.shotTrack') ||
    css.includes('#shotBarTicks') ||
    css.includes('.shotMark') ||
    css.includes('max-width:150%') ||
    codeIncludes(ui, 'id="shotCard"') ||
    html.includes('id="shotSparkHost"') ||
    !css.includes('.shotSparkHost') ||
    !css.includes('.shotSpark{') ||
    !css.includes('.shotSparkY{') ||
    !css.includes('.shotSparkHost .ruleChartTicks') ||
    !css.includes('.hidden,[hidden]{display:none!important}') ||
    css.includes('#shotPanel') ||
    !css.includes('#shotTable td.shotSparkCell{grid-area:spark;display:grid;gap:.65rem') ||
    !css.includes('#shotTable tr.noSpark{') ||
    !css.includes('.shotSpark{grid-area:plot;display:block;width:100%;height:100%;color:var(--ok);overflow:visible}') ||
    !css.includes('.shotGrid{stroke:var(--ln);stroke-width:.8;opacity:.7}') ||
    !css.includes('.shotGrid,.shotTrace{vector-effect:non-scaling-stroke}') ||
    !css.includes('grid-template-rows:max(2.55rem,var(--shot-plot-min,0rem)) auto') ||
    css.includes('.shotEventTicks') ||
    !css.includes('.shotYTick{position:absolute;right:0;white-space:nowrap;transform:translateY(-50%)}') ||
    css.includes('.shotYTick:first-child') || css.includes('.shotYTick:last-child') ||
    !css.includes('.ruleChartLabel{font-size:.78rem;font-weight:700;margin:0 0 .75rem') ||
    !codeIncludes(ui, 'function renderShotSpark(') ||
    !codeIncludes(runtimeJs, 'function buildShotSparkModel(') ||
    !codeIncludes(runtimeJs, 'function axisLabel(') ||
    !codeIncludes(runtimeJs, 'function fillChartTicks(') ||
    codeIncludes(runtimeJs, 's[a=') ||
    !codeIncludes(runtimeJs, 'style.left=') ||
    !codeIncludes(runtimeJs, "style.setProperty('--shot-plot-min'") ||
    !codeIncludes(runtimeJs, '.style.top=') ||
 codeIncludes(runtimeJs, 'style="--shot-plot-min:') ||
    codeIncludes(runtimeJs, "style=\"left:") ||
    !codeIncludes(runtimeJs, 'function shotDisplayFlowGS(') ||
    !codeIncludes(runtimeJs, 'if(!pts.length||dur<=0)return null') ||
    !codeIncludes(runtimeJs, 'm.firstDropS>0') ||
    !codeIncludes(runtimeJs, 'm.flowSegs,m.flowMax') ||
    !codeIncludes(runtimeJs, 'Flow rate (g/s)') ||
    !codeIncludes(runtimeJs, "querySelectorAll('.ruleChartTicks')") ||
    codeIncludes(runtimeJs, "['.ruleChartTicks',xt]") || codeIncludes(runtimeJs, "'.shotEventTicks'") ||
    codeIncludes(runtimeJs, "'1st '+L(") || !codeIncludes(runtimeJs, "label=time.toFixed(1)+' s'") ||
    codeIncludes(runtimeJs, 'fillChartTicks($(\'shotBarTicks\')') ||
    !codeIncludes(runtimeJs, 'raw.sort(') ||
    codeIncludes(runtimeJs, 'shotIdle') ||
    codeIncludes(runtimeJs, "last?'Last shot.'") ||
    !css.includes('.shotFirstDrop{') ||
    codeIncludes(runtimeJs, 'stroke="currentColor"') ||
    !codeIncludes(runtimeJs, "if(spark.hidden)row.classList.add('noSpark')") ||
    !css.includes('.shotCard{') ||
    !css.includes('.metric,.shotCard > *{') ||
    !css.includes('.metric strong,.shotCard strong{') ||
    !css.includes('.metric > div,.shotCard > * > div,.swS{') ||
    css.includes('#diagnosticsPanel .metric,#statusPanel .metric,#scalePanel .metric,.shotCard > *{') ||
    css.includes('#statusPanel .metric::before,#scalePanel .metric::before,.shotCard > *::before{') ||
    css.includes('font-size:1rem;font-weight:700;color:var(--mu)') ||
    !css.includes('.shotCard .shotDur > div,.shotCard .shotActual > div') ||
    !css.includes('grid-template-areas:"dur dur dur actual actual actual" "goal goal err err avgflow avgflow"') ||
    css.includes('grid-template-areas:"dur dur dur actual actual actual" "goal goal avgflow avgflow maxflow maxflow" "err err tare tare drop drop" "ended ended shot shot preset preset" "scale scale rate rate rate rate"') ||
    codeIncludes(ui, 'id="shotElapsed"') ||
    codeIncludes(ui, 'id="shotMoment"') ||
    codeIncludes(ui, "formatHumanTime(d.momentSec)") ||
    codeIncludes(ui, 'id="shotFirstDrop"') ||
    codeIncludes(ui, 'id="shotTareTime"') ||
    codeIncludes(ui, 'id="shotScale"') ||
    codeIncludes(ui, 'id="shotCurrentWeight"') ||
    partialHtml.home.includes('<strong>Yield</strong>') ||
    partialHtml.home.includes('<strong>Avg flow</strong>') ||
    partialHtml.home.includes('<strong>Max flow</strong>') ||
    partialHtml.home.includes('<strong>Dur</strong>') ||
    partialHtml.home.includes('data-label=') ||
    html.includes('data-label="Actual"') ||
    codeIncludes(ui, 'id="shotGoalWeight"') ||
    codeIncludes(ui, 'id="shotErr"') ||
    codeIncludes(ui, 'id="shotFlow"') ||
    codeIncludes(ui, 'id="shotMaxFlow"') ||
    codeIncludes(ui, 'id="shotEnded"') ||
    codeIncludes(ui, 'id="shotType"') ||
    codeIncludes(ui, 'id="shotPreset"') ||
    !codeIncludes(ui, 'shotFrame.card') ||
    codeIncludes(ui, 'id="shotRetare"') ||
    codeIncludes(ui, 'id="shotGuard"') ||
    codeIncludes(ui, 'id="shotPct"') ||
    !codeIncludes(ui, 'id="shotHero"') ||
    !codeIncludes(ui, 'shotHeroState') ||
    !codeIncludes(ui, 'function uiStreamFrame(') ||
    !network.includes('firstDropElapsedMs') ||
    !network.includes('shotType') ||
    !network.includes('scaleProtocol') ||
    !codeIncludes(ui, 'remoteReady&&relayStartReady&&canControl') ||
    codeIncludes(ui, 'Remote machine control disabled by policy') ||
    !network.includes('delta.field("remoteControlEnabled"') ||
    !network.includes('delta.field("lastCommand.requestId"') ||
    !network.includes('delta.field("maintenance.active"') ||
    !network.includes('delta.field("maintenance.persistPending"') ||
    !network.includes('delta.field("maintenance.persistFailed"') ||
    !codeIncludes(ui, 'persistFailed') ||
    !codeIncludes(ui, 'Saving...') ||
    !network.includes('delta.field("cycle.active"') ||
    !network.includes('extractionExtended') ||
    !codeIncludes(ui, 'paintUiStream()')) {
  throw new Error('Web UI must enforce remote policy, maintenance, durable command state, and live shot status');
}
{
  const curveTypes = fs.readFileSync(path.join(sketchDir, 'ShotStopperShotCurveTypes.h'), 'utf8');
  const record = curveTypes.slice(curveTypes.indexOf('struct ShotCurveRecord'),
      curveTypes.indexOf('inline ShotCurveRecord emptyShotCurveRecord'));
  if (!codeIncludes(runtimeJs, "spark.className='shotSparkCell'") ||
      !codeIncludes(runtimeJs, "renderShotSpark(spark,r)") ||
      /flow/i.test(record) || network.includes('\"flowCg\"') || network.includes('\"flowDtS\"')) {
    throw new Error('Stats must share the ordered Weight/Flow chart renderer without a persisted flow series');
  }
}
if (!codeIncludes(ui, 'id="autoToManualGuardEnabled"') ||
    !codeIncludes(ui, 'id="autoToManualGuardLimitMode"') ||
    !codeIncludes(ui, 'id="autoToManualGuardBaselineS"') ||
    !codeIncludes(ui, 'id="scaleTimerStopExtraDelayMs"') ||
    !html.includes('Waits this extra time before stopping the scale') ||
    html.includes('Added after measured scale start lag') ||
    html.includes('Added after the scale timer catches up to circuit whole seconds') ||
    !codeIncludes(ui, 'id="dripDelayS" type="number" min="0" max="10" step="0.1"') ||
    !codeIncludes(ui, 'id="autoToManualGuardManualLimitS"') ||
    !codeIncludes(ui, 'id="autoToManualGuardTrendS"') ||
    !codeIncludes(ui, 'id="resetGuardSamplesButton"') ||
    html.indexOf('id="autoToManualGuardLimitMode"') >
        html.indexOf('id="autoToManualGuardManualLimitS"') ||
    html.indexOf('id="autoToManualGuardManualLimitS"') >
        html.indexOf('id="autoToManualGuardTrendS"') ||
    html.indexOf('id="autoToManualGuardTrendS"') >
        html.indexOf('id="autoToManualGuardBaselineS"') ||
    html.indexOf('id="autoToManualGuardBaselineS"') >
        html.indexOf('id="resetGuardSamplesButton"') ||
    !codeIncludes(ui, 'Reset A→M samples to baseline') ||
    !codeIncludes(ui, 'id="homeAtmSub"') ||
    !codeIncludes(ui, 'id="homeNoScaleSub"') ||
    !codeIncludes(ui, 'id="noScaleBbwMode"') ||
    !codeIncludes(ui, 'id="noScaleAllowRinseWhileArmed"') ||
    !codeIncludes(ui, 'id="lastShotCooldownMin"') ||
    !codeIncludes(ui, 'When BBW has no scale') ||
    !codeIncludes(ui, 'Protection returns after') ||
    !codeIncludes(ui, 'Allow rinse while Armed') ||
    !codeIncludes(ui, "noScaleAllowRinseWhileArmed:$('noScaleAllowRinseWhileArmed').checked") ||
    !codeIncludes(ui, "'rinseEnabled','noScaleAllowRinseWhileArmed'") ||
    !codeIncludes(ui, 'id="noScaleBbwMode"') ||
    !codeIncludes(ui, 'value="warn_once"') ||
    !codeIncludes(ui, 'value="require_scale"') ||
    !codeIncludes(ui, 'function formatNoScaleGuard(') ||
    !codeIncludes(ui, 'function formatSlowExtractionGuard(') ||
    !codeIncludes(ui, "setHomeSub('homeSlowSub',formatSlowExtractionGuard(") ||
    !codeIncludes(ui, 'updateHomeGuardSubs(s,!!s.cycle?.active)') ||
    codeIncludes(ui, 'function updateNoScaleGuard(') ||
    html.includes('id="shotAtmGuard"') ||
    html.includes('id="shotNoScaleGuard"') ||
    html.includes('id="statusExtractionGuard"') ||
    html.includes('id="statusSlowExtractionGuard"') ||
    html.includes('id="statusAtmGuard"') ||
    html.includes('id="statusNoScaleGuard"') ||
    html.indexOf('<legend>Machine and scale</legend>') >
        html.indexOf('<summary>Paddle</summary>') ||
    html.indexOf('<summary>Paddle</summary>') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('<summary>No-scale BBW</summary>') >
        html.indexOf('<summary>Quick rinse</summary>') ||
    html.indexOf('id="noScaleBbwMode"') <
        html.indexOf('<legend>Machine and scale</legend>') ||
    html.indexOf('id="noScaleBbwMode"') >
        html.indexOf('id="noScaleAllowRinseWhileArmed"') ||
    html.indexOf('id="noScaleAllowRinseWhileArmed"') >
        html.indexOf('id="lastShotCooldownMin"') ||
    html.indexOf('id="lastShotCooldownMin"') <
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="lastShotCooldownMin"') >
        html.indexOf('<summary>Quick rinse</summary>') ||
    html.indexOf('id="paddleMode"') <
        html.indexOf('<summary>Paddle</summary>') ||
    html.indexOf('id="paddleMode"') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="noScaleBbwMode"') <
        html.indexOf('id="saveBrewPresetButton"') ||
    !network.includes('avoidBbwShotWithoutScale') ||
    !network.includes('noScaleBbwMode') ||
    !network.includes('noScaleAllowRinseWhileArmed') ||
    !network.includes('warn_once') ||
    !network.includes('require_scale') ||
    !network.includes('not both') ||
    !network.includes('lastShotCooldownMs') ||
    !network.includes('serialDebugOutput') ||
    !network.includes('ringRetainLogLevel') ||
    !network.includes('noScaleShotGuard') ||
    !network.includes('noScaleShotGuardEnabled') ||
    !network.includes('noScaleShotGuardArmed') ||
    !network.includes('cupPresence') ||
    !network.includes('control.cupPresent') ||
    !network.includes('machineState') ||
    !network.includes('machineRunStateName') ||
    !network.includes('cupPresenceStateName') ||
    !network.includes('cupPresenceStateName(control.cupPresenceState) : "UNKNOWN"') ||
    !firmware.includes('last.noScaleShotGuardEnabled') ||
    !firmware.includes('last.noScaleShotGuardArmed') ||
    !firmware.includes('next.cupPresent') ||
    !firmware.includes('next.machineRunState') ||
    !firmware.includes('next.cupPresenceState') ||
    !firmware.includes('cupPresenceState() == CupPresenceState::PRESENT') ||
    !domain.includes('bool cupPresent = false') ||
    !domain.includes('MachineRunState machineRunState') ||
    !domain.includes('CupPresenceState cupPresenceState') ||
    !codeIncludes(ui, 'A→M ·') ||
    !codeIncludes(ui, 'function updateHomeGuardSubs(') ||
    !codeIncludes(ui, 'updateHomeGuardSubs(s,live)') ||
    !codeIncludes(ui, "setHomeSub('homeBbwSub'") ||
    !codeIncludes(ui, "setHomeSub('homeCupSub'") ||
    codeIncludes(ui, "setHomeSub('homeAlertsSub'") ||
    !codeIncludes(ui, 'function formatCupProtection(') ||
    codeIncludes(ui, 'function formatAlertsChannel(') ||
    !/Can\\?'t brew — no cup/.test(ui) ||
    !codeIncludes(ui, 'Shot aborted') ||
    !codeIncludes(ui, 'Brew allowed') ||
    !codeIncludes(ui, "c.state==='PRESENT'||c.present?") ||
    !codeIncludes(ui, 'Present') || !codeIncludes(ui, 'Absent') ||
    !network.includes('shotLogStopDetailName(') ||
    !html.includes('option value="scale_priority">Scale priority') ||
    !css.includes('.swS') ||
    !css.includes('.homeSwitchGrid .swS') ||
    !css.includes('.homeGuardGrid{') ||
    !css.includes('grid-template-columns:repeat(2,minmax(0,1fr))') ||
    !codeIncludes(ui, 'yield_source') ||
    !network.includes('autoToManualGuardEnabled') ||
    !network.includes('autoToManualGuardBaselineMs') ||
    !network.includes('scaleTimerStopExtraDelayMs') ||
    !network.includes('dripDelayMs') ||
    !network.includes('autoToManualGuardTrendMs') ||
    !network.includes('autoToManualGuardEnforced') ||
    !network.includes('autoToManualGuardArmed') ||
    !network.includes('actualWeightSource') ||
    !network.includes('reset-guard-samples') ||
    !network.includes('AUTO_TO_MANUAL_GUARD') ||
    !domain.includes('"UNCONFIRMED_START"') ||
    !network.includes('endReasonName(') ||
    !network.includes('cupProtectionEnabled') ||
    !network.includes('stopIfCupRemoved') ||
    !network.includes('requireCupToStart') ||
    !network.includes('cupRemovedWeightG') ||
    !codeIncludes(ui, "cupRemovedWeightG:number('cupRemovedWeightG')")) {
  throw new Error('Auto-to-manual time guard must be wired in config UI, live panel, shots API, and routes');
}
if (!network.includes('callbacks_.copyHomeShot(latest, workBuf_->homeCurve)') ||
    !network.includes('shotLogProjectLastShot(latest, linked)') ||
    !network.includes('shotLogProjectLastShot(latestShot, control.lastShot)') ||
    network.includes('\"lastGoodShot\":%s') ||
    !firmwareCore.includes('shotLog.copyNewestEligible(record)') ||
    !network.includes('session.cycle = card.cycleId;') ||
    !network.includes('\\"presetName\\":\\"%s\\"') ||
    !network.includes('\\"averageFlowGps\\":%s') ||
    !network.includes('card.averageFlowValid && std::isfinite(card.averageFlowGps)') ||
    codeIncludes(ui, 'rateLastShotValue') ||
    codeIncludes(ui, 'controlsMutable&&last&&!live&&ls.shotLogId') ||
    codeIncludes(ui, 'clearLastShotButton')) {
  throw new Error('Home and integration must project the newest eligible history row');
}
const paddleHelp = html.slice(html.indexOf('<summary>Paddle</summary>'),
    html.indexOf('</details>', html.indexOf('<summary>Paddle</summary>')));
if (!html.includes('<summary>Paddle</summary>') ||
    !html.includes('id="paddleMode"') ||
    !html.includes('<option value="auto">Auto</option>') ||
    !html.includes('<option value="natural">Natural</option>') ||
    !html.includes('<option value="original">Original</option>') ||
    html.indexOf('<option value="auto">Auto</option>') >
        html.indexOf('<option value="natural">Natural</option>') ||
    html.indexOf('<option value="natural">Natural</option>') >
        html.indexOf('<option value="original">Original</option>') ||
    !paddleHelp.includes('<strong>Natural:</strong>') ||
    !paddleHelp.includes('<strong>Original:</strong>') ||
    !paddleHelp.includes('<strong>Auto:</strong>') ||
    paddleHelp.indexOf('<strong>Natural:</strong>') >
        paddleHelp.indexOf('<strong>Original:</strong>') ||
    paddleHelp.indexOf('<strong>Original:</strong>') >
        paddleHelp.indexOf('<strong>Auto:</strong>') ||
    !html.includes('ON starts and OFF stops') ||
    !html.includes('promotes that shot to Natural') ||
    !html.includes('Until the first OFF, automatic stopping waits') ||
    !html.includes('OFF after the rinse window demotes the current shot to Original-style hands-off automation') ||
    !html.includes('Neither transition stops early') ||
    !html.includes('early ON→OFF demotes the tentative shot to a rinse') ||
    !html.includes('Without weight control, OFF stops normally') ||
    !codeIncludes(ui, "paddleMode:$('paddleMode')?['auto','natural','original']") ||
    !codeIncludes(ui, "if($('paddleMode'))$('paddleMode').value=") ||
    !network.includes('"paddleMode"') ||
    !network.includes('paddleMode must be auto, natural or original.') ||
    !network.includes('jsonPaddleMode') ||
    !firmware.includes('machineApplyWorkflowConfig') ||
    !firmwareCore.includes('machineApplyWorkflowConfig(candidate, command.config)') ||
    !firmware.includes('dst.paddleMode = src.paddleMode') ||
    firmwareCore.includes('candidate.paddleMode') ||
    !firmware.includes('machineHidesPhysicalStop()') ||
    !firmware.includes('machineAllowsAutomationStop()') ||
    !firmware.includes('machineBeginCycle') ||
    domain.includes('enum class PaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('enum class PaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('NATURAL = 0') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('ORIGINAL = 1') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('AUTO = 2') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('enum class PaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('parsePaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8')
         .includes('enum class MachineType') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddlePolicy.h'), 'utf8')
         .includes('machinePollIntention()') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8')
        .includes('enum class PaddleMode') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('PaddleMode::') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('paddleMode')) {
  throw new Error('Machine Paddle mode must expose Auto/Natural/Original in UI, API, and APPLY_CONFIG');
}
const brewHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8');
const scaleSenseHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperScaleSense.h'), 'utf8');
const paddlePolicyHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachinePaddlePolicy.h'), 'utf8');
const machineTypesHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineTypes.h'), 'utf8');
const momentaryInputHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryInput.h'), 'utf8');
const momentaryConfigHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryConfig.h'), 'utf8');
const momentaryControlHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryControl.h'), 'utf8');
const momentaryOnlyHeader = fs.readFileSync(
    path.join(sketchDir, 'ShotStopperMachineMomentaryOnlyState.h'), 'utf8');
const relayHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineRelay.h'), 'utf8');
if (brewHeader.includes('machinePollIntention') ||
    brewHeader.includes('getScaleLinkSnapshot') ||
    brewHeader.includes('cupPresenceState(') ||
    brewHeader.includes('scaleAvailable(')) {
  throw new Error('Brew/guards must consume GuardInputs from the stopper — not poll machine/scale/cup');
}
if (scaleSenseHeader.includes('onFirstDropsDetected') ||
    scaleSenseHeader.includes('notifyCupPresenceTare') ||
    scaleSenseHeader.includes('holdCupPresenceTransitions')) {
  throw new Error('Scale sense must return events; the stopper applies brew/cup effects');
}
if (momentaryInputHeader.includes('session.active') ||
    momentaryConfigHeader.includes('session.active') ||
    momentaryControlHeader.includes('session.active') ||
    momentaryOnlyHeader.includes('session.active') ||
    momentaryOnlyHeader.includes('currentWeight') ||
    relayHeader.includes('session.automaticEnabled')) {
  throw new Error('Machine momentary/relay must not read session or live scale globals');
}
if (firmwareCore.includes('readRawActivatorOn()') ||
    firmwareCore.includes('pinMode(RELAY_GPIO') ||
    firmwareCore.includes('#if SHOT_STOPPER_MACHINE_TYPE') ||
    firmwareCore.includes('MACHINE_USES_MOMENTARY') ||
    firmwareCore.includes('stopPulseTenMs') ||
    firmwareCore.includes('maxSinglePressHundredMs') ||
    !firmware.includes('pinMode(RELAY_GPIO, OUTPUT)') ||
    !firmware.includes('machineBootActivatorHeldStably()') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachineActivatorSample.h'), 'utf8')
         .includes('bool rawActivatorOn') ||
    firmwareCore.includes('bool rawActivatorOn')) {
  throw new Error(
      'Machine GPIO, activator sample BSS, and boot debounce must live in machine headers — not shotStopper.cpp');
}
if (!html.includes('<summary>Switch</summary>') ||
    !html.includes('id="stopPulseMs"') ||
    !html.includes('id="maxSinglePressMs"') ||
    !html.includes('id="momentaryStartEdge"') ||
    !html.includes('id="reedConfirmTimeoutS"') ||
    !html.includes('id="assumeIdleWhenScaleConnects"') ||
    !html.includes('id="shotReactTimeoutS"') ||
    !html.includes('class="reedOnly"') ||
    !html.includes('class="switchOnly"') ||
    !html.includes('class="cfgGroup momentaryOnly"') ||
    !html.includes('Auto-stop pulse') ||
    !html.includes('Single-press limit') ||
    !html.includes('Start/stop on') ||
    !html.includes('Reed confirm timeout') ||
    !html.includes('Button press') ||
    !html.includes('Button release') ||
    !html.includes('<strong>Button press:</strong>') ||
    !html.includes('<strong>Button release:</strong>') ||
    !html.includes('shot state start or stop when the button is pressed') ||
    !html.includes('demotes the tentative shot to a rinse') ||
    !html.includes('only after a short release') ||
    !html.includes('starts a rinse directly, never a shot') ||
    !html.includes('machine sensor to confirm that water started or stopped') ||
    !html.includes('the displayed state follows the sensor') ||
    !html.includes('Longer holds remain manual') ||
    html.includes('<summary>Momentary</summary>') ||
    html.indexOf('<summary>Paddle</summary>') >
        html.indexOf('<summary>Switch</summary>') ||
    html.indexOf('<summary>Switch</summary>') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    html.indexOf('id="stopPulseMs"') >
        html.indexOf('<summary>No-scale BBW</summary>') ||
    !html.includes(
        'How long the controller holds the machine button when stopping automatically') ||
    !codeIncludes(ui, "stopPulseMs:$('stopPulseMs')?number(") ||
    !codeIncludes(ui, "if($('stopPulseMs'))$('stopPulseMs').value=") ||
    !codeIncludes(ui, 'Auto-stop pulse') ||
    !codeIncludes(ui, 'Single-press limit') ||
    !codeIncludes(ui, 'Reed confirm timeout') ||
    !codeIncludes(ui, 'momentaryStartEdge:') ||
    !codeIncludes(ui, 'reedConfirmTimeoutMs:') ||
    !codeIncludes(ui, 'assumeIdleWhenScaleConnects:') ||
    !codeIncludes(ui, 'shotReactTimeoutS:') ||
    !codeIncludes(ui, 'id="overrideIdleLink"') ||
    !codeIncludes(ui, 'id="overrideBrewingLink"') ||
    !codeIncludes(ui, 'id="machineStateValue"') ||
    !codeIncludes(ui, '/api/v1/control/state-override') ||
    !codeIncludes(ui, 'updateHomeAdminActions') ||
    !codeIncludes(ui, 'function updateHomeAdminActions(unlocked,remoteEnabled){const panel=$(') ||
    !codeIncludes(ui, 'show=!!unlocked&&!!remoteEnabled') ||
    !codeIncludes(ui, 'syncAdminSessionUi(admin,remoteReady)') ||
    codeIncludes(ui, 'id="overrideIdleButton"') ||
    codeIncludes(ui, 'id="overrideBrewingButton"') ||
    codeIncludes(ui, "d.classList.contains('momentaryMachine')&&!d.classList.contains('reedMachine')") ||
    !html.includes('Assume idle when the scale connects') ||
    !html.includes('marks the machine as idle without pressing its button') ||
    !html.includes('Shot reaction timeout') ||
    !html.includes(
        'If the scale shows no coffee for this long after Start') ||
    !html.includes('Override idle') ||
    !html.includes('Override brewing') ||
    !css.includes('html.reedMachine .switchOnly') ||
    !css.includes('html:not(.momentaryMachine) .switchOnly') ||
    !css.includes('#machineStateValue .switchOnly') ||
    !css.includes('#machineStateValue .switchOnly,#machineStateValue a{font-size:.78rem;font-weight:400}') ||
    !css.includes('.momentaryOnly') ||
    !css.includes('html:not(.reedMachine) .reedOnly') ||
    !network.includes('"stopPulseMs"') ||
    !network.includes('"momentaryStartEdge"') ||
    !network.includes('"reedConfirmTimeoutMs"') ||
    !network.includes('stopPulseMs must be an integer from 50 to 1000.') ||
    !network.includes('maxSinglePressMs must be an integer from 100 to 5000.') ||
    !network.includes('momentaryStartEdge must be press or release.') ||
    !network.includes(
        'reedConfirmTimeoutMs must be an integer from 200 to 5000.') ||
    !network.includes(
        'Shot reaction timeout must be 0 (compiled default) or from 3 to 30 s.') ||
    !network.includes(
        'shotReactTimeoutS must be 0 (compiled default) or an integer from 3 to 30.') ||
    !network.includes('jsonStopPulseMs') ||
    !network.includes('jsonMomentaryStartEdge') ||
    !network.includes('jsonReedConfirmTimeoutMs') ||
    !network.includes('jsonAssumeIdleWhenScaleConnects') ||
    !network.includes('jsonShotReactTimeoutS') ||
    !network.includes('stateOverrideHandler') ||
    !network.includes('/api/v1/control/state-override') ||
    !firmware.includes('machineApplyWorkflowConfig') ||
    !firmware.includes('dst.stopPulseTenMs = src.stopPulseTenMs') ||
    !momentaryControlHeader.includes(
        'dst.momentaryStartOnPress = src.momentaryStartOnPress') ||
    !momentaryControlHeader.includes(
        'dst.reedConfirmTimeoutHundredMs = src.reedConfirmTimeoutHundredMs') ||
    !momentaryControlHeader.includes(
        'dst.assumeIdleWhenScaleConnects = src.assumeIdleWhenScaleConnects') ||
    !momentaryControlHeader.includes(
        'dst.shotReactTimeoutS = src.shotReactTimeoutS') ||
    paddlePolicyHeader.includes('momentaryStartOnPress') ||
    paddlePolicyHeader.includes('reedConfirmTimeoutHundredMs') ||
    paddlePolicyHeader.includes('assumeIdleWhenScaleConnects') ||
    paddlePolicyHeader.includes('shotReactTimeoutS') ||
    machineTypesHeader.includes('parseMomentaryStartEdge') ||
    machineTypesHeader.includes('momentaryStartEdgeId') ||
    machineTypesHeader.includes('PaddleMode') ||
    machineTypesHeader.includes('parsePaddleMode') ||
    !fs.readFileSync(path.join(sketchDir, 'ShotStopperMachinePaddleConfig.h'), 'utf8')
         .includes('parsePaddleMode') ||
    !momentaryConfigHeader.includes('parseMomentaryStartEdge') ||
    !momentaryConfigHeader.includes('momentaryStartEdgeId') ||
    !domain.includes('uint8_t stopPulseTenMs') ||
    !domain.includes('bool momentaryStartOnPress') ||
    !domain.includes('setRuntimeStopPulseMs') ||
    !domain.includes('setRuntimeReedConfirmTimeoutMs') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8')
        .includes('stopPulseTenMs') ||
    fs.readFileSync(path.join(sketchDir, 'ShotStopperBrew.h'), 'utf8')
        .includes('stopPulseTenMs')) {
  throw new Error(
      'Momentary Switch timings must be wired in Settings, API, APPLY_CONFIG, and runtime config — not brew');
}
{
  const assert = require('assert'), vm = require('vm'), classes = new Set(['hidden']);
  const toggle = (name, on) => on ? classes.add(name) : classes.delete(name);
  const context = vm.createContext({$: () => ({classList: {toggle}}),
    document: {body: {classList: {toggle: () => {}}}}});
  vm.runInContext(blockAt(runtimeJs, 'function updateHomeAdminActions('), context);
  const visible = (admin, remote) => { vm.runInContext(`updateHomeAdminActions(${admin},${remote})`, context); return !classes.has('hidden'); };
  assert(!visible(false, false) && !visible(true, false) && !visible(false, true) && visible(true, true));
}
if (!html.includes('class="cfgGroup paddleOnly"><summary>Paddle</summary>') ||
    !html.includes('class="cfgGroup"><summary>Quick rinse</summary>') ||
    html.includes('class="cfgGroup paddleOnly"><summary>Quick rinse</summary>') ||
    !html.includes('id="rinseEnabled" type="checkbox"') ||
    html.indexOf('id="rinseEnabled"') >
        html.indexOf('id="rinseGestureS"') ||
    html.indexOf('<summary>Quick rinse</summary>') >
        html.indexOf('id="rinseEnabled"') ||
    !html.includes('id="rinseEnabled" type="checkbox"> Quick rinse') ||
    !html.includes('Lets you flush the group with a short paddle flip') ||
    !html.includes('Lets you flush the group with a long press from idle') ||
    !html.includes('How long to hold the switch from idle before a rinse starts') ||
    !html.includes('How long water runs after a Quick rinse begins') ||
    !html.includes('a long press is left to the machine') ||
    !html.includes('id="rinseButton" class="btnGlyph" title="Start rinse"') ||
    html.includes('id="rinseButton" class="btnGlyph paddleOnly"') ||
    !codeIncludes(ui, 's.config.rinseEnabled===true') ||
    !codeIncludes(ui, 'rinseEnabled:$(\'rinseEnabled\').checked') ||
    !network.includes('"rinseEnabled"') ||
    !network.includes('rinseEnabled must be a boolean.') ||
    !firmwareCore.includes('candidate.rinseEnabled = command.config.rinseEnabled') ||
    !firmware.includes('uint32_t rinseBegin(') ||
    !firmware.includes('UserIntent::REQUEST_RINSE') ||
    !firmware.includes('machineBeginRinse') ||
    !html.includes('class="cfgGroup momentaryOnly"><summary>Switch</summary>') ||
    !html.includes('Paddle-off reminder<select id="paddleReturnReminder">') ||
    !html.includes('<option value="0">OFF</option><option value="15">15</option><option value="30" selected>30</option><option value="60">60</option><option value="120">120</option>') ||
    !html.includes('When on, repeating beeps remind you to return the paddle after an automatic stop while it is still ON. Turn off to silence this reminder.') ||
    html.includes('id="paddleReturnReminderBeep"') ||
    html.includes('id="paddleReturnReminderIntervalS"') ||
    !codeIncludes(runtimeJs, 'paddleReturnReminderBeep:reminder?!!+reminder.value:undefined') ||
    !codeIncludes(runtimeJs, 'paddleReturnReminderIntervalMs:reminder?(+reminder.value||30)*1000:undefined') ||
    html.includes('cfgGroup paddleOnly momentaryOnly') ||
    html.includes('cfgGroup momentaryOnly paddleOnly') ||
    !css.includes('html.momentaryMachine .paddleOnly') ||
    !css.includes('html:not(.momentaryMachine) .momentaryOnly') ||
    !css.includes('html:not(.reedMachine) .reedOnly') ||
    !codeIncludes(ui, "classList.toggle('momentaryMachine',t!=='paddle')") ||
    !codeIncludes(ui, "classList.toggle('reedMachine',t==='momentary_reed')")) {
  throw new Error(
      'Paddle and Momentary Settings groups must be mutually exclusive by compiled machine type; Quick rinse is shared');
}
const rinseHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperRinse.h'), 'utf8');
const brewTypesHeader = fs.readFileSync(path.join(sketchDir, 'ShotStopperBrewTypes.h'), 'utf8');
if (rinseHeader.includes('session.') ||
    rinseHeader.includes('runtimeConfig') ||
    brewHeader.includes('runtimeConfig.rinseGestureMs') ||
    brewTypesHeader.includes('DEFAULT_RINSE_GESTURE_MS') ||
    brewTypesHeader.includes('DEFAULT_RINSE_DURATION_MS') ||
    brewTypesHeader.includes('ENTER_RINSE =') ||
    firmware.includes('machineRinseGestureMs') ||
    domainCore.includes('snapshot.rinseGestureMs') ||
    !firmwareCore.includes('session.rinseStartedAtMs = rinseBegin') ||
    !firmwareCore.includes(
        'blockedHoldTimeoutMs = runtimeConfig.rinseGestureMs') ||
    !brewHeader.includes('inputs.blockedHoldTimeoutMs')) {
  throw new Error(
      'Rinse clock must not write session; brew must not read rinseGestureMs; ShotStopper copies the accept anchor');
}
if (!codeIncludes(ui, 'id="learnedOffsetG"') ||
    !codeIncludes(ui, 'id="weightOffsetBaselineG"') ||
    !codeIncludes(ui, 'id="resetCalibrationButton"') ||
    html.indexOf('id="learnedOffsetG"') >
        html.indexOf('id="weightOffsetBaselineG"') ||
    html.indexOf('id="weightOffsetBaselineG"') >
        html.indexOf('id="resetCalibrationButton"') ||
    !codeIncludes(ui, 'Reset learned stop offset') ||
    codeIncludes(ui, 'Reset learned stop offset to baseline') ||
    !css.includes('.bbwLearning .btnBar{max-width:22rem}') ||
    css.includes('#bbwAlgorithm{width:100%}') ||
    !codeIncludes(ui, 'Save changed baseline values before resetting') ||
    !network.includes('weightOffsetBaselineG') ||
    !codeIncludes(ui, 'weightOffsetBaselineG')) {
  throw new Error('Learned stop offset baseline must be wired like A→M baseline reset');
}
if (!html.includes('<summary>Cup</summary>') ||
    !html.includes('<summary>Tare</summary>') ||
    html.indexOf('<summary>Cup</summary>') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="minimumCupWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="minimumCupWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="cupRemovedWeightG"') >
        html.indexOf('<summary>Tare</summary>') ||
    html.indexOf('id="retareStabilitySamples"') <
        html.indexOf('<summary>Cup</summary>') ||
    html.indexOf('id="retareStabilitySamples"') >
        html.indexOf('<summary>Tare</summary>') ||
    !html.includes('<summary>Scales</summary>') ||
    html.includes('<summary>Scale & retare</summary>') ||
    html.indexOf('<summary>Tare</summary>') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('<summary>Scales</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    html.indexOf('id="autoTare"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="postTareBaselineGraceS"') <
        html.indexOf('id="autoTare"') ||
    html.indexOf('id="postTareBaselineGraceS"') >
        html.indexOf('id="autoRetare"') ||
    html.indexOf('id="autoRetare"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareWindowS"') > html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="minimumCupWeightG"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilitySamples"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityToleranceG"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityMaxGapS"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="retareStabilityMinDurationS"') >
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="scaleTimerStopExtraDelayMs"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="dripDelayS"') <
        html.indexOf('id="scaleTimerStopExtraDelayMs"') ||
    html.indexOf('id="scalePreference"') <
        html.indexOf('id="dripDelayS"') ||
    html.indexOf('id="scalePreference"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="preferredScaleSelect"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="forgetPairedScale"') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('<summary>Bookoo</summary>') <
        html.indexOf('<summary>Scales</summary>') ||
    html.indexOf('id="canTareStartTimer"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="canTareStartTimer"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('id="bookooMuteOnBuzzerOnly"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="bookooMuteOnBuzzerOnly"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('id="bookooConnectBeepLevel"') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('id="bookooConnectBeepLevel"') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<strong>Requires shot-start tare.</strong>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<strong>Requires shot-start tare.</strong>') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('Applies when <strong>Buzzer only</strong> is selected.') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('Applies when <strong>Buzzer only</strong> is selected.') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<strong>Scale only or Scale priority</strong>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<strong>Scale only or Scale priority</strong>') >
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<summary>Acaia</summary>') <
        html.indexOf('<summary>Bookoo</summary>') ||
    html.indexOf('<summary>Felicita</summary>') <
        html.indexOf('<summary>Acaia</summary>') ||
    html.indexOf('<summary>Felicita</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    !codeIncludes(ui, "d.parentElement.closest('details')")) {
  throw new Error('Machine settings must split Tare and Scales, with Bookoo/Acaia/Felicita subgroups');
}
if (!html.includes('id="postTareBaselineGraceS" type="number" min="0.5" max="10" step="0.1"') ||
    !codeIncludes(ui, "rangeCheck('postTareBaselineGraceS',0.5,10,'Post-tare grace',{unit:'s',}") ||
    !codeIncludes(ui, "postTareBaselineGraceMs:sToMs('postTareBaselineGraceS')") ||
    !codeIncludes(ui, "'postTareBaselineGrace'") ||
    !codeIncludes(ui, "$(k+'S').value=String(c[k+'Ms']/1000)") ||
    !codeIncludes(ui, "apply('tareOpt',!$('autoTare').checked)") ||
    !(codeIncludes(ui, "$('autoTare').onchange=()=>{updateConfigGroups();markConfigDirty()}") ||
      codeIncludes(ui, "$('autoTare').onchange=()=>{R.updateConfigGroups();R.markConfigDirty()}")) ||
    !codeIncludes(ui, "typeof c.postTareBaselineGraceMs==='number'") ||
    !network.includes('\\"postTareBaselineGraceMs\\":%lu') ||
    !network.includes('Post-tare grace must be from 0.5 to 10 s.') ||
    !network.includes('candidate.postTareBaselineGraceMs') ||
    !firmware.includes('session.config.postTareBaselineGraceMs')) {
  throw new Error('Post-tare grace must be wired through Tare settings, status/settings, and firmware');
}
if (!codeIncludes(ui, "rangeCheck('dripDelayS',0,10,'Drip delay',{unit:'s',}") ||
    !codeIncludes(ui, "dripDelayMs:sToMs('dripDelayS')") ||
    !codeIncludes(ui, "$('dripDelayS').value=String((c.dripDelayMs??3000)/1000)") ||
    !codeIncludes(ui, 'How long to wait after water stops before recording the final drink weight and learning from it.') ||
    !network.includes('\\"dripDelayMs\\":%lu') ||
    !network.includes('Drip delay must be from 0 to 10 s.') ||
    !network.includes('candidate.dripDelayMs')) {
  throw new Error('Drip delay must be wired through Settings, status/settings, and config validation');
}
if (!html.includes('<summary>AtomHeart Eclair</summary>') ||
    !html.includes('Eclair uses the regular tare and timer workflow') ||
    !html.includes('It has no extra volume or sound options') ||
    html.indexOf('<summary>AtomHeart Eclair</summary>') <
        html.indexOf('<summary>Felicita</summary>') ||
    html.indexOf('<summary>AtomHeart Eclair</summary>') >
        html.indexOf('<summary>Alerts</summary>') ||
    html.includes('id="eclair')) {
  throw new Error('Machine settings must include an informational AtomHeart Eclair subgroup without settings');
}
