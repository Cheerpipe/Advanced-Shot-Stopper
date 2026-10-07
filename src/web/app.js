'use strict';import*as R from'/js/runtime.js?v=__FW_ASSET_TAG__';R.paintTheme(R.themeMode());document.addEventListener('change',e=>{if(e.target&&e.target.id==='uiTheme')R.paintTheme(e.target.value)});document.addEventListener('click',e=>{if(e.target&&e.target.closest('#themeSignal'))R.cycleTheme()});const assetQuery=new URL(import.meta.url).search;const jsMods=new Map;const htmlLoaded=new Set(['home']);const viewLoads=new Map;const SECONDARY=new Set(['stats','history','diagnostic','admin']);let secondaryViews=null;let logTimer=0;let routeSeq=0;let activeView='';const ROUTES={'/':'home','/settings':'settings','/stats':'stats','/history':'history','/admin':'admin','/diagnostic':'diagnostic','/log':'diagnostic'}
;function knownPath(pathname){const p=(pathname||'/').replace(/\/+$/,'')||'/';return Object.prototype.hasOwnProperty.call(ROUTES,p)?p:null}function viewToPath(view){for(const[p,v]of Object.entries(ROUTES)){if(v===view)return p}return'/'}async function loadPartial(name){const response=await fetch('/partials/'+name+'.html'+assetQuery);if(!response.ok)throw new Error(__WEBUI_TEXT__("shell.failed_to_load_view_markup"));return response.text()}async function ensureView(name){if(jsMods.has(name)&&htmlLoaded.has(name))return jsMods.get(name);if(viewLoads.has(name))return viewLoads.get(name);const load=(async()=>{const section=document.getElementById('view-'+name);if(!section)throw new Error(__WEBUI_TEXT__("shell.missing_view_section")+name);if(name==='home'){if(__homeModule.init)__homeModule.init();jsMods.set('home',__homeModule);return __homeModule}const markup=htmlLoaded.has(name)?null:loadPartial(name),module=SECONDARY.has(name)?(secondaryViews||(secondaryViews=import('/js/secondary.js'+assetQuery).then(sec=>sec.views).catch(e=>{secondaryViews=null;throw e}))).then(views=>views[name]):import('/js/'+name+'.js'+assetQuery);
const[html,mod]=await Promise.all([markup,module]);if(!mod)throw new Error(__WEBUI_TEXT__("shell.missing_secondary_view")+name);if(html!==null){section.innerHTML=html;htmlLoaded.add(name)}if(mod.init)mod.init();jsMods.set(name,mod);return mod})();viewLoads.set(name,load);try{return await load}finally{viewLoads.delete(name)}}R.setEnsureViewHook(ensureView);function stopExtraPolls(){clearInterval(logTimer);logTimer=0;R.stopDiagnosticStream();R.stopLogStream();R.stopHistoryStream();R.stopStatsStream()}function startView(name,seq=routeSeq){return R.withPollGate(async()=>{
if(!R.webUiPollingActive()||name!==activeView||seq!==routeSeq)return false;
stopExtraPolls();let ok=false;
if(seq!==routeSeq)return false;
if(name==='home'||name==='settings'||name==='admin'||name==='diagnostic'){ok=await R.loadStatus();R.armStatusTimer()}
if(name!==activeView||seq!==routeSeq)return false;
if(name==='diagnostic'){if(ok)ok=await R.startLogStream()}
if(name==='stats')ok=await R.startStatsStream();
else if(name==='history')ok=await R.startHistoryStream();
return ok;
})}async function renderRoute(pathname){const seq=++routeSeq,boot=R.showPageBoot();R.stopViewPolls();const known=knownPath(pathname);let view='home';let target='/';if(known){view=ROUTES[known];target=known}
if(R.compatibilityModeOn()&&view!=='admin'&&view!=='diagnostic'){view='admin';target='/admin'}if(location.pathname!==target)history.replaceState({},'',target);activeView=view;const ready=ensureView(view).then(()=>{if(seq!==routeSeq)return;document.body.classList.toggle('homeView',view==='home');document.querySelectorAll('.view').forEach(el=>el.classList.toggle('hidden',el.dataset.view!==view));markNavActive(viewToPath(view))});R.setActiveView(view,ready)
;try{const[,ok]=await Promise.all([ready,startView(view)]);if(seq!==routeSeq)return;if(ok)R.hideHomeBoot(boot);else if(R.webUiPollingActive())R.message(__WEBUI_TEXT__("shell.unable_to_load_view"),'error')}catch(e){if(seq===routeSeq)R.message(e&&e.message?e.message:__WEBUI_TEXT__("shell.unable_to_load_view"),'error')}}function navigate(path){const known=knownPath(path)
;const target=known?known:'/';if(location.pathname!==target)history.pushState({},'',target);renderRoute(target)}R.setViewPollHooks({stop:stopExtraPolls,start:()=>renderRoute(location.pathname),route:pathname=>renderRoute(pathname||location.pathname)});document.querySelectorAll('a[data-route]').forEach(a=>{a.addEventListener('click',e=>{e.preventDefault();const path=a.getAttribute('data-route')||'/';markNavActive(path);navigate(path)})})
;const root=document.documentElement,nav=document.querySelector('.pageNav'),mobile=window.matchMedia('(max-width: 699px)');
function updateNavigationLayout(){
const data=root.dataset;
for(const mode of mobile.matches?['bottom']:['icons','text','bottom']){
data.navLayout=mode;if(nav.scrollWidth<=nav.clientWidth)break;
}
sizeHeader();
}
function sizeHeader(){root.style.setProperty('--menu-offset',nav.offsetTop+'px')}
let pillPlaced=false;const pill=nav?Object.assign(document.createElement('div'),{className:'pill'}):null;if(pill){nav.append(pill);nav.classList.add('pillNav')}
function placePill(animate){const a=nav&&nav.querySelector('a.active');if(!pill||!a)return;if(!pillPlaced){animate=false;pillPlaced=true}
if(!animate)pill.style.transition='none';else{const travel=Math.abs(a.offsetLeft-(parseFloat(pill.style.translate)||0));pill.style.transitionDuration=`${Math.min(480,Math.max(280,travel*0.85))}ms`}
pill.style.width=`${a.offsetWidth}px`;pill.style.translate=`${a.offsetLeft}px 0`;
if(!animate)requestAnimationFrame(()=>requestAnimationFrame(()=>{pill.style.transition=''}))}
function markNavActive(path){document.querySelectorAll('.pageNav a').forEach(a=>{const current=a.getAttribute('data-route')===path;a.classList.toggle('active',current);if(current)a.setAttribute('aria-current','page');else a.removeAttribute('aria-current')});placePill(true)}
if(nav){
nav.setAttribute('aria-label',__WEBUI_TEXT__("shell.primary"));nav.querySelectorAll('a').forEach(a=>{a.title=a.textContent});
new ResizeObserver(updateNavigationLayout).observe(document.getElementById('app'));
new ResizeObserver(sizeHeader).observe(document.querySelector('.topBar'));
const pillAnchor=new ResizeObserver(()=>{const a=nav.querySelector('a.active');// Bold active labels resize their links; the placement already targets the post-toggle geometry, so only genuine resizes re-anchor (style.translate reads back normalized)
if(a&&pill.style.width===`${a.offsetWidth}px`&&parseFloat(pill.style.translate)===a.offsetLeft)return;placePill(false)});pillAnchor.observe(nav);nav.querySelectorAll('a').forEach(a=>pillAnchor.observe(a));
new MutationObserver(updateNavigationLayout).observe(nav,{subtree:true,attributes:true,attributeFilter:['class','hidden']});
document.fonts?.ready.then(()=>{updateNavigationLayout();placePill(false)});updateNavigationLayout();
mobile.addEventListener('change',updateNavigationLayout);
}
const msgEl=document.getElementById('message');if(msgEl){msgEl.setAttribute('role','status');msgEl.setAttribute('aria-live','polite')}
R.initHeaderSignals();
function scrollHeader(){root.toggleAttribute('data-scrolled',window.scrollY>0);if(mobile.matches)document.body.style.setProperty('--header-progress',Math.min(1,Math.max(0,window.scrollY/120)))}window.addEventListener('scroll',scrollHeader,{passive:true});mobile.addEventListener('change',scrollHeader);scrollHeader();window.addEventListener('popstate',()=>{if(R.exitFullScreenOnPop())return;renderRoute(location.pathname)})
;document.addEventListener('visibilitychange',()=>{R.noteWebUiPowerActivity();document.hidden?R.stopViewPolls():startView(activeView)});document.addEventListener('wheel',R.noteWebUiInteraction,{capture:true,passive:true});document.addEventListener('touchmove',R.noteWebUiInteraction,{capture:true,passive:true});document.addEventListener('pointerdown',R.noteWebUiInteraction,true);document.addEventListener('click',R.noteWebUiInteraction,true);document.addEventListener('input',R.noteWebUiInteraction,true);document.addEventListener('change',R.noteWebUiInteraction,true);document.addEventListener('keydown',R.noteWebUiInteraction,true);R.setMutable(false);R.claimWebUiOwnership();
