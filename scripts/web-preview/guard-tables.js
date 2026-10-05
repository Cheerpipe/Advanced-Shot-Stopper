'use strict';

(() => {
  // Real preset data from the Home quick settings screenshots.
  const TIME = {prot: 12, tMin: 28, tMaxBbw: 44, wall: 50}; // BBW cut protection (s), min/max BBW brew time (s), machine operational wall (s)
  const PRESETS = [
    {name: 'Double', badge: 'Factory', target: 36, floor: 34, ceil: 42.5},
    {name: 'Single', badge: 'Factory', target: 18, floor: 16, ceil: 20},
  ];
  const g = v => (Number.isInteger(v) ? v : v.toFixed(1)) + ' g';
  const dot = c => `<i style="background:${c}" aria-hidden="true"></i>`;
  const FC = '#d97706', GC = 'var(--ok)', SC = '#5594dd';

  // The brief rules per preset (values from settings).
  const rules = p => ({
    fast: `Cuts at ${TIME.tMin} s (max ${g(p.ceil)})`,
    bbw: `Cuts at ${g(p.target)} between ${TIME.tMin} and ${TIME.tMaxBbw} s`,
    slow: `Cuts at ${g(p.floor)} from ${TIME.tMaxBbw} s (max ${TIME.wall} s)`,
  });

  // 00 · Reference: the table as implemented today in Home.
  function classicTable(p) {
    const r = rules(p);
    return `<table class="gtTable"><thead><tr><th>${dot(FC)}Fast</th><th>${dot(GC)}BBW</th><th>${dot(SC)}Slow</th></tr></thead>` +
      `<tbody><tr><td>${r.fast}</td><td>${r.bbw}</td><td>${r.slow}</td></tr></tbody></table>`;
  }

  // 01 · Current pick: preset accordion with the rules inside.
  function accordion() {
    return `<div class="gtAcc">${PRESETS.map((p, i) =>
      `<div class="gtPreset${i === 0 ? ' open' : ''}">` +
      `<button type="button" class="gtAccHead" aria-expanded="${i === 0}">` +
      `<span class="gtAccName">${p.name}</span><span class="gtAccBadge">${p.badge}</span>` +
      `<span class="gtAccTarget">Target ${g(p.target)}</span>` +
      `<span class="gtAccChev" aria-hidden="true">▾</span></button>` +
      `<div class="gtPanel"><div class="gtPanelIn"><div class="gtPanelPad">` +
      `<div class="gtRows">` +
      `<div class="gtRow"><span class="gtName">${dot(FC)}Fast</span><span class="gtRule">${rules(p).fast}</span></div>` +
      `<div class="gtRow"><span class="gtName">${dot(GC)}BBW</span><span class="gtRule">${rules(p).bbw}</span></div>` +
      `<div class="gtRow"><span class="gtName">${dot(SC)}Slow</span><span class="gtRule">${rules(p).slow}</span></div>` +
      `</div></div></div></div></div>`).join('')}</div>`;
  }

  // FW-style thick preset row with a guard color strip; opens the rules panel.
  function fwRow(p, i, color, variant) {
    const open = i === 0;
    const cls = {bar: 'fwBar', fill: 'fwFill', duo: 'fwDuo'}[variant];
    return `<div class="fwPreset ${cls}${open ? ' open' : ''}" style="--pc:${color}" data-fw="${i}">` +
      `<button type="button" class="fwHead" aria-expanded="${open}">` +
      (variant === 'fill' ? `<span class="fwDot" aria-hidden="true"></span>` : '') +
      `<span class="fwName">${p.name}</span><span class="fwBadge">${p.badge}</span>` +
      `<span class="fwTarget">Target ${g(p.target)}</span>` +
      `<span class="fwChev" aria-hidden="true">▾</span></button>` +
      `<div class="fwPanel"><div class="fwPanelIn">` +
      `<div class="gtRows">` +
      `<div class="gtRow"><span class="gtName">${dot(FC)}Fast</span><span class="gtRule">${rules(p).fast}</span></div>` +
      `<div class="gtRow"><span class="gtName">${dot(GC)}BBW</span><span class="gtRule">${rules(p).bbw}</span></div>` +
      `<div class="gtRow"><span class="gtName">${dot(SC)}Slow</span><span class="gtRule">${rules(p).slow}</span></div>` +
      `</div></div></div></div>`;
  }

  // 02/03/04 · Hybrids: FW thick colorful rows as accordion + rules below.
  const hybrid = variant => p => PRESETS.map((p2, i) => fwRow(p2, i, [FC, GC, SC][i % 3], variant)).join('');
  const HYBRIDS = [
    {key: 'bar', name: '02 · FW + barra de color',
      desc: 'Filas gruesas estilo firmware con una barra de color de guardia en el borde izquierdo; al tocar un preset se abre su bloque de reglas debajo y el anterior se cierra.'},
    {key: 'fill', name: '03 · FW + punto de color',
      desc: 'Igual estructura pero con un punto sólido de color junto al nombre; el bloque de reglas se abre con la misma transición animada.'},
    {key: 'duo', name: '04 · FW + fondo suave',
      desc: 'La fila abierta toma un fondo suave del color de guardia además de la barra; más contraste para el preset seleccionado, mismo acordeón de reglas debajo.'},
  ];

  const frameHtml = phone => `
    <div class="frame${phone ? ' phone' : ''}"><p class="frameLabel">${phone ? 'Teléfono · 360 px' : 'Escritorio'}</p>
    <fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>
    <div class="gtZone"></div></fieldset></div>`;

  const host = document.getElementById('options');

  const zoneState = new Map(); // zone element -> {preset, gap}
  const zoneRender = new Map(); // zone element -> (preset) => html
  function addSection(title, desc, inner, star = false) {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${title}${star ? ' <span class="star">Recomendada</span>' : ''}</h2><p>${desc}</p></div>` +
      frameHtml(false) + frameHtml(true);
    host.append(section);
    section.querySelectorAll('.gtZone').forEach(zone => {
      zone.innerHTML = inner(PRESETS[0]);
      zoneState.set(zone, {preset: 0});
      zoneRender.set(zone, inner);
    });
  }

  // Radio variant: preset accordion with radio circles and bigger names.
  function radioAccordion() {
    return `<div class="gtAcc gtRadio">${PRESETS.map((p, i) => {
      const r = rules(p);
      return `<div class="gtPreset${i === 0 ? ' open' : ''}">` +
        `<button type="button" class="gtAccHead" aria-expanded="${i === 0}">` +
        `<span class="gtRadioDot" aria-hidden="true"></span>` +
        `<span class="gtAccName">${p.name}</span><span class="gtAccBadge">${p.badge}</span>` +
        `<span class="gtAccTarget">Target ${g(p.target)}</span>` +
        `<span class="gtAccChev" aria-hidden="true">▾</span></button>` +
        `<div class="gtPanel"><div class="gtPanelIn"><div class="gtPanelPad">` +
        `<div class="gtRows">` +
        `<div class="gtRow"><span class="gtName">${dot(FC)}Fast</span><span class="gtRule">${r.fast}</span></div>` +
        `<div class="gtRow"><span class="gtName">${dot(GC)}BBW</span><span class="gtRule">${r.bbw}</span></div>` +
        `<div class="gtRow"><span class="gtName">${dot(SC)}Slow</span><span class="gtRule">${r.slow}</span></div>` +
        `</div></div></div></div></div>`;
    }).join('')}</div>`;
  }

  // 00 · reference
  addSection('00 · Tabla actual (referencia)',
    'La tabla tal como está implementada hoy en Home: títulos con punto de color y una fila de reglas.',
    () => classicTable(PRESETS[0]));
  // 01 · current accordion
  addSection('01 · Acordeón por preset (actual)',
    'La propuesta seleccionada: cada preset abre su bloque de reglas con transición animada y el anterior se cierra.',
    () => accordion());
  // 02 · radio accordion (fusion of 01 with radio circles)
  addSection('02 · Radio + filas guardia → regla',
    'Acordeón por preset con círculos tipo radio en cada fila: el círculo relleno marca el preset activo y al tocarlo se abre debajo su bloque de reglas con transición animada. El nombre del preset usa el tamaño de Settings.',
    () => radioAccordion());
  // 03–05 · hybrids
  HYBRIDS.forEach(h => addSection(h.name, h.desc, () => hybrid(h.key)(PRESETS[0])));

  // Accordion + FW accordion behavior (one open per group, animated by CSS).
  host.addEventListener('click', event => {
    const card = event.target.closest('.presetCard[data-preset]');
    if (card) {
      document.querySelectorAll('.frame .presetCard').forEach(c => {
        const on = c.dataset.preset === card.dataset.preset;
        c.classList.toggle('active', on);
        c.classList.toggle('selected', on);
      });
      const preset = PRESETS[+card.dataset.preset];
      for (const zone of zoneState.keys()) zone.innerHTML = zoneRender.get(zone)(preset);
      return;
    }
    const head = event.target.closest('.gtAccHead, .fwHead');
    if (!head) return;
    const preset = head.closest('.gtPreset, .fwPreset');
    if (preset.classList.contains('open')) return;
    preset.parentElement.querySelectorAll('.open').forEach(o => {
      o.classList.remove('open');
      const h = o.querySelector('.gtAccHead, .fwHead');
      if (h) h.setAttribute('aria-expanded', 'false');
    });
    preset.classList.add('open');
    head.setAttribute('aria-expanded', 'true');
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
