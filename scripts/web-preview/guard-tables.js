'use strict';

(() => {
  // Real preset data from the Home quick settings screenshots.
  const TIME = {prot: 12, tMin: 28, tMax: 44, wall: 50};
  const PRESETS = [
    {name: 'Double', badge: 'Factory', target: 36, floor: 34, ceil: 42.5},
    {name: 'Single', badge: 'Factory', target: 18, floor: 16, ceil: 20},
  ];
  const g = v => (Number.isInteger(v) ? v : v.toFixed(1)) + ' g';
  const FC = '#d97706', GC = 'var(--ok)', SC = '#5594dd';
  const dot = c => `<i style="background:${c}" aria-hidden="true"></i>`;

  // The rule sentences, identical across proposals (values from settings).
  const rules = p => ({
    fast: `Cuts at ${TIME.tMin} s (max ${g(p.ceil)})`,
    bbw: `Cuts at ${g(p.target)}, ${TIME.tMin}–${TIME.tMax} s`,
    slow: `Cuts at ${g(p.floor)} from ${TIME.tMax} s (max ${TIME.wall} s)`,
  });

  // Preset accordion (03/04): every preset lists its own rules; the
  // selected one opens its panel below with an animated transition.
  function accordion(kind) {
    const info = p => kind === 'rows'
      ? `<div class="gtRows gtCompact">` +
        `<div class="gtRow"><span class="gtName">${dot(FC)}Fast</span><span class="gtRule">Cuts at ${TIME.tMin} s (max ${g(p.ceil)})</span></div>` +
        `<div class="gtRow"><span class="gtName">${dot(GC)}BBW</span><span class="gtRule">Cuts at ${g(p.target)}, ${TIME.tMin}–${TIME.tMax} s</span></div>` +
        `<div class="gtRow"><span class="gtName">${dot(SC)}Slow</span><span class="gtRule">Cuts at ${g(p.floor)} from ${TIME.tMax} s (max ${TIME.wall} s)</span></div></div>`
      : `<div class="gtWins gtCompact">` +
        `<div class="gtWin wn"><span class="gtWinT">Fast guard</span><span class="gtWinR">Cuts at ${TIME.tMin} s once the weight is between ${g(p.target)} and ${g(p.ceil)}</span></div>` +
        `<div class="gtWin ok"><span class="gtWinT">Brew by Weight</span><span class="gtWinR">Cuts as soon as the weight reaches ${g(p.target)}</span></div>` +
        `<div class="gtWin dn"><span class="gtWinT">Slow guard</span><span class="gtWinR">Cuts at ${g(p.floor)} if reached; otherwise pours down to it</span></div></div>`;
    return `<div class="gtAcc">` + PRESETS.map((p, i) =>
      `<div class="gtPreset${i === 0 ? ' open' : ''}">` +
      `<button type="button" class="gtAccHead" aria-expanded="${i === 0}">` +
      `<span class="gtAccName">${p.name}</span><span class="gtAccBadge">${p.badge}</span>` +
      `<span class="gtAccTarget">Target ${g(p.target)}</span>` +
      `<span class="gtAccChev" aria-hidden="true">▾</span></button>` +
      `<div class="gtPanel"><div class="gtPanelIn"><div class="gtPanelPad">${info(p)}</div></div></div></div>`).join('') + `</div>`;
  }

  const frameHtml = (phone, noCards) => `
    <div class="frame${phone ? ' phone' : ''}"><p class="frameLabel">${phone ? 'Teléfono · 360 px' : 'Escritorio'}</p>
    <fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>
    ${noCards ? '' : `<div class="presetCards">${PRESETS.map((p, i) =>
      `<div class="presetCard${i === 0 ? ' active selected' : ''}" data-preset="${i}"><span class="presetCardDot" aria-hidden="true"></span><div class="presetCardTitleRow"><div class="presetCardTitle">${p.name}</div><span class="presetCardBadge">${p.badge}</span></div><div class="presetCardMeta">Target ${g(p.target)}</div></div>`).join('')}</div>`}
    <div class="gtZone"></div></fieldset></div>`;

  const host = document.getElementById('options');

  // 00 · Reference: the table as implemented today.
  function classic(p) {
    const r = rules(p);
    return `<table class="gtTable"><thead><tr><th>${dot(FC)}Fast</th><th>${dot(GC)}BBW</th><th>${dot(SC)}Slow</th></tr></thead>` +
      `<tbody><tr><td>${r.fast}</td><td>${r.bbw}</td><td>${r.slow}</td></tr></tbody></table>`;
  }

  const PROPOSALS = [
    {name: '01 · Tabla clásica refinada', star: true,
      desc: 'La tabla actual con dos mejoras de lectura: puntos de color en los títulos para asociar cada guardia con su color y filete superior en la fila de reglas.',
      render: classic},
    {name: '02 · Tarjetas por guardia', star: true,
      desc: 'Cada guardia como tarjeta independiente con su color en el título. En escritorio las tres quedan lado a lado; en teléfono se apilan conservando el mismo contenido.',
      render: p => { const r = rules(p);
        return `<div class="gtCards">` +
          `<div class="gtCard"><header>${dot(FC)}Fast</header><p>${r.fast}</p></div>` +
          `<div class="gtCard"><header>${dot(GC)}BBW</header><p>${r.bbw}</p></div>` +
          `<div class="gtCard"><header>${dot(SC)}Slow</header><p>${r.slow}</p></div></div>`; } },
    {name: '03 · Filas guardia → regla', self: true,
      desc: 'Acordeón por preset: al seleccionar uno se abre debajo su bloque de reglas (guardia a la izquierda, regla a la derecha) con transición animada; el anterior se cierra. La tarjeta abierta queda resaltada en el color de acento y los elementos, compactos.',
      render: () => accordion('rows')},
    {name: '04 · Ventanas de tiempo', star: true, self: true,
      desc: 'Las reglas por ventana de tiempo —antes de 28 s, la ventana de llenado y después de 44 s— dentro del acordeón por preset: seleccionas uno y se abre debajo su bloque con transición animada; tarjeta abierta resaltada en acento y elementos compactos.',
      render: () => accordion('wins')},
    {name: '05 · Valores destacados',
      desc: 'Mínimas palabras: cada guardia con los valores que importan como pastillas — el tiempo y el peso que definen su corte. La fila de pastillas se envuelve sola en teléfono.',
      render: p => `
        <div class="gtChipRows">
          <div class="gtChipRow"><span class="gtName">${dot(FC)}Fast</span><span class="chip">${TIME.tMin} s</span><span class="chip">max ${g(p.ceil)}</span></div>
          <div class="gtChipRow"><span class="gtName">${dot(GC)}BBW</span><span class="chip">${g(p.target)}</span><span class="chip">${TIME.tMin}–${TIME.tMax} s</span></div>
          <div class="gtChipRow"><span class="gtName">${dot(SC)}Slow</span><span class="chip">${g(p.floor)}</span><span class="chip">${TIME.tMax}–${TIME.wall} s</span></div>
        </div>`},
  ];

  const zoneState = new Map(); // zone element -> {render, preset}
  const classicSection = {name: '00 · Tabla actual (referencia)',
    desc: 'La tabla tal como está implementada hoy en Home: títulos y una fila de reglas, sin puntos de color ni jerarquía adicional.', render: classic};
  [classicSection, ...PROPOSALS].forEach(s => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${s.name}${s.star ? ' <span class="star">Recomendada</span>' : ''}</h2><p>${s.desc}</p></div>${frameHtml(false, s.self)}${frameHtml(true, s.self)}`;
    host.append(section);
    section.querySelectorAll('.gtZone').forEach(zone => zoneState.set(zone, {render: s.render, preset: 0}));
  });

  function paintZone(zone) {
    const st = zoneState.get(zone);
    zone.innerHTML = st.render(PRESETS[st.preset]);
  }
  function paintAll() { for (const zone of zoneState.keys()) paintZone(zone); }
  paintAll();

  host.addEventListener('click', event => {
    const head = event.target.closest('.gtAccHead');
    if (head) {
      const preset = head.closest('.gtPreset');
      if (!preset.classList.contains('open')) {
        preset.parentElement.querySelectorAll('.gtPreset.open').forEach(o => {
          o.classList.remove('open');
          o.querySelector('.gtAccHead').setAttribute('aria-expanded', 'false');
        });
        preset.classList.add('open');
        head.setAttribute('aria-expanded', 'true');
      }
      return;
    }
    const card = event.target.closest('.presetCard[data-preset]');
    if (!card) return;
    const frameEl = card.closest('.frame');
    frameEl.querySelectorAll('.presetCard').forEach(c => {
      const on = c === card;
      c.classList.toggle('active', on);
      c.classList.toggle('selected', on);
    });
    const zone = frameEl.querySelector('.gtZone');
    zoneState.get(zone).preset = +card.dataset.preset;
    paintZone(zone);
  });

  const toggle = document.getElementById('themeToggle');
  const themes = [{cls: '', label: 'Tema: auto'}, {cls: 'theme-light', label: 'Tema: claro'}, {cls: 'theme-dark', label: 'Tema: oscuro'}];
  let index = 0;
  toggle.addEventListener('click', () => {
    index = (index + 1) % themes.length;
    document.documentElement.classList.remove('theme-light', 'theme-dark');
    if (themes[index].cls) document.documentElement.classList.add(themes[index].cls);
    document.documentElement.style.colorScheme = themes[index].cls ? themes[index].cls.slice(6) : '';
    toggle.textContent = themes[index].label;
  });
})();
