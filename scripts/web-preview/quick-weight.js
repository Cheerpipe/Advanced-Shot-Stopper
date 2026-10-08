'use strict';

(() => {
  // Maquetas de ajuste rápido de peso: cada pantalla replica el Home actual y
  // agrega un control distinto. Todo es presentación local, sin dispositivo.
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
  const CHIP_WEIGHTS = [17, 18, 20, 25, 27, 30, 36, 40, 54];
  const FAN_WEIGHTS = [17, 18, 20, 25, 27, 36];

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

  // ---------- Chrome común: cabecera, navegación y dock ----------
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

  function standardDock() {
    const dock = el('div', 'mockDock');
    dock.innerHTML = `
      <button type="button" class="btnGlyph ghost narrow jsRinse"><span class="g">≋</span><span class="t">Rinse</span></button>
      <button type="button" class="btnGlyph jsStart"><span class="g">▶</span><span class="t">Start shot</span></button>
      <button type="button" class="btnGlyph ghost narrow jsPush"><span class="g">!</span><span class="t">Push</span></button>`;
    return dock;
  }

  // ---------- Réplica del Home actual ----------
  function buildHome(mock) {
    const body = el('div', 'mockBody');
    mock.append(body);

    const hero = el('section', 'shotHero');
    hero.hidden = true;
    hero.innerHTML = `
      <div class="heroBody">
        <p class="heroLabel"><span class="heroDot" aria-hidden="true"></span><span class="jsHeroState"></span><span class="jsHeroPreset"></span></p>
        <p class="heroNum"><b class="jsHeroWeight">0.0</b><span class="jsHeroGoal"></span></p>
        <div class="heroTrack" role="img"><i class="jsHeroBar"></i></div>
        <p class="heroChips"><span class="heroChip jsHeroElapsed"></span><span class="heroChip jsHeroFlow"></span></p>
      </div>`;
    body.append(hero);

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
    body.append(quick);
    const brewInput = $('input', quick);
    const guards = $('.homeGuardGrid', quick);
    brewInput.addEventListener('change', () => guards.classList.toggle('fieldOff', !brewInput.checked));

    const acc = el('div', 'presetAcc');
    const presets = el('fieldset');
    presets.innerHTML = '<legend>Presets</legend>';
    presets.append(acc);
    body.append(presets);

    const equip = el('fieldset', 'mkEquip');
    equip.innerHTML = `
      <legend>Equipment</legend>
      <details class="lampRow"><summary><i class="lamp" aria-hidden="true"></i><span class="devName">Machine</span><span class="lampState stateReady">Ready</span><svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg></details>
      <details class="lampRow"><summary><i class="lamp" aria-hidden="true"></i><span class="devName">Scale</span><span class="lampState">Connected · Acaia Lunar</span><svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg></details>
      <details class="lampRow"><summary><i class="lamp" aria-hidden="true"></i><span class="devName">Cup</span><span class="lampState">Present · 142.5 g</span><svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg></details>`;
    body.append(equip);

    const ctx = {
      mock, body, state: { w: 36, preset: 'Espresso' }, shot: null, sliders: [],
      hero: {
        root: hero, state: $('.jsHeroState', hero), preset: $('.jsHeroPreset', hero),
        weight: $('.jsHeroWeight', hero), goal: $('.jsHeroGoal', hero),
        bar: $('.jsHeroBar', hero), elapsed: $('.jsHeroElapsed', hero), flow: $('.jsHeroFlow', hero),
      },
      bbwRule: null, presetNameEls: [], dockStart: null,
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
    ctx.sliders.forEach(resync => resync());
    $$('.qchip.on', ctx.mock).forEach(chip => chip.classList.remove('on'));
    $$('.qchip', ctx.mock).forEach(chip => {
      if (Number(chip.dataset.g) === p.g) chip.classList.add('on');
    });
  }

  // ---------- Simulación de tiro ----------
  function setStartButton(ctx, running) {
    const btn = ctx.dockStart;
    if (btn) {
      btn.classList.toggle('solid', running);
      btn.innerHTML = running
        ? '<span class="g">■</span><span class="t">Stop shot</span>'
        : '<span class="g">▶</span><span class="t">Start shot</span>';
    }
    if (ctx.setRunning) ctx.setRunning(running);
  }

  function stopShot(ctx, finished) {
    if (!ctx.shot) return;
    clearInterval(ctx.shot.timer);
    ctx.shot = null;
    setStartButton(ctx, false);
    if (ctx.onShotEnd) ctx.onShotEnd();
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
        hero.elapsed.textContent = t.toFixed(1) + ' s';
        hero.flow.textContent = speed.toFixed(1) + ' g/s';
        if (cur >= target - 0.05) {
          hero.bar.style.width = '100%';
          return stopShot(ctx, true);
        }
        if (ctx.onShotTick) ctx.onShotTick(cur / target);
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

  // ---------- Tarjeta objetivo común ----------
  function targetCard(headHtml) {
    const card = el('div', 'twCard');
    card.innerHTML = `<p class="twLabel">${ICON_SCALE} Target weight</p>${headHtml}`;
    return card;
  }
  const bigValue = () => `<p class="twVal"><span class="jsNum">36.0</span><small>g</small></p>`;
  const presetSub = note => `<p class="twSub">Applies to <b class="jsPresetName">Espresso</b>${note}</p>`;

  // Slider táctil compartido: riel + relleno + burbuja, enganche por pasos.
  function makeSlider(ctx, opts) {
    const range = opts.range, snap = opts.snap || 1;
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
        let v = range.min + t * (range.max - range.min);
        v = Math.round(v / snap) * snap;
        node.dataset.v = Math.round(clampW(v) * 10) / 10;
        paint();
        setW(ctx, value(), { silent: true });
      },
      end: () => { node.classList.remove('drag'); opts.onEnd(value()); },
    });
    return { node, set(v) { node.dataset.v = v; paint(); }, value };
  }

  // ---------- 01 · Rueda selector ----------
  function variant01(ctx) {
    const card = targetCard(`
      <div class="qwheel" aria-label="Set the target weight with the wheels">
        <div class="qdrum int"><div class="qdrumList"></div></div>
        <span class="qwheelSep">.</span>
        <div class="qdrum dec"><div class="qdrumList"></div></div>
        <span class="qwheelUnit">g</span>
        <span class="qwheelBand" aria-hidden="true"></span>
      </div>
      ${presetSub(' · flick the wheels')}`);
    ctx.body.querySelector('.shotHero').after(card);
    ctx.presetNameEls.push($('.jsPresetName', card));
    const drums = $$('.qdrum', card), lists = $$('.qdrumList', card);
    const ranges = [{ min: 5, max: 100 }, { min: 0, max: 9 }];
    let idx = [Math.floor(ctx.state.w) - 5, Math.round((ctx.state.w % 1) * 10)];
    lists.forEach((list, d) => {
      for (let v = ranges[d].min; v <= ranges[d].max; v++) list.append(el('i', null, String(v)));
    });
    const itemH = () => lists[0].firstChild.offsetHeight, drumH = () => drums[0].offsetHeight;
    const tyFor = d => drumH() / 2 - (idx[d] + 0.5) * itemH();
    const paintHi = () => lists.forEach((list, d) => [...list.children].forEach((item, i) => item.classList.toggle('at', i === idx[d])));
    const paint = () => { lists.forEach((list, d) => { list.style.transform = `translateY(${tyFor(d)}px)`; }); paintHi(); };
    paint();
    const apply = silent => setW(ctx, (idx[0] + 5) + idx[1] / 10, { silent });
    drums.forEach((drum, d) => drag(drum, {
      start: e => {
        lists[d].style.transition = 'none';
        return { sx: e.clientY, base: tyFor(d), ty: tyFor(d) };
      },
      move: (e, c) => {
        c.ty = c.base + (e.clientY - c.sx);
        const prov = Math.max(0, Math.min(ranges[d].max - ranges[d].min, Math.round((drumH() / 2 - c.ty) / itemH() - 0.5)));
        if (prov !== idx[d]) { idx[d] = prov; apply(true); paintHi(); }
        lists[d].style.transform = `translateY(${c.ty}px)`;
      },
      end: (e, c) => {
        idx[d] = Math.max(0, Math.min(ranges[d].max - ranges[d].min, Math.round((drumH() / 2 - c.ty) / itemH() - 0.5)));
        lists[d].style.transition = '';
        paint();
        apply(false);
      },
    }));
    ctx.sliders.push(() => { idx = [Math.floor(ctx.state.w) - 5, Math.round((ctx.state.w % 1) * 10)]; paint(); });
  }

  // ---------- 02 · Riel magnético de presets ----------
  function variant02(ctx) {
    const MIN = 10, MAX = 80;
    const card = targetCard(`
      <div class="qsliderHead">${bigValue()}<span class="twHint">slide · snaps<br>to presets</span></div>
      <div class="qrail">
        <div class="qrailTrack">
          <div class="qrailRail"></div>
          <span class="qthumb"></span>
          <span class="qrailBubble"></span>
        </div>
      </div>
      <p class="twHint">Tap a mark to jump straight to it.</p>`);
    ctx.body.querySelector('.shotHero').after(card);
    const rail = $('.qrail', card), track = $('.qrailTrack', card);
    const thumb = $('.qthumb', track), bubble = $('.qrailBubble', track);
    const marks = CHIP_WEIGHTS.map(g => {
      const mark = el('button', 'qmark', `<i></i><span>${g}</span>`);
      mark.type = 'button';
      mark.dataset.g = g;
      mark.style.left = (g - MIN) / (MAX - MIN) * 100 + '%';
      track.append(mark);
      return mark;
    });
    const presetFor = g => (PRESETS.find(p => p.g === g) || {}).name || 'Custom';
    const nearest = w => CHIP_WEIGHTS.reduce((a, b) => (Math.abs(b - w) < Math.abs(a - w) ? b : a));
    const paint = (w, snapAnim) => {
      thumb.classList.toggle('snap', !!snapAnim);
      thumb.style.left = (w - MIN) / (MAX - MIN) * 100 + '%';
      marks.forEach(m => m.classList.toggle('on', Number(m.dataset.g) === nearest(w)));
    };
    paint(ctx.state.w);
    drag(rail, {
      start: () => { rail.classList.add('drag'); return {}; },
      move: (e, c) => {
        const r = track.getBoundingClientRect();
        const t = Math.max(0, Math.min(1, (e.clientX - r.left) / r.width));
        c.w = MIN + t * (MAX - MIN);
        paint(c.w);
        const n = nearest(c.w);
        bubble.style.left = (c.w - MIN) / (MAX - MIN) * 100 + '%';
        bubble.textContent = `${fmt(n)} g · ${presetFor(n)}`;
      },
      end: (e, c) => {
        rail.classList.remove('drag');
        const n = nearest(c.w == null ? ctx.state.w : c.w);
        paint(n, true);
        setW(ctx, n);
      },
    });
    marks.forEach(m => m.addEventListener('click', () => {
      setW(ctx, Number(m.dataset.g));
      paint(ctx.state.w, true);
    }));
    ctx.sliders.push(() => paint(ctx.state.w));
  }

  // ---------- 03 · Scrub vertical ----------
  function variant03(ctx) {
    const card = targetCard(`
      <div class="qscrub">
        <div class="qscrubMain">
          <p class="twVal qscrubVal"><span class="jsNum">36.0</span><small>g</small></p>
          ${presetSub(' · drag ↑↓, 0.1 g per tick')}
        </div>
        <div class="qrule"><div class="qruleIn"></div></div>
      </div>
      <span class="qscrubArrows" aria-hidden="true"><svg viewBox="0 0 24 24"><path d="m6 14 6-6 6 6"/></svg><svg viewBox="0 0 24 24"><path d="m6 10 6 6 6-6"/></svg></span>`);
    ctx.body.querySelector('.shotHero').after(card);
    ctx.presetNameEls.push($('.jsPresetName', card));
    const zone = $('.qscrub', card), ruler = $('.qruleIn', card);
    const paint = () => { ruler.style.transform = `translateY(${-(ctx.state.w * 28) % 140}px)`; };
    paint();
    drag(zone, {
      start: e => { zone.classList.add('drag'); return { y: e.clientY, w: ctx.state.w, moved: 0 }; },
      move: (e, c) => {
        c.moved = Math.max(c.moved, Math.abs(e.clientY - c.y));
        setW(ctx, c.w + (c.y - e.clientY) * 0.025, { silent: true });
        paint();
      },
      end: (e, c) => {
        zone.classList.remove('drag');
        if (c.moved > 3) showToast(ctx, `Target set to ${fmt(ctx.state.w)} g · ${ctx.state.preset}`);
        else setW(ctx, ctx.state.w + (e.clientY < zone.getBoundingClientRect().top + zone.offsetHeight / 2 ? 0.1 : -0.1));
      },
    });
  }

  // ---------- 04 · Dial radial ----------
  function polar(cx, cy, r, deg) {
    const rad = deg * Math.PI / 180;
    return [cx + r * Math.sin(rad), cy - r * Math.cos(rad)];
  }
  function arcPath(cx, cy, r, a0, a1) {
    const [x0, y0] = polar(cx, cy, r, a0), [x1, y1] = polar(cx, cy, r, a1);
    return `M ${x0} ${y0} A ${r} ${r} 0 ${Math.abs(a1 - a0) > 180 ? 1 : 0} 1 ${x1} ${y1}`;
  }
  const DIAL = { min: 10, max: 80, a0: -135, a1: 135, r: 96, c: 110 };
  const dialAngle = w => DIAL.a0 + (clampW(w) - DIAL.min) / (DIAL.max - DIAL.min) * (DIAL.a1 - DIAL.a0);
  function angleFromEvent(node, e) {
    const rect = node.getBoundingClientRect();
    const dx = e.clientX - (rect.left + rect.width / 2), dy = e.clientY - (rect.top + rect.height / 2);
    let deg = Math.atan2(dx, -dy) * 180 / Math.PI;
    return Math.max(DIAL.a0, Math.min(DIAL.a1, deg));
  }
  function variant04(ctx) {
    const card = targetCard(`
      <button type="button" class="qdialOpen" style="all:unset;display:block;width:100%;cursor:pointer">
        <div class="qsliderHead">${bigValue()}<span class="twHint">tap to open<br>the dial ↗</span></div>
      </button>`);
    ctx.body.querySelector('.shotHero').after(card);

    const backdrop = el('div', 'mkBackdrop');
    const wrap = el('div', 'qdialWrap');
    wrap.innerHTML = `
      <div class="qdial" role="dialog" aria-label="Set target weight">
        <p class="twLabel" style="justify-content:center">${ICON_SCALE} Set target weight</p>
        <div class="qdialStage">
          <svg viewBox="0 0 220 220" aria-hidden="true">
            <path class="dTrack" fill="none" stroke="var(--ln)" stroke-width="10" stroke-linecap="round"/>
            <path class="dTicks" fill="none" stroke="var(--mu)" stroke-width="2"/>
            <path class="dValue" fill="none" stroke="var(--ac)" stroke-width="10" stroke-linecap="round"/>
            <circle class="dThumb" r="11" fill="var(--sf)" stroke="var(--ac)" stroke-width="4"/>
          </svg>
          <div class="qdialCenter"><p class="twVal" style="font-size:2.6rem"><span class="jsNum">36.0</span><small>g</small></p><p class="twSub" style="margin:.1rem 0 0"><b class="jsPresetName">Espresso</b></p></div>
        </div>
        <p class="qdialRange"><span>10 g</span><span>80 g</span></p>
        <div class="qdialBtns">
          <button type="button" data-d="-1">− 1 g</button>
          <button type="button" data-d="1">+ 1 g</button>
          <button type="button" class="apply">Listo</button>
        </div>
      </div>`;
    ctx.mock.append(backdrop, wrap);
    ctx.presetNameEls.push($('.jsPresetName', wrap));

    const svg = $('.qdialStage svg', wrap), value = $('.dValue', svg), thumb = $('.dThumb', svg);
    const ticks = $('.dTicks', svg);
    let path = '';
    for (let g = DIAL.min; g <= DIAL.max; g += 5) {
      const a = dialAngle(g), long = g % 10 === 0;
      const [x0, y0] = polar(DIAL.c, DIAL.c, DIAL.r - (long ? 18 : 13), a);
      const [x1, y1] = polar(DIAL.c, DIAL.c, DIAL.r - 7, a);
      path += `M ${x0.toFixed(1)} ${y0.toFixed(1)} L ${x1.toFixed(1)} ${y1.toFixed(1)} `;
    }
    ticks.setAttribute('d', path);
    $('.dTrack', svg).setAttribute('d', arcPath(DIAL.c, DIAL.c, DIAL.r, DIAL.a0, DIAL.a1));

    const paint = () => {
      const a = dialAngle(ctx.state.w);
      value.setAttribute('d', arcPath(DIAL.c, DIAL.c, DIAL.r, DIAL.a0, a));
      const [x, y] = polar(DIAL.c, DIAL.c, DIAL.r, a);
      thumb.setAttribute('cx', x); thumb.setAttribute('cy', y);
      renderTargets(ctx);
    };
    let openW = 36;
    const open = () => { openW = ctx.state.w; paint(); wrap.classList.add('show'); backdrop.classList.add('show'); };
    const close = apply => {
      wrap.classList.remove('show'); backdrop.classList.remove('show');
      if (!apply) setW(ctx, openW, { silent: true, force: true });
      else showToast(ctx, `Target set to ${fmt(ctx.state.w)} g · ${ctx.state.preset}`);
    };
    $('.qdialOpen', card).addEventListener('click', open);
    backdrop.addEventListener('click', () => close(false));
    drag(svg, {
      move: e => { setW(ctx, DIAL.min + (angleFromEvent(svg, e) - DIAL.a0) / (DIAL.a1 - DIAL.a0) * (DIAL.max - DIAL.min), { silent: true }); paint(); },
    });
    $$('.qdialBtns button', wrap).forEach(btn => {
      if (btn.dataset.d) btn.addEventListener('click', () => { setW(ctx, ctx.state.w + Number(btn.dataset.d), { silent: true }); paint(); });
      else btn.addEventListener('click', () => close(true));
    });
  }

  // ---------- 05 · Hoja con doble slider ----------
  function variant05(ctx) {
    const card = targetCard(`
      <button type="button" style="all:unset;display:block;width:100%;cursor:pointer" class="qsheetOpen">
        <div class="qsliderHead">${bigValue()}<span class="twHint">tap to open<br>sliders ↗</span></div>
      </button>`);
    ctx.body.querySelector('.shotHero').after(card);

    const backdrop = el('div', 'mkBackdrop');
    const sheet = el('div', 'qsheet');
    sheet.innerHTML = `
      <div class="qgrabBar" aria-hidden="true"></div>
      <div class="qsheetTop"><p class="twLabel">${ICON_SCALE} Target weight</p><p class="twVal" style="font-size:2.2rem"><span class="jsNum">36.0</span><small>g</small></p></div>
      <p class="qslideLabel"><span>Coarse</span><small>1 g steps</small></p>
      <div class="jsCoarse"></div>
      <p class="qslideLabel"><span>Fine</span><small>0.1 g steps · ±2 g</small></p>
      <div class="jsFine"></div>
      <div class="qchips"></div>
      <div class="qsheetBtns"><button type="button" class="apply">Listo</button></div>`;
    ctx.mock.append(backdrop, sheet);

    const coarseRange = { min: 10, max: 80 }, fineRange = { min: 34, max: 38 };
    const toast = () => showToast(ctx, `Target set to ${fmt(ctx.state.w)} g · ${ctx.state.preset}`);
    const recenterFine = v => { fineRange.min = v - 2; fineRange.max = v + 2; };
    const coarse = makeSlider(ctx, { range: coarseRange, snap: 1, onEnd: v => { recenterFine(v); fine.set(v); toast(); } });
    const fine = makeSlider(ctx, { range: fineRange, snap: 0.1, onEnd: toast });
    $('.jsCoarse', sheet).append(coarse.node);
    $('.jsFine', sheet).append(fine.node);

    const chipsRow = $('.qchips', sheet);
    [18, 20, 25, 27, 36, 54].forEach(g => {
      const chip = el('button', 'qchip' + (g === 36 ? ' on' : ''), `${fmt(g)}<small>g</small>`);
      chip.type = 'button';
      chip.addEventListener('click', () => {
        $$('.qchip', chipsRow).forEach(c => c.classList.remove('on'));
        chip.classList.add('on');
        setW(ctx, g);
        coarse.set(g);
        recenterFine(g);
        fine.set(g);
      });
      chipsRow.append(chip);
    });
    const open = () => {
      coarse.set(ctx.state.w);
      recenterFine(ctx.state.w);
      fine.set(ctx.state.w);
      sheet.classList.add('open');
      backdrop.classList.add('show');
    };
    const close = () => { sheet.classList.remove('open'); backdrop.classList.remove('show'); };
    $('.qsheetOpen', card).addEventListener('click', open);
    backdrop.addEventListener('click', close);
    $('.apply', sheet).addEventListener('click', close);
  }

  // ---------- 06 · Arco alrededor de Start ----------
  function variant06(ctx, mock) {
    const dock = el('div', 'mockDock');
    dock.innerHTML = `
      <button type="button" class="qarcSide jsRinse"><svg viewBox="0 0 24 24"><path d="M7 3v3M12 3v3M17 3v3M5 9h14l1.5 11H3.5z"/><path d="M5 14h14"/></svg><span>Rinse</span></button>
      <button type="button" class="qarcBtn" aria-label="Start shot. Drag the ring to set the target weight.">
        <svg viewBox="0 0 100 100" aria-hidden="true">
          <path class="aTrack" fill="none" stroke="var(--ln)" stroke-width="4"/>
          <path class="aValue" fill="none" stroke="var(--ac)" stroke-width="6" stroke-linecap="round"/>
          <circle class="aThumb" r="5" fill="var(--sf)" stroke="var(--ac)" stroke-width="3"/>
        </svg>
        <span class="qarcCore"><b>▶</b><small>Start</small></span>
      </button>
      <button type="button" class="qarcSide jsPush"><svg viewBox="0 0 24 24"><path d="M12 3v18M5 10h14M7 7l-2 3 2 3M17 7l2 3-2 3"/></svg><span>Push</span></button>`;
    mock.append(dock);
    const read = el('button', 'qarcRead');
    read.type = 'button';
    read.innerHTML = '<span class="dot" aria-hidden="true"></span><span class="jsNum">36.0</span> g · drag the ring';
    mock.append(read);

    const ARC = { min: 10, max: 80, a0: -145, a1: 145, r: 45, c: 50 };
    const svg = $('svg', dock);
    const value = $('.aValue', dock), thumb = $('.aThumb', dock);
    $('.aTrack', dock).setAttribute('d', arcPath(ARC.c, ARC.c, ARC.r, ARC.a0, ARC.a1));
    const core = $('.qarcCore', dock), btn = $('.qarcBtn', dock);
    const arcAngle = w => ARC.a0 + (clampW(w) - ARC.min) / (ARC.max - ARC.min) * (ARC.a1 - ARC.a0);

    const paint = progress => {
      if (progress == null) {
        const a = arcAngle(ctx.state.w);
        value.setAttribute('d', arcPath(ARC.c, ARC.c, ARC.r, ARC.a0, a));
        const [x, y] = polar(ARC.c, ARC.c, ARC.r, a);
        thumb.setAttribute('cx', x); thumb.setAttribute('cy', y);
        renderTargets(ctx);
      } else {
        value.setAttribute('d', arcPath(ARC.c, ARC.c, ARC.r, ARC.a0, ARC.a0 + progress * (ARC.a1 - ARC.a0)));
        const [x, y] = polar(ARC.c, ARC.c, ARC.r, ARC.a0 + progress * (ARC.a1 - ARC.a0));
        thumb.setAttribute('cx', x); thumb.setAttribute('cy', y);
      }
    };
    paint();

    const ringDrag = (node, isBtn) => drag(node, {
      start: e => ({ x: e.clientX, y: e.clientY, moved: 0 }),
      move: (e, c) => {
        if (isBtn) {
          c.moved = Math.max(c.moved, Math.hypot(e.clientX - c.x, e.clientY - c.y));
          if (c.moved < 5) return;
        }
        const rect = svg.getBoundingClientRect();
        const dx = e.clientX - (rect.left + rect.width / 2), dy = e.clientY - (rect.top + rect.height / 2);
        let deg = Math.atan2(dx, -dy) * 180 / Math.PI;
        deg = Math.max(ARC.a0, Math.min(ARC.a1, deg));
        setW(ctx, ARC.min + (deg - ARC.a0) / (ARC.a1 - ARC.a0) * (ARC.max - ARC.min), { silent: true });
        paint();
      },
      end: (e, c) => {
        if (isBtn && c.moved < 5) { startShot(ctx); return; }
        showToast(ctx, `Target set to ${fmt(ctx.state.w)} g · ${ctx.state.preset}`);
      },
    });
    ringDrag(btn, true);
    ringDrag(read, false);

    $('.jsRinse', dock).addEventListener('click', () => startShot(ctx, 'rinse'));
    $('.jsPush', dock).addEventListener('click', () => showToast(ctx, 'Paddle pulsed (momentary)'));
    ctx.setRunning = running => {
      core.innerHTML = running ? '<b>■</b><small>Stop</small>' : '<b>▶</b><small>Start</small>';
    };
    ctx.onShotTick = progress => paint(progress);
    ctx.onShotEnd = () => paint();
  }

  // ---------- 07 · Slider de precisión ----------
  function variant07(ctx) {
    const card = targetCard(`
      <div class="qslider qsliderCard">
        <div class="qsliderHead">${bigValue()}<span class="twHint">drag · 0.5 g steps</span></div>
      </div>
      <div class="qticks"><span>10</span><span>45</span><span>80</span></div>
      <div class="qfine"><button type="button" data-d="-0.1">− 0.1</button><button type="button" data-d="0.1">+ 0.1</button></div>
      ${presetSub(' · fine tune below')}`);
    ctx.body.querySelector('.shotHero').after(card);
    ctx.presetNameEls.push($('.jsPresetName', card));
    const slider = makeSlider(ctx, {
      range: { min: 10, max: 80 },
      snap: 0.5,
      onEnd: () => showToast(ctx, `Target set to ${fmt(ctx.state.w)} g · ${ctx.state.preset}`),
    });
    $('.qsliderCard', card).after(slider.node);
    $$('.qfine button', card).forEach(btn => btn.addEventListener('click', () => {
      const v = Math.round((ctx.state.w + Number(btn.dataset.d)) * 10) / 10;
      setW(ctx, v);
      slider.set(v);
    }));
    ctx.sliders.push(() => slider.set(ctx.state.w));
  }

  // ---------- 08 · Carrusel de presets ----------
  function variant08(ctx) {
    const card = targetCard(`
      <div class="qcar" aria-label="Swipe to pick a preset"><div class="qcarTrack"></div></div>
      <div class="qcarDots" aria-hidden="true"></div>
      <p class="twHint" style="text-align:center;margin-top:.55rem">Swipe the cards · applies on release</p>`);
    ctx.body.querySelector('.shotHero').after(card);
    const car = $('.qcar', card), track = $('.qcarTrack', card), dots = $('.qcarDots', card);
    const cards = PRESETS.map((p, i) => {
      const c = el('div', 'qcarCard' + (i === 0 ? ' at' : ''), `<b><span>${fmt(p.g)}</span><small>g</small></b><span>${p.name} · ${p.badge}</span>`);
      track.append(c);
      dots.append(el('i', i === 0 ? 'at' : ''));
      return c;
    });
    let index = 0;
    const centerFor = i => car.clientWidth / 2 - (cards[i].offsetLeft + cards[i].offsetWidth / 2);
    const paint = snap => {
      track.classList.toggle('snap', !!snap);
      track.style.transform = `translateX(${centerFor(index)}px)`;
      cards.forEach((c, i) => {
        c.classList.toggle('at', i === index);
        c.querySelector('b span').textContent = fmt(PRESETS[i].g);
      });
      [...dots.children].forEach((d, i) => d.classList.toggle('at', i === index));
    };
    paint();
    const nearestIndex = x => {
      let best = 0, bestD = 1e9;
      cards.forEach((c, i) => {
        const d = Math.abs(c.offsetLeft + c.offsetWidth / 2 + x - car.clientWidth / 2);
        if (d < bestD) { bestD = d; best = i; }
      });
      return best;
    };
    drag(car, {
      start: e => { track.classList.remove('snap'); return { sx: e.clientX, x0: centerFor(index) }; },
      move: (e, c) => {
        let x = c.x0 + (e.clientX - c.sx);
        x = Math.min(centerFor(0) + 24, Math.max(centerFor(cards.length - 1) - 24, x));
        c.x = x;
        track.style.transform = `translateX(${x}px)`;
      },
      end: (e, c) => {
        index = c.x == null ? index : nearestIndex(c.x);
        paint(true);
        selectPreset(ctx, index);
      },
    });
    ctx.sliders.push(() => {
      const next = Math.max(0, PRESETS.findIndex(p => p.name === ctx.state.preset));
      if (next !== index) { index = next; paint(true); }
    });
  }

  // ---------- 09 · Menú radial ----------
  function variant09(ctx) {
    const card = targetCard(`
      <p class="twVal" style="text-align:center;font-size:3rem"><span class="jsNum">36.0</span><small>g</small></p>
      <p class="twHint" style="text-align:center">Hold the weight to open quick weights</p>`);
    ctx.body.querySelector('.shotHero').after(card);

    const backdrop = el('div', 'mkBackdrop');
    const wrap = el('div', 'qfanWrap');
    const origin = el('div', 'qfanOrigin');
    const ring = el('div', 'qpressRing');
    wrap.append(origin, ring);
    const items = FAN_WEIGHTS.map((g, i) => {
      const item = el('button', 'qfanItem', fmt(g));
      item.type = 'button';
      item.dataset.g = g;
      item.dataset.i = i;
      origin.after(item);
      return item;
    });
    const cancel = el('div', 'qfanCancel', '✕');
    origin.after(cancel);
    ctx.mock.append(backdrop, wrap);

    const place = (x, y) => {
      const rect = ctx.mock.getBoundingClientRect();
      const ox = Math.max(88, Math.min(rect.width - 88, x - rect.left));
      const oy = Math.max(120, Math.min(rect.height - 150, y - rect.top - 40));
      origin.style.left = ox + 'px';
      origin.style.top = oy + 'px';
      ring.style.left = ox + 'px';
      ring.style.top = oy + 'px';
      cancel.style.left = ox + 'px';
      cancel.style.top = oy + 'px';
      items.forEach((item, i) => {
        const a = (-160 + i * (140 / (items.length - 1))) * Math.PI / 180;
        item.style.left = ox + Math.sin(a) * 118 + 'px';
        item.style.top = oy + Math.cos(a) * -118 + 'px';
      });
    };
    const nearest = (x, y) => {
      let best = null, bestD = 44;
      items.forEach(item => {
        const r = item.getBoundingClientRect();
        const d = Math.hypot(x - (r.left + r.width / 2), y - (r.top + r.height / 2));
        if (d < bestD) { bestD = d; best = item; }
      });
      return best;
    };
    let holdTimer = 0;
    drag(card, {
      start: e => {
        holdTimer = setTimeout(() => {
          place(e.clientX, e.clientY);
          wrap.classList.add('show');
          backdrop.classList.add('show');
        }, 350);
        return { x: e.clientX, y: e.clientY };
      },
      move: (e, c) => {
        if (!wrap.classList.contains('show')) {
          if (Math.hypot(e.clientX - c.x, e.clientY - c.y) > 12) clearTimeout(holdTimer);
          return;
        }
        items.forEach(item => item.classList.remove('hot'));
        const hot = nearest(e.clientX, e.clientY);
        if (hot) hot.classList.add('hot');
      },
      end: e => {
        clearTimeout(holdTimer);
        if (!wrap.classList.contains('show')) return;
        const hot = nearest(e.clientX, e.clientY);
        wrap.classList.remove('show');
        backdrop.classList.remove('show');
        if (hot) setW(ctx, Number(hot.dataset.g));
      },
    });
  }

  // ---------- 10 · Cajón inferior ----------
  function variant10(ctx, mock) {
    const grab = el('button', 'qgrab');
    grab.type = 'button';
    grab.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><path d="m6 14 6-6 6 6"/></svg><span><span class="jsNum">36.0</span> g · target</span>';
    const drawer = el('div', 'qdrawer');
    drawer.innerHTML = `
      <div class="qgrabBar" aria-hidden="true"></div>
      <div class="qsliderHead"><p class="twLabel">${ICON_SCALE} Target weight</p><p class="twVal" style="font-size:2.2rem"><span class="jsNum">36.0</span><small>g</small></p></div>
      <div class="jsSlide"></div>
      <div class="qchips"></div>
      <p class="twHint">Slide to set · chips jump to favorites.</p>`;
    mock.append(grab, drawer);
    const slider = makeSlider(ctx, {
      range: { min: 10, max: 80 },
      snap: 0.5,
      onEnd: () => showToast(ctx, `Target set to ${fmt(ctx.state.w)} g · ${ctx.state.preset}`),
    });
    $('.jsSlide', drawer).append(slider.node);
    ctx.sliders.push(() => slider.set(ctx.state.w));
    const chipsRow = $('.qchips', drawer);
    CHIP_WEIGHTS.forEach(g => {
      const chip = el('button', 'qchip' + (g === 36 ? ' on' : ''), `${fmt(g)}<small>g</small>`);
      chip.type = 'button';
      chip.addEventListener('click', () => {
        $$('.qchip', chipsRow).forEach(c => c.classList.remove('on'));
        chip.classList.add('on');
        setW(ctx, g);
        slider.set(g);
      });
      chipsRow.append(chip);
    });

    let openState = false;
    const setOpen = open => {
      openState = open;
      drawer.classList.toggle('open', open);
      grab.querySelector('svg').style.transform = open ? 'rotate(180deg)' : '';
    };
    drag(grab, {
      start: e => ({ y: e.clientY, up: 0, down: 0 }),
      move: (e, c) => {
        c.up = Math.max(c.up, c.y - e.clientY);
        c.down = Math.max(c.down, e.clientY - c.y);
        if (c.up > 6) setOpen(true);
        if (c.down > 6) setOpen(false);
      },
      end: (e, c) => { if (c.up < 6 && c.down < 6) setOpen(!openState); },
    });
  }

  // ---------- Init ----------
  const VARIANTS = { '01': variant01, '02': variant02, '03': variant03, '04': variant04, '05': variant05, '06': variant06, '07': variant07, '08': variant08, '09': variant09, '10': variant10 };
  $$('.mock[data-chrome]').forEach(mock => {
    mock.insertAdjacentHTML('afterbegin', TOP);
    mock.insertAdjacentHTML('beforeend', NAV);
    const variant = mock.dataset.variant;
    const ctx = buildHome(mock);
    if (variant === '06') {
      variant06(ctx, mock);
    } else {
      const dock = standardDock();
      mock.append(dock);
      wireDock(ctx, mock);
      if (VARIANTS[variant]) VARIANTS[variant](ctx, mock);
    }
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
