'use strict';

(() => {
  // Segunda iteración: píldora flotante + hoja inferior animada con controles
  // continuos. Cada maqueta es una pantalla con scroll real; nada toca el fw.
  const $ = (sel, root) => (root || document).querySelector(sel);
  const $$ = (sel, root) => [...(root || document).querySelectorAll(sel)];
  const fmt = w => (Math.round(w * 10) / 10).toFixed(1);
  const clampW = w => Math.min(500, Math.max(1, w));
  const el = (tag, cls, html) => {
    const node = document.createElement(tag);
    if (cls) node.className = cls;
    if (html != null) node.innerHTML = html;
    return node;
  };
  const ICON_SCALE = '<svg class="ic" viewBox="0 0 24 24" aria-hidden="true"><path d="M8 4h8M12 4v3M5 21h14M6.5 7h11l2.3 9.5A2.6 2.6 0 0 1 17.2 21H6.8a2.6 2.6 0 0 1-2.6-4.5z"/></svg>';
  const PRESETS = [
    { name: 'Espresso', badge: 'Factory', g: 36 },
    { name: 'Ristretto', badge: 'Factory', g: 27 },
    { name: 'Lungo', badge: 'Factory', g: 54 },
    { name: 'Weekend shot', badge: 'Custom', g: 40.5 },
  ];

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
  const polar = (cx, cy, r, deg) => {
    const rad = deg * Math.PI / 180;
    return [cx + r * Math.sin(rad), cy - r * Math.cos(rad)];
  };
  const arcPath = (cx, cy, r, a0, a1) => {
    const [x0, y0] = polar(cx, cy, r, a0), [x1, y1] = polar(cx, cy, r, a1);
    return `M ${x0} ${y0} A ${r} ${r} 0 ${Math.abs(a1 - a0) > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  };

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

  // ---------- Réplica del Home ----------
  function buildHome(mock) {
    const scroll = el('div', 'mockScroll');
    mock.append(scroll);

    const hero = el('section', 'shotHero');
    hero.hidden = true;
    hero.innerHTML = `
      <div class="heroBody">
        <p class="heroLabel"><span class="heroDot" aria-hidden="true"></span><span class="jsHeroState"></span><span class="jsHeroPreset"></span></p>
        <p class="heroNum"><b class="jsHeroWeight">0.0</b><span class="jsHeroGoal"></span></p>
        <div class="heroTrack" role="img"><i class="jsHeroBar"></i></div>
        <p class="heroChips"><span class="heroChip jsHeroElapsed"></span><span class="heroChip jsHeroFlow"></span></p>
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
    if (ctx.hero.goal) ctx.hero.goal.textContent = '/ ' + fmt(ctx.state.w) + ' g';
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
    if (value === ctx.state.w && !opts.force) return;
    ctx.state.w = value;
    const preset = PRESETS.find(p => p.name === ctx.state.preset);
    if (preset) preset.g = value;
    renderTargets(ctx);
    if (!opts.silent) showToast(ctx, `Target set to ${fmt(value)} g · ${ctx.state.preset}`);
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
  function stopShot(ctx, finished) {
    if (!ctx.shot) return;
    clearInterval(ctx.shot.timer);
    ctx.shot = null;
    setStartButton(ctx, false);
    if (finished) {
      ctx.hero.state.textContent = 'Complete';
      ctx.hero.root.classList.remove('live');
      ctx.hero.elapsed.textContent = ctx.hero.elapsed.textContent + ' · done';
      setTimeout(() => { ctx.hero.root.hidden = true; }, 2100);
    } else {
      ctx.hero.root.hidden = true;
    }
  }
  function startShot(ctx, mode) {
    if (ctx.shot) return stopShot(ctx, false);
    if (ctx.closeSheet) ctx.closeSheet();
    ctx.scroll.scrollTo({ top: 0, behavior: 'smooth' });
    const rinse = mode === 'rinse';
    const target = rinse ? 3 : ctx.state.w;
    const hero = ctx.hero;
    hero.root.hidden = false;
    hero.root.classList.add('live');
    hero.root.style.setProperty('--goal-pct', '100%');
    hero.state.textContent = rinse ? 'Rinsing' : 'Brewing';
    hero.preset.textContent = rinse ? '' : ' · ' + ctx.state.preset;
    hero.weight.textContent = '0.0';
    hero.goal.textContent = rinse ? '' : '/ ' + fmt(ctx.state.w) + ' g';
    hero.bar.style.width = '0%';
    hero.elapsed.textContent = '0.0 s';
    hero.flow.textContent = '';
    let t = 0, cur = 0;
    const step = () => {
      t += 0.09;
      if (rinse) {
        hero.weight.textContent = t.toFixed(1);
        hero.bar.style.width = Math.min(100, (t / target) * 100) + '%';
        if (t >= target) return stopShot(ctx, true);
      } else {
        const speed = 0.35 + 3.1 * Math.pow(Math.max(0, 1 - cur / target), 1.6);
        cur = Math.min(target, cur + speed * 0.09);
        hero.weight.textContent = fmt(cur);
        hero.bar.style.width = (cur / target) * 100 + '%';
        hero.flow.textContent = speed.toFixed(1) + ' g/s';
        if (cur >= target - 0.05) {
          hero.bar.style.width = '100%';
          return stopShot(ctx, true);
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

  // ---------- Píldora + hoja inferior ----------
  function makeSheet(ctx, ctlFactory, opts) {
    const options = opts || {};
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
      ${options.valueRow === false ? '' : `
      <div class="qsheetVal">
        <p class="twVal"><span class="jsNum">36.0</span><small>g</small></p>
      </div>`}
      <div class="qsheetBody"></div>`;
    ctx.mock.append(backdrop, sheet);
    ctx.presetNameEls.push($('.jsPresetName', sheet));

    const pill = el('button', 'qpill');
    pill.type = 'button';
    pill.innerHTML = `${ICON_SCALE}<span>Target</span><b><span class="jsNum">36.0</span> g</b><svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m6 14 6-6 6 6"/></svg>`;
    ctx.mock.append(pill);

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

    drag(pill, {
      start: e => ({ y: e.clientY, moved: 0 }),
      move: (e, c) => { c.moved = Math.max(c.moved, c.y - e.clientY); },
      end: (e, c) => { if (c.moved > 10) open(); else if (!sheet.classList.contains('open')) open(); },
    });
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

  // ---------- Controles continuos ----------
  const toastEnd = ctx => showToast(ctx, `Target set to ${fmt(ctx.state.w)} g · ${ctx.state.preset}`);
  const bound = v => Math.max(10, Math.min(80, v));

  // Slider horizontal continuo reutilizable.
  function makeSlider(ctx, range, onEnd) {
    const node = el('div', 'qtrack');
    node.innerHTML = '<div class="qtrackRail"></div><div class="qtrackFill"></div><span class="qbubble"></span><span class="qthumb"></span>';
    const fill = $('.qtrackFill', node), thumb = $('.qthumb', node), bubble = $('.qbubble', node);
    const value = () => Number(node.dataset.v);
    const paint = () => {
      const pct = (value() - range.min) / (range.max - range.min) * 100;
      fill.style.width = pct + '%';
      thumb.style.left = pct + '%';
      bubble.style.left = pct + '%';
      bubble.textContent = fmt(value()) + ' g';
    };
    node.dataset.v = ctx.state.w;
    paint();
    drag(node, {
      start: () => node.classList.add('drag'),
      move: e => {
        const rect = node.getBoundingClientRect();
        const t = Math.max(0, Math.min(1, (e.clientX - rect.left) / rect.width));
        node.dataset.v = range.min + t * (range.max - range.min);
        paint();
        setW(ctx, value(), { silent: true });
      },
      end: () => { node.classList.remove('drag'); onEnd(value()); },
    });
    return { node, set(v) { node.dataset.v = v; paint(); }, value };
  }

  // 01 · Columna vertical
  function ctlColumn(ctx, body) {
    const MIN = 10, MAX = 80;
    body.innerHTML = `
      <div class="qcol">
        <div class="qcolInfo">
          <p class="twVal"><span class="jsNum">36.0</span><small>g</small></p>
          <p class="twSub"><b class="jsPresetName">Espresso</b> · arrastra la columna</p>
          <div class="qcolRange"><span>${MIN} g</span><span>${MAX} g</span></div>
        </div>
        <div class="qcolTrack"><div class="qcolFill"></div><div class="qcolThumb"><i></i></div></div>
      </div>`;
    ctx.presetNameEls.push($('.jsPresetName', body));
    const zone = $('.qcol', body), track = $('.qcolTrack', body);
    const fill = $('.qcolFill', body), thumb = $('.qcolThumb', body);
    const paint = () => {
      const t = (Math.max(MIN, Math.min(MAX, ctx.state.w)) - MIN) / (MAX - MIN);
      fill.style.height = t * 100 + '%';
      const th = thumb.offsetHeight;
      thumb.style.bottom = t * (track.clientHeight - th) + 'px';
    };
    paint();
    drag(zone, {
      move: e => {
        const r = track.getBoundingClientRect();
        const t = Math.max(0, Math.min(1, 1 - (e.clientY - r.top) / r.height));
        setW(ctx, MIN + t * (MAX - MIN), { silent: true });
        paint();
      },
      end: () => toastEnd(ctx),
    });
    return { resync: paint };
  }

  // 02 · Dial radial
  function ctlDial(ctx, body) {
    const D = { min: 10, max: 80, a0: -135, a1: 135, r: 96, c: 110 };
    body.innerHTML = `
      <div class="qdialStage">
        <svg viewBox="0 0 220 220" aria-hidden="true">
          <path class="dTrack" fill="none" stroke="var(--ln)" stroke-width="10" stroke-linecap="round"/>
          <path class="dTicks" fill="none" stroke="var(--mu)" stroke-width="2"/>
          <path class="dValue" fill="none" stroke="var(--ac)" stroke-width="10" stroke-linecap="round"/>
          <circle class="dThumb" r="11" fill="var(--sf)" stroke="var(--ac)" stroke-width="4"/>
        </svg>
        <div class="qdialCenter"><p class="twVal"><span class="jsNum">36.0</span><small>g</small></p><p class="twSub" style="margin:0"><b class="jsPresetName">Espresso</b></p></div>
      </div>
      <p class="qdialRange"><span>${D.min} g</span><span>${D.max} g</span></p>`;
    ctx.presetNameEls.push($('.jsPresetName', body));
    const svg = $('svg', body), value = $('.dValue', svg), thumb = $('.dThumb', svg);
    const angle = w => D.a0 + (Math.max(D.min, Math.min(D.max, w)) - D.min) / (D.max - D.min) * (D.a1 - D.a0);
    let ticks = '';
    for (let g = D.min; g <= D.max; g += 5) {
      const a = angle(g), big = g % 10 === 0;
      const [x0, y0] = polar(D.c, D.c, D.r - (big ? 18 : 13), a);
      const [x1, y1] = polar(D.c, D.c, D.r - 7, a);
      ticks += `M ${x0.toFixed(1)} ${y0.toFixed(1)} L ${x1.toFixed(1)} ${y1.toFixed(1)} `;
    }
    $('.dTicks', svg).setAttribute('d', ticks);
    $('.dTrack', svg).setAttribute('d', arcPath(D.c, D.c, D.r, D.a0, D.a1));
    const paint = () => {
      const a = angle(ctx.state.w);
      value.setAttribute('d', arcPath(D.c, D.c, D.r, D.a0, a));
      const [x, y] = polar(D.c, D.c, D.r, a);
      thumb.setAttribute('cx', x);
      thumb.setAttribute('cy', y);
    };
    paint();
    drag(svg, {
      move: e => {
        const r = svg.getBoundingClientRect();
        const dx = e.clientX - (r.left + r.width / 2), dy = e.clientY - (r.top + r.height / 2);
        let deg = Math.atan2(dx, -dy) * 180 / Math.PI;
        deg = Math.max(D.a0, Math.min(D.a1, deg));
        setW(ctx, D.min + (deg - D.a0) / (D.a1 - D.a0) * (D.max - D.min), { silent: true });
        paint();
      },
      end: () => toastEnd(ctx),
    });
    return { resync: paint };
  }

  // 03 · Doble slider
  function ctlDual(ctx, body) {
    const coarseRange = { min: 10, max: 80 }, fineRange = { min: 34, max: 38 };
    body.innerHTML = `
      <p class="qslideLabel"><span>Grueso</span><small>10 – 80 g</small></p>
      <div class="jsCoarse"></div>
      <p class="qslideLabel"><span>Fino</span><small>± 2 g · décimas</small></p>
      <div class="jsFine"></div>`;
    const recenter = v => { fineRange.min = v - 2; fineRange.max = v + 2; };
    const fine = makeSlider(ctx, fineRange, () => toastEnd(ctx));
    const coarse = makeSlider(ctx, coarseRange, v => { recenter(v); fine.set(v); toastEnd(ctx); });
    $('.jsFine', body).append(fine.node);
    $('.jsCoarse', body).append(coarse.node);
    return {
      resync() {
        coarse.set(ctx.state.w);
        recenter(ctx.state.w);
        fine.set(ctx.state.w);
      },
    };
  }

  // 04 · Número gigante
  function ctlBigNum(ctx, body) {
    body.innerHTML = `
      <div class="qbignum">
        <div class="qbignumMain">
          <p class="twVal"><span class="jsNum">36.0</span><small>g</small></p>
          <p class="twSub">Arrastra el número ↑ ↓ · <b class="jsPresetName">Espresso</b></p>
        </div>
        <div class="qrule"><div class="qruleIn"></div></div>
      </div>`;
    ctx.presetNameEls.push($('.jsPresetName', body));
    const zone = $('.qbignum', body), ruler = $('.qruleIn', body);
    const paint = () => { ruler.style.transform = `translateY(${-(ctx.state.w * 26) % 130}px)`; };
    paint();
    drag(zone, {
      start: e => { zone.classList.add('drag'); return { y: e.clientY, w: ctx.state.w, moved: 0 }; },
      move: (e, c) => {
        c.moved = Math.max(c.moved, Math.abs(e.clientY - c.y));
        setW(ctx, bound(c.w + (c.y - e.clientY) * 0.03), { silent: true });
        paint();
      },
      end: (e, c) => {
        zone.classList.remove('drag');
        if (c.moved > 3) toastEnd(ctx);
      },
    });
    return { resync: paint };
  }

  // 05 · Regla con lupa
  function ctlRuler(ctx, body) {
    const MIN = 10, MAX = 80, PPG = 16;
    body.innerHTML = `
      <div class="qlens">
        <div class="qlensStrip"><div class="qlensTicks"></div><div class="qlensReticle"></div></div>
        <div class="qlensHint"><span>${MIN} g</span><span>desliza la regla · suelta con inercia</span><span>${MAX} g</span></div>
      </div>`;
    const strip = $('.qlensStrip', body), ticksHost = $('.qlensTicks', body);
    const marks = [];
    for (let v = MIN; v <= MAX; v += 0.5) {
      const tick = el('i');
      tick.dataset.v = v;
      tick.style.left = ((v - MIN) * PPG) + 'px';
      ticksHost.append(tick);
      marks.push({ v, node: tick, label: null });
      if (v % 5 === 0) {
        const label = el('span', null, String(v));
        label.style.left = ((v - MIN) * PPG) + 'px';
        ticksHost.append(label);
        marks.push({ v, node: label, label: true });
      }
    }
    let raf = 0, vel = 0;
    const paint = () => {
      const rect = strip.getBoundingClientRect();
      const cx = rect.width / 2;
      const tx = cx - (ctx.state.w - MIN) * PPG;
      ticksHost.style.transform = `translateX(${tx}px)`;
      for (const m of marks) {
        const d = Math.abs((m.v - MIN) * PPG + tx - cx);
        const lens = Math.exp(-Math.pow(d / 85, 2));
        if (m.label) {
          m.node.style.opacity = (0.15 + 0.85 * lens).toFixed(2);
          m.node.style.transform = `translateX(-50%) scale(${(0.85 + 0.5 * lens).toFixed(2)})`;
        } else {
          m.node.style.height = (11 + 21 * lens).toFixed(1) + 'px';
          m.node.style.opacity = (0.5 + 0.5 * lens).toFixed(2);
          m.node.style.background = lens > 0.55 ? 'var(--ac)' : '';
        }
      }
    };
    const stopMomentum = () => { if (raf) cancelAnimationFrame(raf); raf = 0; vel = 0; };
    drag($('.qlens', body), {
      start: e => { stopMomentum(); return { x: e.clientX, lastX: e.clientX, lastT: performance.now(), vel: 0 }; },
      move: (e, c) => {
        const now = performance.now();
        const dt = Math.max(1, now - c.lastT);
        c.vel = 0.8 * c.vel + 0.2 * ((e.clientX - c.lastX) / dt);
        c.lastX = e.clientX;
        c.lastT = now;
        setW(ctx, bound(ctx.state.w + (e.clientX - c.x) / PPG), { silent: true });
        paint();
      },
      end: (e, c) => {
        vel = Math.max(-2.4, Math.min(2.4, c.vel));
        const glide = () => {
          setW(ctx, bound(ctx.state.w + vel * 16 / PPG), { silent: true });
          vel *= 0.94;
          paint();
          if (Math.abs(vel) > 0.012) raf = requestAnimationFrame(glide);
          else { raf = 0; toastEnd(ctx); }
        };
        if (Math.abs(vel) > 0.05) raf = requestAnimationFrame(glide);
        else toastEnd(ctx);
      },
    });
    return { resync: paint };
  }

  // 06 · Micrómetro
  function ctlMicrometer(ctx, body) {
    body.innerHTML = `
      <div class="qmic">
        <div class="qmicScale"><div class="qmicScaleIn"></div><div class="qmicNeedle"></div></div>
        <div class="qmicDrum"><div class="qmicKnurl"></div><div class="qmicAxis"></div></div>
        <div class="qmicRead"><small>gira el rodillo · 0.02 g / px</small><small><b class="jsPresetName">Espresso</b></small></div>
      </div>`;
    ctx.presetNameEls.push($('.jsPresetName', body));
    const zone = $('.qmic', body), scaleIn = $('.qmicScaleIn', body);
    const knurl = $('.qmicKnurl', body), scale = $('.qmicScale', body);
    const ticks = [];
    for (let v = 5; v <= 100; v++) {
      const t = el('i');
      if (v % 5 === 0) t.classList.add('big');
      scaleIn.append(t);
      ticks.push({ v, node: t });
    }
    let px = 0;
    const paint = () => {
      const w = scale.getBoundingClientRect().width, cx = w / 2;
      for (const t of ticks) {
        const x = cx + (t.v - ctx.state.w) * 9;
        const vis = x > -8 && x < w + 8;
        t.node.style.display = vis ? '' : 'none';
        t.node.style.left = x.toFixed(1) + 'px';
        t.node.classList.toggle('now', Math.round(ctx.state.w) === t.v);
      }
      knurl.style.backgroundPosition = `${(px % 14).toFixed(1)}px 0, ${(px % 7).toFixed(1)}px 0, 0 0`;
    };
    paint();
    drag(zone, {
      start: e => ({ x: e.clientX, w: ctx.state.w, px0: px }),
      move: (e, c) => {
        const dx = e.clientX - c.x;
        px = c.px0 + dx;
        setW(ctx, bound(c.w + dx * 0.02), { silent: true });
        paint();
      },
      end: () => toastEnd(ctx),
    });
    return { resync: paint };
  }

  // 07 · Arco del shot
  function ctlShotArc(ctx, body) {
    const A = { min: 10, max: 80, a0: -145, a1: 145, r: 46, c: 50 };
    body.innerHTML = `
      <div class="qshot">
        <svg viewBox="0 0 100 100" aria-hidden="true">
          <path class="aTrack" fill="none" stroke="var(--ln)" stroke-width="4"/>
          <path class="aValue" fill="none" stroke="var(--ac)" stroke-width="6" stroke-linecap="round"/>
          <circle class="aThumb" r="5" fill="var(--sf)" stroke="var(--ac)" stroke-width="3"/>
        </svg>
        <button type="button" class="qshotBtn"><b><span class="jsNum">36.0</span> g</b><small>gira el anillo · toca para brew</small></button>
      </div>`;
    const zone = $('.qshot', body), svg = $('svg', body);
    const btn = $('.qshotBtn', body), value = $('.aValue', svg), thumb = $('.aThumb', svg);
    $('.aTrack', svg).setAttribute('d', arcPath(A.c, A.c, A.r, A.a0, A.a1));
    const angle = w => A.a0 + (Math.max(A.min, Math.min(A.max, w)) - A.min) / (A.max - A.min) * (A.a1 - A.a0);
    const paint = () => {
      const a = angle(ctx.state.w);
      value.setAttribute('d', arcPath(A.c, A.c, A.r, A.a0, a));
      const [x, y] = polar(A.c, A.c, A.r, a);
      thumb.setAttribute('cx', x);
      thumb.setAttribute('cy', y);
    };
    paint();
    drag(zone, {
      start: e => ({ moved: 0, x: e.clientX, y: e.clientY }),
      move: (e, c) => {
        c.moved = Math.max(c.moved, Math.hypot(e.clientX - c.x, e.clientY - c.y));
        if (c.moved < 5) return;
        const r = svg.getBoundingClientRect();
        const dx = e.clientX - (r.left + r.width / 2), dy = e.clientY - (r.top + r.height / 2);
        let deg = Math.atan2(dx, -dy) * 180 / Math.PI;
        deg = Math.max(A.a0, Math.min(A.a1, deg));
        setW(ctx, A.min + (deg - A.a0) / (A.a1 - A.a0) * (A.max - A.min), { silent: true });
        paint();
      },
      end: (e, c) => {
        if (c.moved < 5) { startShot(ctx); return; }
        toastEnd(ctx);
      },
    });
    return { resync: paint };
  }

  // 08 · Scrub adaptativo
  function ctlAdaptive(ctx, body) {
    body.innerHTML = `
      <div class="qada">
        <p class="twVal"><span class="jsNum">36.0</span><small>g</small></p>
        <div class="qadaMeter"><i></i></div>
        <div class="qadaLabels"><span>fino</span><span>ganancia</span><span>grueso</span></div>
        <p class="twHint">Despacio = precisión · rápido = rango</p>
      </div>`;
    const zone = $('.qada', body), meter = $('.qadaMeter i', body);
    drag(zone, {
      start: e => { zone.classList.add('drag'); return { y: e.clientY, t: performance.now(), gain: 0.015 }; },
      move: (e, c) => {
        const now = performance.now(), dt = Math.max(4, now - c.t);
        const dy = e.clientY - c.y;
        const vel = Math.abs(dy) / dt;
        c.gain = 0.015 + Math.min(0.28, vel * 0.03);
        setW(ctx, bound(ctx.state.w - dy * c.gain), { silent: true });
        c.y = e.clientY;
        c.t = now;
        meter.style.width = (12 + (c.gain - 0.015) / 0.28 * 88).toFixed(0) + '%';
      },
      end: () => { zone.classList.remove('drag'); meter.style.width = '12%'; toastEnd(ctx); },
    });
    return { resync: () => {} };
  }

  // 09 · Joystick relativo
  function ctlJoystick(ctx, body) {
    body.innerHTML = `
      <div class="qjoy" aria-label="Relative adjustment pad">
        <div class="qjoyOrigin"></div><div class="qjoyLine"></div>
        <div class="qjoyPuck"><i></i></div><div class="qjoyDelta">+0.0 g</div>
      </div>
      <p class="twHint" style="margin-top:.5rem">Toca y arrastra: derecha suma, izquierda resta · suelta y repite</p>`;
    const zone = $('.qjoy', body);
    const origin = $('.qjoyOrigin', body), line = $('.qjoyLine', body);
    const puck = $('.qjoyPuck', body), delta = $('.qjoyDelta', body);
    const local = e => {
      const r = zone.getBoundingClientRect();
      return [e.clientX - r.left, e.clientY - r.top];
    };
    drag(zone, {
      start: e => {
        const [x, y] = local(e);
        zone.classList.add('on');
        origin.style.left = x + 'px';
        origin.style.top = y + 'px';
        line.style.left = x + 'px';
        line.style.top = y + 'px';
        line.style.width = '0px';
        puck.style.left = x + 'px';
        puck.style.top = y + 'px';
        delta.style.left = x + 'px';
        delta.style.top = (y - 34) + 'px';
        delta.textContent = '+0.0 g';
        return { x0: x, w: ctx.state.w };
      },
      move: (e, c) => {
        const [x, y] = local(e);
        const d = (x - c.x0) * 0.06;
        setW(ctx, bound(c.w + d), { silent: true });
        line.style.width = Math.abs(x - c.x0) + 'px';
        line.style.left = Math.min(c.x0, x) + 'px';
        puck.style.left = x + 'px';
        puck.style.top = y + 'px';
        delta.style.left = x + 'px';
        delta.style.top = (y - 34) + 'px';
        delta.textContent = (d >= 0 ? '+' : '−') + fmt(Math.abs(d)) + ' g';
      },
      end: () => { zone.classList.remove('on'); toastEnd(ctx); },
    });
    return { resync: () => {} };
  }

  // 10 · Corona doble
  function ctlCrown(ctx, body) {
    const O = { min: 10, max: 80, a0: -135, a1: 135, r: 97, ri: 64, c: 110, span: 3 };
    body.innerHTML = `
      <div class="qcrown">
        <svg viewBox="0 0 220 220" aria-hidden="true">
          <path class="oTrack" fill="none" stroke="var(--ln)" stroke-width="9" stroke-linecap="round"/>
          <path class="oTicks" fill="none" stroke="var(--mu)" stroke-width="2"/>
          <path class="oValue" fill="none" stroke="var(--ac)" stroke-width="9" stroke-linecap="round"/>
          <circle class="oThumb" r="9" fill="var(--sf)" stroke="var(--ac)" stroke-width="3.5"/>
          <path class="iTrack" fill="none" stroke="var(--ln)" stroke-width="12" stroke-linecap="round"/>
          <path class="iValue" fill="none" stroke="color-mix(in srgb,var(--ac) 55%,var(--sf))" stroke-width="12" stroke-linecap="round"/>
          <circle class="iThumb" r="7" fill="var(--sf)" stroke="var(--ac)" stroke-width="3"/>
        </svg>
        <div class="qcrownCenter"><p class="twVal"><span class="jsNum">36.0</span><small>g</small></p><small class="jsPresetName">Espresso</small></div>
      </div>
      <p class="twHint" style="text-align:center;margin-top:.4rem">Anillo exterior: 10–80 g · interior: ±3 g</p>`;
    ctx.presetNameEls.push($('.jsPresetName', body));
    const svg = $('svg', body);
    const oAngle = w => O.a0 + (Math.max(O.min, Math.min(O.max, w)) - O.min) / (O.max - O.min) * (O.a1 - O.a0);
    let base = ctx.state.w;
    const iAngle = w => O.a0 + Math.max(-O.span, Math.min(O.span, w - base)) / O.span * (O.a1 - O.a0);
    let ticks = '';
    for (let g = O.min; g <= O.max; g += 5) {
      const a = oAngle(g), big = g % 10 === 0;
      const [x0, y0] = polar(O.c, O.c, O.r - (big ? 16 : 11), a);
      const [x1, y1] = polar(O.c, O.c, O.r - 6, a);
      ticks += `M ${x0.toFixed(1)} ${y0.toFixed(1)} L ${x1.toFixed(1)} ${y1.toFixed(1)} `;
    }
    $('.oTicks', svg).setAttribute('d', ticks);
    $('.oTrack', svg).setAttribute('d', arcPath(O.c, O.c, O.r, O.a0, O.a1));
    $('.iTrack', svg).setAttribute('d', arcPath(O.c, O.c, O.ri, O.a0, O.a1));
    const paint = () => {
      const oa = oAngle(ctx.state.w);
      $('.oValue', svg).setAttribute('d', arcPath(O.c, O.c, O.r, O.a0, oa));
      const [ox, oy] = polar(O.c, O.c, O.r, oa);
      const ot = $('.oThumb', svg);
      ot.setAttribute('cx', ox); ot.setAttribute('cy', oy);
      const ia = iAngle(ctx.state.w);
      $('.iValue', svg).setAttribute('d', arcPath(O.c, O.c, O.ri, O.a0, ia));
      const [ix, iy] = polar(O.c, O.c, O.ri, ia);
      const it = $('.iThumb', svg);
      it.setAttribute('cx', ix); it.setAttribute('cy', iy);
    };
    paint();
    drag(svg, {
      start: e => {
        const r = svg.getBoundingClientRect();
        const dx = e.clientX - (r.left + r.width / 2), dy = e.clientY - (r.top + r.height / 2);
        const rv = Math.hypot(dx, dy) / r.width * 220;
        return { ring: rv > 80 ? 'outer' : 'inner', base: base };
      },
      move: (e, c) => {
        const r = svg.getBoundingClientRect();
        const dx = e.clientX - (r.left + r.width / 2), dy = e.clientY - (r.top + r.height / 2);
        let deg = Math.atan2(dx, -dy) * 180 / Math.PI;
        deg = Math.max(O.a0, Math.min(O.a1, deg));
        const t = (deg - O.a0) / (O.a1 - O.a0);
        if (c.ring === 'outer') {
          setW(ctx, O.min + t * (O.max - O.min), { silent: true });
          base = ctx.state.w;
        } else {
          setW(ctx, bound(c.base - O.span + t * O.span * 2), { silent: true });
        }
        paint();
      },
      end: () => { base = ctx.state.w; toastEnd(ctx); },
    });
    return { resync() { base = ctx.state.w; paint(); } };
  }

  // ---------- Init ----------
  const CONTROLS = {
    '01': ctlColumn, '02': ctlDial, '03': ctlDual, '04': ctlBigNum, '05': ctlRuler,
    '06': ctlMicrometer, '07': ctlShotArc, '08': ctlAdaptive, '09': ctlJoystick, '10': ctlCrown,
  };
  const WITH_OWN_VALUE = { '01': 1, '02': 1, '04': 1, '07': 1, '08': 1, '10': 1 };
  $$('.mock[data-chrome]').forEach(mock => {
    mock.insertAdjacentHTML('afterbegin', TOP);
    mock.insertAdjacentHTML('beforeend', NAV);
    mock.append(standardDock());
    const ctx = buildHome(mock);
    wireDock(ctx, mock);
    const variant = mock.dataset.variant;
    if (CONTROLS[variant]) makeSheet(ctx, CONTROLS[variant], { valueRow: !WITH_OWN_VALUE[variant] });
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
