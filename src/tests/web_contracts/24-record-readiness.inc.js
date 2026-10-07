// Cold record views must not subscribe before their markup can accept a page.
{
  const assert = require('assert').strict, vm = require('vm');
  const source = rawRuntimeJs.slice(rawRuntimeJs.indexOf('let shotWs='),
      rawRuntimeJs.indexOf('function formatExtractionGuard('));
  function fixture(view) {
    let mount;
    const sockets = [], timers = [];
    class Socket {
      static OPEN = 1;
      constructor() { this.readyState = 0; this.sent = []; sockets.push(this); }
      send(value) { this.sent.push(JSON.parse(value)); }
      open() { this.readyState = 1; this.onopen(); }
      close() { this.readyState = 3; this.onclose?.({code: 1006}); }
    }
    const context = vm.createContext({
      WebSocket: Socket, location: {protocol: 'http:', host: 'device.local'},
      document: {hidden: false, addEventListener() {}}, window: {addEventListener() {}},
      setTimeout: fn => { timers.push(fn); return timers.length; }, clearTimeout() {},
      requestAnimationFrame: () => 1, performance: {now: () => 0},
      webUiPollingActive: () => true, webUiClientId: 'test', webUiPowerSeconds: () => 30,
      activeView: view, viewReady: new Promise(resolve => { mount = resolve; }),
      invalidateHomeStream() {}, noteReachFail() {}, statusPageOk: () => true,
      shotStatsViewActive: () => context.activeView === 'stats',
      applyCommonStatus() {}, applyShotPage() {}, renderShots() {}, updateFirmwareFooter() {}, noteReachOk() {},
      shotSort: 'date', shotSortDir: 'desc', historyDir: 'desc',
      SHOTS_PAGE_SIZE: 10, SHOTS_EXPORT_LIMIT: 100, HISTORY_PAGE_SIZE: 20,
    });
    vm.runInContext(source, context);
    vm.runInContext('paintUiStream=()=>{};scheduleShotPaint=()=>{};', context);
    context.startUiStream();
    const label = view === 'stats' ? 'Stats' : 'History';
    return {context, sockets, mount, start: () => context['start' + label + 'Stream'](),
      stop: () => context['stop' + label + 'Stream'](),
      subscribed: socket => socket.sent.filter(message => message.op === view && message.on)};
  }
  (async () => {
    for (const view of ['stats', 'history']) {
      for (const socketFirst of [true, false]) {
        const f = fixture(view), pending = f.start(), socket = f.sockets[0];
        if (socketFirst) {
          socket.open();
          assert.equal(f.subscribed(socket).length, 0, view + ' must wait for markup');
        }
        f.mount();
        await Promise.resolve();
        if (!socketFirst) {
          assert.equal(f.subscribed(socket).length, 0, view + ' must wait for an open socket');
          socket.open();
        }
        assert.equal(f.subscribed(socket).length, 1, view + ' must subscribe exactly once');
        assert.equal(f.subscribed(socket)[0].request, 1);
        socket.close();
        assert.equal(await pending, false, 'disconnect settles the pending view');
        f.context.startUiStream();
        f.sockets[1].open();
        assert.equal(f.subscribed(f.sockets[1]).length, 1, 'ready views resubscribe on reconnect');
        assert.equal(f.subscribed(f.sockets[1])[0].request, 2);
      }
      {
        const f = fixture(view), cancelled = f.start();
        f.stop();
        const replacement = f.start();
        f.sockets[0].open(); f.mount();
        await Promise.resolve();
        assert.equal(await cancelled, false);
        assert.equal(f.subscribed(f.sockets[0]).length, 1,
            'a cancelled mount cannot subscribe again after same-view reentry');
        f.stop();
        assert.equal(await replacement, false);
      }
      {
        const f = fixture(view), pending = f.start();
        f.stop(); f.context.activeView = 'settings';
        f.sockets[0].open(); f.mount();
        assert.equal(await pending, false);
        assert.equal(f.subscribed(f.sockets[0]).length, 0, 'leaving a cold view cancels its subscription');
      }
    }
    // Deliver a real split Stats page through the production socket callback.
    const f = fixture('stats'), pending = f.start(), socket = f.sockets[0];
    socket.open(); f.mount(); await Promise.resolve();
    const row = id => ({id, bootId: 9, endedAtMs: 30000, hasWallTime: false,
      endedAtLocalSec: 0, endedAtUnixSec: 0, durationS: 28, rating: 0,
      wCg: [100, 200], wAtMs: [1000, 2000], wBreakBefore: []});
    const frame = {v: 1, type: 'stats', boot: 9, snapshot: true, epoch: 5, seq: 1, request: 1,
      ui: {}, bootId: 9, total: 2, offset: 0, limit: 10, hasMore: false,
      rows: [row(2)], rowBase: 0, more: true};
    socket.onmessage({data: JSON.stringify(frame)});
    assert.equal(vm.runInContext('statsPage.rows.length', f.context), 1);
    // Export can be requested while the standing page is still arriving.
    const exported = f.context.statsFrameWindow(0, 100, 'date', 'desc', 90000);
    assert.equal(vm.runInContext('statsPage.rows.length', f.context), 1);
    socket.onmessage({data: JSON.stringify({...frame, snapshot: false,
      rows: [row(1)], rowBase: 1, more: false})});
    assert.equal(await pending, true);
    const request = socket.sent.find(message => message.fetch).request;
    socket.onmessage({data: JSON.stringify({...frame, snapshot: false, seq: 2,
      request, limit: 100})});
    socket.onmessage({data: JSON.stringify({...frame, snapshot: false, seq: 2,
      request, limit: 100, rows: [row(1)], rowBase: 1, more: false})});
    assert.deepEqual(Array.from(await exported, entry => entry.id), [2, 1]);
    assert(!socket.sent.some(message => message.op === 'resync'));
    f.stop();
    socket.onmessage({data: JSON.stringify({...frame, snapshot: false, rowBase: 1})});
    assert(!socket.sent.some(message => message.op === 'resync'), 'cancelled fragments stay ignored');
  })().catch(error => { console.error(error); process.exitCode = 1; });
}
