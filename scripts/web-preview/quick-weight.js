'use strict';

(() => {
  // Cuarta iteración: control único — micrómetro de tambor con aceleración
  // por velocidad + Reset al peso predeterminado del perfil. El lápiz de la
  // card Last shot (sin fondo, alineado al objetivo) invoca la hoja.
  const $ = (sel, root) => (root || document).querySelector(sel);
  const $$ = (sel, root) => [...(root || document).querySelectorAll(sel)];
  const fmt = w => (Math.round(w * 10) / 10).toFixed(1);
  const clampW = w => Math.min(500, Math.max(1, w));
  const bound = v => Math.max(10, Math.min(80, v));
  const el = (tag, cls, html) => {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (html != null) node.innerHTML = html;
    return node;
  };
  const ICON_SCALE = '<svg class="ic" viewBox="0 0 24 24" aria-hidden="true"><path d="M8 4h8M12 4v3M5 21h14M6.5 7h11l2.3 9.5A2.6 2.6 0 0 1 17.2 21H6.8a2.6 2.6 0 0 1-2.6-4.5z"/></svg>';
  const ICON_PEN = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 20h4L19 9l-4-4L4 16v4zM13.5 6.5l4 4"/></svg>';
  const ICON_RESET = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 12a9 9 0 1 0 2.6-7.4M3 4v5h5"/></svg>';
  const PRESETS = [
    { name: 'Espresso', badge: 'Factory', def: 36, g: 36 },
    { name: 'Ristretto', badge: 'Factory', def: 27, g: 27 },
    { name: 'Lungo', badge: 'Factory', def: 54, g: 54 },
    { name: 'Weekend shot', badge: 'Custom', def: 40.5, g: 40.5 },
  ];
  const SPARK = `
    <svg viewBox="0 0 100 30" preserveAspectRatio="none" aria-hidden="true">
      <path class="fill" d="M0,27 C12,26 18,21 28,16 C40,10 52,8 66,7 C78,6 90,5 100,4 L100,30 L0,30 z"/>
      <path class="line" d="M0,27 C12,26 18,21 28,16 C40,10 52,8 66,7 C78,6 90,5 100,4"/>
    </svg>`;

  function drag(node, handlers) {
    node.addEventListener('pointerdown', event => {
      if (event.button !== 0 && event.pointerType === 'mouse') return;
      event.preventDefault();
      try { node.setPointerCapture(event.pointerId); } catch { /* synthetic events cannot capture */ }
      const ctx = handlers.start ? handlers.start(event) : {};
      const move = ev => handlers.move && handlers.move(ev, ctx);
      const end = ev => {
        node.removeEventListener('pointermove', move);
        node.removeEventListener('pointerup', end);
        node.removeEventListener('pointercancel', end);
        handlers.end && handlers.end(ev, ctx);
      };
      node.addEventListener('pointermove', move);
      node.addEventListener('pointerup', end);
      node.addEventListener('pointercancel', end);
    });
  }
  const speedMul = (vel, cap, k) => 1 + Math.min(cap - 1, Math.abs(vel || 0) * k);

  // ---------- Chrome común ----------
  const TOP = `
  <div class="mockTop">
    <div class="brand"><svg class="brandMark" viewBox="0 0 36 48" aria-hidden="true"><use href="#brandMark"/></svg><span><small>Open</small>Brew by Weight</span></div>
    <div class="headerSignals">
      <span class="signalIndicator" data-level="3" title="Wi-Fi: strong"><svg viewBox="0 0 24 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path data-step="3" d="M3 8.5a14 14 0 0 1 18 0"/><path data-step="2" d="M6.2 12a9 9 0 0 1 11.6 0"/><path data-step="1" d="M9.4 15.4a4 4 0 0 1 5.2 0"/></g><circle cx="12" cy="19" r="1.3" fill="currentColor"/></svg></span>
      <span class="signalIndicator" data-level="3" title="Bluetooth: Acaia Lunar"><svg viewBox="0 0 32 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="m3 8.5 8 7-4 3.5V5l4 3.5L3 15.5"/><g fill="currentColor" stroke="none"><rect data-step="1" x="17" y="15" width="3.5" height="5" rx="1"/><rect data-step="2" x="22" y="10" width="3.5" height="10" rx="1"/><rect data-step="3" x="27" y="4" width="3.5" height="16" rx="1"/></g></g></svg></span>
    </div>
  </div>`;
  const NAV = `
  <nav class="pageNav" aria-label="Pages">
    <a class="active" aria-current="page"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m3 10 9-7 9 7v10H3zM9 20v-7h6v7"/></svg><span>Home</span></a>
    <a><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 4v16h18M6 16l5-5 4 2 6-8"/></svg><span>Stats</span></a>
    <a><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 11a9 9 0 1 1 2.6 7.4M3 4v7h7M12 7v5l3 2"/></svg><span>History</span></a>
    <a><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h2m6 0h8M4 17h8m6 0h2"/><circle cx="9" cy="7" r="3"/><circle cx="15" cy="17" r="3"/></svg><span>Settings</span></a>
    <a><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m12 3 8 3v6c0 4-4 7-8 9-4-2-8-5-8-9V6z"/><rect x="9" y="10" width="6" height="5" rx="1"/><path d="M10 10V8a2 2 0 0 1 4 0v2"/></svg><span>Admin</span></a>
  </nav>`;
  const standardDock = () => {
    const dock = el('div', 'mockDock');
    dock.innerHTML = `
      <button type="button" class="btnGlyph ghost narrow jsRinse"><span class="g">≋</span><span class="t">Rinse</span></button>
      <button type="button" class="btnGlyph jsStart"><span class="g">▶</span><span class="t">Start shot</span></button>
      <button type="button" class="btnGlyph ghost narrow jsPush"><span class="g">!</span><span class="t">Push</span></button>`;
    return dock;
  };

  // ---------- Réplica del Home vigente (card Last shot visible) ----------
  function buildHome(mock) {
    const scroll = el('div', 'mockScroll');
    mock.append(scroll);

    const hero = el('section', 'shotHero');
    hero.innerHTML = `
      <div class="heroBody">
        <p class="heroLabel"><span class="heroDot" aria-hidden="true"></span><span class="jsHeroState">Last shot</span><span class="jsHeroPreset"> · Espresso</span></p>
        <p class="heroNum"><b class="jsHeroWeight">36.2</b><span class="jsHeroGoal">/ 36.0 g</span></p>
        <div class="heroTrack" role="img"><i class="jsHeroBar" style="width:100%"></i></div>
        <p class="heroChips"><span class="heroChip jsHeroElapsed">27.4 s</span><span class="heroChip jsHeroFlow">2.1 g/s</span></p>
        <div class="heroSpark">${SPARK}</div>
      </div>`;
    scroll.append(hero);

    const quick = el('fieldset');
    quick.innerHTML = `
      <legend>Quick Settings</legend>
      <div class="homeSwitchGrid">
        <label class="mkBrew" for="x"><input type="checkbox" role="switch" checked><span class="swL">Brew by weight<span class="swS">Stop at target weight</span></span><span class="xlsw" aria-hidden="true"><i></i></span></label>
        <div class="homeGuardGrid">
          <label class="switchRow kcard"><input type="checkbox" role="switch"><span class="swL">No-scale BBW<span class="swS">Warn before brewing</span></span><span class="ktrack" aria-hidden="true"><i class="kknob"></i></span></label>
          <label class="switchRow kcard"><input type="checkbox" role="switch" checked><span class="swL">A→M time guard<span class="swS">32 s limit after scale loss</span></span><span class="ktrack" aria-hidden="true"><i class="kknob"></i></span></label>
          <label class="switchRow kcard"><input type="checkbox" role="switch" checked><span class="swL">Slow extraction guard<span class="swS">Allow a slower extraction</span></span><span class="ktrack" aria-hidden="true"><i class="kknob"></i></span></label>
          <label class="switchRow kcard"><input type="checkbox" role="switch" checked><span class="swL">Fast extraction guard<span class="swS">Minimum extraction time</span></span><span class="ktrack" aria-hidden="true"><i class="kknob"></i></span></label>
          <label class="switchRow kcard"><input type="checkbox" role="switch" checked><span class="swL">Avoid accidental touch<span class="swS">Ignore brief paddle touches</span></span><span class="ktrack" aria-hidden="true"><i class="kknob"></i></span></label>
          <label class="switchRow kcard"><input type="checkbox" role="switch" checked><span class="swL">Cup protection<span class="swS">Stop if the cup is removed</span></span><span class="ktrack" aria-hidden="true"><i class="kknob"></i></span></label>
        </div>
      </div>`;
    scroll.append(quick);
    const brewInput = $('input', quick);
    const guards = $('.homeGuardGrid', quick);
    brewInput.addEventListener('change', () => guards.classList.toggle('fieldOff', !brewInput.checked));

    const acc = el('div', 'presetAcc');
    const presets = el('fieldset');
    presets.innerHTML = '<legend>Presets</legend>';
    presets.append(acc);
    scroll.append(presets);

    const equip = el('fieldset', 'mkEquip');
    equip.innerHTML = `
      <legend>Equipment</legend>
      <details class="lampRow"><summary><i class="lamp" aria-hidden="true"></i><span class="devName">Machine</span><span class="lampState stateReady">Ready</span><svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg></details>
      <details class="lampRow"><summary><i class="lamp" aria-hidden="true"></i><span class="devName">Scale</span><span class="lampState">Connected · Acaia Lunar</span><svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg></details>
      <details class="lampRow"><summary><i class="lamp" aria-hidden="true"></i><span class="devName">Cup</span><span class="lampState">Present · 142.5 g</span><svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg></details>`;
    scroll.append(equip);

    const ctx = {
      mock, scroll, state: { w: 36, preset: 'Espresso' }, shot: null,
      hero: {
        root: hero, state: $('.jsHeroState', hero), preset: $('.jsHeroPreset', hero),
        weight: $('.jsHeroWeight', hero), goal: $('.jsHeroGoal', hero),
        bar: $('.jsHeroBar', hero), elapsed: $('.jsHeroElapsed', hero), flow: $('.jsHeroFlow', hero),
      },
      presetNameEls: [], resyncs: [], closeSheet: null, dockStart: null,
    };

    PRESETS.forEach((p, index) => {
      const item = el('div', 'presetAccItem' + (index === 0 ? ' open' : ''));
      item.innerHTML = `
        <button type="button" class="presetAccHead" ${index === 0 ? 'aria-expanded="true"' : ''}>
          <span class="presetAccDot" aria-hidden="true"></span>
          <span class="presetAccName">${p.name}</span>
          <span class="presetAccBadge">${p.badge}</span>
          <span class="presetAccTarget"></span>
          <span class="presetAccChev" aria-hidden="true">▾</span>
        </button>
        <div class="presetAccPanel"><div class="presetAccPanelIn"><div class="presetAccRows">
          <div class="guardRow guardFast"><span class="guardName"><i></i>Fast</span><span class="guardRule">Cuts at 43.5 g between 12 s and 28 s</span></div>
          <div class="guardRow guardBbw"><span class="guardName"><i></i>BBW</span><span class="guardRule jsBbwRule"></span></div>
          <div class="guardRow guardSlow"><span class="guardName"><i></i>Slow</span><span class="guardRule">Cuts at 34 g or more from 45 s</span></div>
        </div></div></div>`;
      $('.presetAccHead', item).addEventListener('click', () => selectPreset(ctx, index));
      acc.append(item);
    });
    ctx.accItems = $$('.presetAccItem', acc);

    const toastNode = el('p', 'mkToast');
    mock.append(toastNode);
    ctx.toastNode = toastNode;
    ctx.toastTimer = 0;

    renderTargets(ctx);
    return ctx;
  }

  function renderTargets(ctx) {
    $$('.jsNum', ctx.mock).forEach(node => { node.textContent = fmt(ctx.state.w); });
    ctx.accItems.forEach((item, index) => {
      const live = item.classList.contains('open');
      const g = live ? ctx.state.w : PRESETS[index].g;
      $('.presetAccTarget', item).textContent = 'Target: ' + fmt(g) + ' g';
      $('.jsBbwRule', item).textContent = `Cuts at ${fmt(g)} g between 26 s and 45 s`;
    });
    ctx.presetNameEls.forEach(node => { node.textContent = ctx.state.preset; });
    ctx.hero.goal.textContent = '/ ' + fmt(ctx.state.w) + ' g';
    ctx.hero.preset.textContent = ' · ' + ctx.state.preset;
  }

  function showToast(ctx, text) {
    ctx.toastNode.innerHTML = '<span aria-hidden="true">✓</span> ' + text;
    ctx.toastNode.classList.add('show');
    clearTimeout(ctx.toastTimer);
    ctx.toastTimer = setTimeout(() => ctx.toastNode.classList.remove('show'), 2200);
  }

  function setW(ctx, w, options) {
    const opts = options || {};
    const value = Math.round(clampW(w) * 10) / 10;
    if (value === ctx.state.w && !opts.force) return false;
    ctx.state.w = value;
    const preset = PRESETS.find(p => p.name === ctx.state.preset);
    if (preset) preset.g = value;
    renderTargets(ctx);
    if (!opts.silent) showToast(ctx, `Target set to ${fmt(value)} g · ${ctx.state.preset}`);
    return true;
  }

  function selectPreset(ctx, index) {
    const p = PRESETS[index];
    ctx.accItems.forEach((item, i) => {
      item.classList.toggle('open', i === index);
      $('.presetAccHead', item).setAttribute('aria-expanded', String(i === index));
    });
    ctx.state.preset = p.name;
    setW(ctx, p.g);
    ctx.resyncs.forEach(resync => resync());
  }

  // ---------- Simulación de tiro ----------
  function setStartButton(ctx, running) {
    const btn = ctx.dockStart;
    if (!btn) return;
    btn.classList.toggle('solid', running);
    btn.innerHTML = running
      ? '<span class="g">■</span><span class="t">Stop shot</span>'
      : '<span class="g">▶</span><span class="t">Start shot</span>';
  }
  function stopShot(ctx, actual) {
    if (!ctx.shot) return;
    clearInterval(ctx.shot.timer);
    ctx.shot = null;
    setStartButton(ctx, false);
    ctx.hero.state.textContent = 'Last shot';
    ctx.hero.root.classList.remove('live');
    ctx.hero.weight.textContent = fmt(actual);
    ctx.hero.elapsed.textContent = ctx.hero.elapsed.textContent.replace(' · done', '') + ' · done';
  }
  function startShot(ctx, mode) {
    if (ctx.shot) return stopShot(ctx, Number(ctx.hero.weight.textContent));
    if (ctx.closeSheet) ctx.closeSheet();
    ctx.scroll.scrollTo({ top: 0, behavior: 'smooth' });
    const rinse = mode === 'rinse';
    const target = rinse ? 3 : ctx.state.w;
    const hero = ctx.hero;
    hero.root.classList.add('live');
    hero.state.textContent = rinse ? 'Rinsing' : 'Brewing';
    hero.weight.textContent = '0.0';
    hero.bar.style.width = '0%';
    hero.elapsed.textContent = '0.0 s';
    hero.flow.textContent = '';
    let t = 0, cur = 0;
    const step = () => {
      t += 0.09;
      if (rinse) {
        hero.weight.textContent = t.toFixed(1);
        hero.bar.style.width = Math.min(100, (t / target) * 100) + '%';
        if (t >= target) return stopShot(ctx, target);
      } else {
        const speed = 0.35 + 3.1 * Math.pow(Math.max(0, 1 - cur / target), 1.6);
        cur = Math.min(target, cur + speed * 0.09);
        hero.weight.textContent = fmt(cur);
        hero.bar.style.width = (cur / target) * 100 + '%';
        hero.flow.textContent = speed.toFixed(1) + ' g/s';
        if (cur >= target - 0.05) {
          hero.bar.style.width = '100%';
          return stopShot(ctx, target);
        }
      }
      hero.elapsed.textContent = t.toFixed(1) + ' s';
    };
    ctx.shot = { timer: setInterval(step, 90) };
    setStartButton(ctx, true);
  }
  function wireDock(ctx, mock) {
    const start = $('.jsStart', mock);
    ctx.dockStart = start;
    start.addEventListener('click', () => startShot(ctx));
    $('.jsRinse', mock).addEventListener('click', () => startShot(ctx, 'rinse'));
    $('.jsPush', mock).addEventListener('click', () => showToast(ctx, 'Paddle pulsed (momentary)'));
  }

  // ---------- Lápiz del hero + hoja inferior ----------
  function makeSheet(ctx, ctlFactory) {
    const backdrop = el('div', 'mkBackdrop');
    const sheet = el('div', 'qsheet');
    sheet.setAttribute('role', 'dialog');
    sheet.setAttribute('aria-label', 'Set target weight');
    sheet.innerHTML = `
      <div class="qsheetGrab" aria-hidden="true"><i></i></div>
      <div class="qsheetHead">
        <p class="twLabel">${ICON_SCALE} Target weight</p>
        <span class="qsheetHp">· <b class="jsPresetName">Espresso</b></span>
        <button type="button" class="qsheetDone">Listo</button>
      </div>
      <div class="qsheetBody"></div>`;
    ctx.mock.append(backdrop, sheet);
    ctx.presetNameEls.push($('.jsPresetName', sheet));

    const body = $('.qsheetBody', sheet);
    const ctl = ctlFactory(ctx, body) || {};
    const open = () => {
      if (ctl.resync) ctl.resync();
      sheet.classList.add('open');
      backdrop.classList.add('show');
    };
    const close = () => {
      sheet.classList.remove('open');
      backdrop.classList.remove('show');
    };
    ctx.closeSheet = close;
    ctx.resyncs.push(() => ctl.resync && ctl.resync());

    const heroPen = el('span', 'qeditPen');
    heroPen.setAttribute('role', 'button');
    heroPen.setAttribute('tabindex', '0');
    heroPen.setAttribute('aria-label', 'Edit target weight');
    heroPen.innerHTML = ICON_PEN;
    heroPen.addEventListener('click', open);
    heroPen.addEventListener('keydown', e => {
      if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); open(); }
    });
    $('.heroNum', ctx.hero.root).append(heroPen);

    [$('.qsheetGrab', sheet), $('.qsheetHead', sheet)].forEach(zone => drag(zone, {
      start: e => { sheet.classList.add('nodrag'); return { y: e.clientY, dy: 0 }; },
      move: (e, c) => {
        c.dy = Math.max(0, e.clientY - c.y);
        sheet.style.transform = `translateY(${c.dy}px)`;
      },
      end: (e, c) => {
        sheet.classList.remove('nodrag');
        if (c.dy > 90) {
          sheet.style.transition = 'transform .3s ease-in';
          sheet.style.transform = 'translateY(105%)';
          setTimeout(() => {
            sheet.classList.remove('open');
            sheet.style.transition = '';
            sheet.style.transform = '';
            backdrop.classList.remove('show');
          }, 290);
        } else {
          sheet.style.transform = '';
        }
      },
    }));
    backdrop.addEventListener('click', close);
    $('.qsheetDone', sheet).addEventListener('click', close);
    return { sheet, body, open, close };
  }

  // ---------- Micrómetro: escala superior con aceleración + Reset ----------
  function ctlMicrometer(ctx, body) {
    body.innerHTML = `
      <div class="qsheetVal" style="padding:0 0 .1rem">
        <p class="twVal"><span class="jsNum">36.0</span><small>g</small></p>
        <button type="button" class="qreset jsReset">${ICON_RESET}<span>Reset · <b class="jsDef">36.0</b> g</span></button>
      </div>
      <div class="qmicZone jsZone">
        <div class="jsScale"></div>
      </div>`;
    const zone = $('.jsZone', body);
    const scale = el('div', 'qmicScale');
    const inner = el('div', 'qmicScaleIn');
    scale.append(inner);
    $('.jsScale', body).append(scale);
    const ticks = [];
    for (let v = 5; v <= 100; v++) {
      const t = el('i', v % 5 === 0 ? 'big' : '');
      inner.append(t);
      ticks.push({ v, node: t });
    }
    const vel = { x: 0, t: 0, v: 0 };
    const resetBtn = $('.jsReset', body);
    const presetDef = () => (PRESETS.find(p => p.name === ctx.state.preset) || {}).def;
    const paint = () => {
      const r = scale.getBoundingClientRect();
      const c = r.width / 2;
      const now = Math.round(ctx.state.w);
      for (const t of ticks) {
        const p = c + (t.v - ctx.state.w) * 9;
        t.node.style.display = p > -6 && p < r.width + 6 ? '' : 'none';
        t.node.style.left = p.toFixed(1) + 'px';
        t.node.classList.toggle('now', t.v === now);
      }
      $('.jsDef', body).textContent = fmt(presetDef());
      resetBtn.classList.toggle('show', Math.abs(ctx.state.w - presetDef()) > 0.04);
    };
    paint();
    drag(zone, {
      start: e => { vel.x = e.clientX; vel.t = performance.now(); vel.v = 0; return { w: ctx.state.w, x: e.clientX }; },
      move: (e, c) => {
        const dx = e.clientX - c.x;
        c.x = e.clientX;
        const now = performance.now(), dt = Math.max(4, now - vel.t);
        vel.v = 0.75 * vel.v + 0.25 * (Math.abs(e.clientX - vel.x) / dt);
        vel.x = e.clientX; vel.t = now;
        c.w = bound(c.w + dx * 0.008 * speedMul(vel.v, 10, 14));
        setW(ctx, c.w, { silent: true });
        paint();
      },
      end: () => showToast(ctx, `Target set to ${fmt(ctx.state.w)} g · ${ctx.state.preset}`),
    });
    resetBtn.addEventListener('click', () => {
      const def = presetDef();
      if (!setW(ctx, def)) showToast(ctx, `Already at the preset weight · ${fmt(def)} g`);
      else showToast(ctx, `Reset to ${fmt(def)} g · ${ctx.state.preset}`);
      paint();
    });
    return { resync: paint };
  }

  // ---------- Init ----------
  $$('.mock[data-chrome]').forEach(mock => {
    mock.insertAdjacentHTML('afterbegin', TOP);
    mock.insertAdjacentHTML('beforeend', NAV);
    mock.append(standardDock());
    const ctx = buildHome(mock);
    wireDock(ctx, mock);
    if (mock.dataset.variant === '01') makeSheet(ctx, ctlMicrometer);
    renderTargets(ctx);
  });

  const themeButton = document.getElementById('themeToggle');
  if (themeButton) {
    const states = [
      { cls: '', label: 'Tema: auto' },
      { cls: 'theme-light', label: 'Tema: claro' },
      { cls: 'theme-dark', label: 'Tema: oscuro' },
    ];
    let index = 0;
    themeButton.addEventListener('click', () => {
      index = (index + 1) % states.length;
      const state = states[index];
      document.documentElement.classList.remove('theme-light', 'theme-dark');
      if (state.cls) document.documentElement.classList.add(state.cls);
      themeButton.textContent = state.label;
    });
  }
})();
