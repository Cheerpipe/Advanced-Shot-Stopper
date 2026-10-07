// The full-screen shot card and the shot-aware session keep-alive are the
// two behavioral contracts added with the mobile presentation mode. Both are
// regression-prone state machines, so they get pure vm-slice coverage here:
// the keep-alive must read fresh stream state (a frozen liveness copy once
// kept dead sessions alive), and the back-consume protocol must swallow
// exactly one history pop per exit.
{
  const assert = require('assert').strict;
  const vm = require('vm');

  // Session keep-alive: resetWebUiInactivity / webUiPollingActive /
  // sessionShotLive over controllable time, timers, and stream state.
  {
    let now = 0;
    let deactivated = 0;
    let pendingTimer = null;
    const api = new Function('Date', 'setTimeout', 'clearTimeout', 'deactivateWebUi',
        'let webUiOwner=true,webUiActiveUntil=0,webUiInactivityTimer=0' +
        ',WEB_UI_INACTIVITY_MS=1000,shotFrame=null,shotStale=true' +
        ',homeFrame=null,homeStale=true,statusLiveShot=false;' +
        runtimeJs.slice(runtimeJs.indexOf('function sessionShotLive('),
            runtimeJs.indexOf('function noteWebUiInteraction(')) +
        ';return{reset:resetWebUiInactivity,polling:webUiPollingActive' +
        ',setFrame:v=>shotFrame=v,setStale:v=>shotStale=v' +
        ',setHome:(stale,status)=>{homeStale=stale;homeFrame={status}}' +
        ',setOwner:v=>webUiOwner=v,until:()=>webUiActiveUntil}')(
        {now: () => now},
        fn => { pendingTimer = fn; return 1; },
        () => {}, () => deactivated++);

    // A live ui-stream card re-arms the expired session instead of killing it.
    api.setStale(false);
    api.setFrame({card: {valid: true, live: true}});
    now = 5000;
    assert.equal(api.polling(), true, 'a live shot must keep the session');
    assert.equal(deactivated, 0);
    assert.ok(api.until() > now, 'the deadline must be re-armed');

    // A fresh home stream reporting an active cycle keeps it on any view.
    api.setFrame(null);
    api.setHome(false, {cycle: {active: true}});
    now = 9000;
    assert.equal(api.polling(), true, 'a live cycle on the home stream must keep the session');

    // Stale streams must let the session die (frozen liveness regression).
    api.setHome(true, {liveShot: true});
    api.reset();
    now += 1001;
    pendingTimer();
    pendingTimer = null;
    assert.equal(deactivated, 1, 'stale live data must let the session expire');
    assert.equal(api.polling(), false);

    // Losing ownership stops the re-arm chain entirely.
    api.setOwner(false);
    api.setFrame({card: {valid: true, live: true}});
    api.setHome(false, {machineRunning: true});
    assert.equal(api.polling(), false, 'a dead session must not revive for a shot');
  }

  // Back-consume protocol: enter pushes one history entry; every exit path
  // consumes exactly one pop, and unrelated pops reach the router.
  {
    const heroClasses = new Set(), bodyClasses = new Set();
    const history = {pushed: 0, back: 0};
    const stubClasses = set => ({
      add: (...c) => c.forEach(x => set.add(x)),
      remove: (...c) => c.forEach(x => set.delete(x)),
      contains: c => set.has(c),
      toggle() {},
    });
    const element = () => ({onclick: null, dataset: {}, focus() {},
      addEventListener() {}, querySelector: () => null});
    const hero = Object.assign(element(), {classList: stubClasses(heroClasses)});
    const els = {shotHero: hero, shotFsButton: element(), shotFsClose: element()};
    const api = new Function('$', 'history', 'location', 'document', 'setTimeout', 'clearTimeout',
        runtimeJs.slice(runtimeJs.search(/let\s+shotFsActive\s*=/),
            runtimeJs.indexOf('// The diagnostic stream rides')) +
        ';return{enter:enterShotFullScreen,exit:exitShotFullScreen' +
        ',pop:exitFullScreenOnPop,init:initShotFullScreen}')(
        id => els[id],
        {pushState: () => history.pushed++, back: () => history.back++},
        {href: '/'},
        {body: {classList: stubClasses(bodyClasses)}},
        () => 0, () => {});

    api.init();
    assert.equal(api.pop(), false, 'a pop without full screen reaches the router');

    els.shotFsButton.onclick();
    assert.ok(heroClasses.has('fs') && bodyClasses.has('shotFs'));
    assert.equal(history.pushed, 1, 'entering pushes one history entry');
    assert.equal(api.pop(), true, 'the back gesture exits full screen');
    assert.ok(!heroClasses.has('fs') && !bodyClasses.has('shotFs'));

    // The X button exits via history.back(); that pop must be swallowed once.
    els.shotFsButton.onclick();
    els.shotFsClose.onclick();
    assert.ok(!heroClasses.has('fs'), 'the X button exits full screen');
    assert.equal(history.back, 1, 'the X exit consumes its pushed entry');
    assert.equal(api.pop(), true, 'the X-driven pop is consumed');
    assert.equal(api.pop(), false, 'the next pop reaches the router');
  }
}
