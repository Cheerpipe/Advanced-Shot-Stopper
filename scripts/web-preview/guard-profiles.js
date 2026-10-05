'use strict';

(() => {
  // Real preset data from the Home quick settings screenshots.
  const TIME = {prot: 12, tMin: 28, tMaxBbw: 44, wall: 50}; // BBW cut protection (s), min/max BBW brew time (s), machine operational wall (s)
  const PRESETS = [
    {name: 'Double', badge: 'Factory', target: 36, floor: 34, ceil: 42.5},
    {name: 'Single', badge: 'Factory', target: 18, floor: 16, ceil: 20},
  ];
  const g = v => (Number.isInteger(v) ? v : v.toFixed(1)) + ' g';
  const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

  // Chart canvas: 260 x 78 units, scaled to the panel width.
  const W = 260, L = 0, R = 2, T = 8, B = 76, VBH = 78;
  const MIN_GAP_PX = 17;  // label line-box height + air: minimum line separation on screen
  const GUTTER_PX = 22.4; // 1.4rem right gutter holding the weight labels

  // The final chart: one straight line per cut situation, stats-style HTML
  // labels. `f` maps the normalized weight (0..1) to the vertical fraction,
  // enabling alternative smooth scales. `gap` is the minimum separation
  // between drawn limit lines, in canvas units: lines that end up closer
  // than a label height are quantized downward until they clear it.
  function finalProfile(p, f = (u => u), gap = 6.5) {
    const FC = '#d97706', SC = '#5594dd'; // rule colors: orange fast, blue slow
    const VE = ' vector-effect="non-scaling-stroke"';
    const x = t => L + (W - L - R) * t / TIME.wall;
    const y = v => B - (B - T) * f(v / p.ceil);
    const q = [y(p.ceil), y(p.target), y(p.floor)].sort((a, b) => a - b);
    for (let i = 1; i < q.length; i++) if (q[i] - q[i - 1] < gap) q[i] = q[i - 1] + gap;
    const Y = v => v === p.ceil ? q[0] : v === p.target ? q[1] : v === p.floor ? q[2] : y(v);
    const path = pts => pts.map((pt, i) => (i ? 'L' : 'M') + x(pt[0]).toFixed(1) + ' ' + Y(pt[1]).toFixed(1)).join(' ');
    const line = (pts, color, dash) => `<path d="${path(pts)}" fill="none" stroke="${color}" stroke-width="1.35"${VE} stroke-linecap="round" stroke-linejoin="round"${dash ? ` stroke-dasharray="${dash}"` : ''}/>`;
    const xMark = (t, v, color) => { const cx = x(t).toFixed(1), cy = Y(v).toFixed(1);
      return `<path d="M${cx - 1.2} ${cy - 1.2}L${+cx + 1.2} ${+cy + 1.2}M${cx - 1.2} ${+cy + 1.2}L${+cx + 1.2} ${cy - 1.2}" stroke="${color}" stroke-width="1"${VE} stroke-linecap="round"/>`; };
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${T}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${B - T}" fill="${color}" fill-opacity="${op}"/>`;
    const top = u => (u / VBH * 100).toFixed(2) + '%';
    return `<figure class="gpFig"><div class="gpWrap">` +
      `<svg viewBox="0 0 ${W} ${VBH}" role="img" aria-label="Guard limits for ${esc(p.name)}: a fast shot reaches the ceiling by second ${TIME.prot} (the earliest the firmware can cut) and the fast guard cuts at ${TIME.tMin} seconds; a normal shot cuts at ${esc(g(p.target))} anywhere between ${TIME.tMin} and ${TIME.tMaxBbw} seconds; at ${TIME.tMaxBbw} seconds a shot already between ${esc(g(p.floor))} and ${esc(g(p.target))} is cut by the slow guard, and one below ${esc(g(p.floor))} is poured down to it by ${TIME.wall} seconds; machine limit ${TIME.wall} seconds">` +
      band(0, TIME.tMin, FC, .22) + band(TIME.tMin, TIME.tMaxBbw, 'var(--ok)', .22) + band(TIME.tMaxBbw, TIME.wall, SC, .22) +
      [p.ceil, p.target, p.floor].map(v => `<path d="M${L} ${Y(v).toFixed(1)}H${W - R}" stroke="var(--mu)" stroke-width=".8"${VE} stroke-dasharray="1.6 2.6" opacity=".7"/>`).join('') +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${T}V${B}M${x(TIME.tMaxBbw).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width=".8"${VE} stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      line([[0, 0], [TIME.prot, p.ceil]], FC, '3 2.6') +
      line([[TIME.prot, p.ceil], [TIME.tMin, p.ceil]], FC) +
      line([[0, 0], [TIME.tMin, p.target]], 'var(--ok)', '3 2.6') +
      line([[TIME.tMin, p.target], [TIME.tMaxBbw, p.target]], 'var(--ok)') +
      line([[0, 0], [TIME.tMaxBbw, p.floor]], SC, '3 2.6') +
      line([[TIME.tMaxBbw, p.floor], [TIME.wall, p.floor]], SC) +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${Y(p.ceil).toFixed(1)}V${Y(p.target).toFixed(1)}" stroke="${FC}" stroke-width="1.35"${VE} stroke-linecap="round"/>` +
      `<path d="M${x(TIME.tMaxBbw).toFixed(1)} ${Y(p.target).toFixed(1)}V${Y(p.floor).toFixed(1)}" stroke="${SC}" stroke-width="1.35"${VE} stroke-linecap="round"/>` +
      xMark(TIME.prot, p.ceil, FC) + xMark(TIME.tMin, p.ceil, FC) +
      xMark(TIME.tMin, p.target, 'var(--ok)') + xMark(TIME.tMaxBbw, p.target, 'var(--ok)') +
      xMark(TIME.tMaxBbw, p.floor, SC) + xMark(TIME.wall, p.floor, SC) +
      `</svg>` +
      `<span class="shotYTick" style="top:${top(q[0])}">${esc(g(p.ceil))}</span>` +
      `<span class="shotYTick" style="top:${top(q[1])}">${esc(g(p.target))}</span>` +
      `<span class="shotYTick" style="top:${top(q[2])}">${esc(g(p.floor))}</span>` +
      `</div>` +
      `<div class="ruleChartTicks" style="margin:.15rem 1.4rem 0 0"><span class="ruleTick" style="left:0">0 s</span><span class="ruleTick" style="left:56%">28 s</span><span class="ruleTick" style="left:88%">44 s</span><span class="ruleTick" style="left:100%">50 s</span></div>` +
      `</figure>`;
  }

  const frameHtml = phone =>
    `<div class="frame${phone ? ' phone' : ''}"><p class="frameLabel">${phone ? 'Teléfono · 360 px' : 'Escritorio'}</p>` +
    `<fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>` +
    `<div class="presetCards">${PRESETS.map((p, i) =>
      `<div class="presetCard${i === 0 ? ' active selected' : ''}" data-preset="${i}"><div class="presetCardTitleRow"><div class="presetCardTitle">${p.name}</div><span class="presetCardBadge">${p.badge}</span></div><div class="presetCardMeta">Target ${g(p.target)}</div></div>`).join('')}</div>` +
    `<div class="gpZone"></div></fieldset></div>`;

  const host = document.getElementById('options');
  const sections = [
    {name: '01 · Versión final · escala lineal', desc: 'Los límites de los guardias dibujados solo con datos reales, en los colores de los gráficos de Stats: la rápida (naranjo) sube hasta el techo de 42.5 g en el segundo 12 (protección BBW, lo antes que el firmware puede cortar), espera plano hasta la compuerta de 28 s y corta (X naranja); la BBW normal (verde) llega en diagonal a 36 g y sigue plana entre 28 y 44 s, donde puede ocurrir el corte (X verde); a los 44 s el segmento vertical celeste corta a todo tiro que ya esté entre 34 y 36 g, y la lenta sigue en plano a 34 g hasta el límite de 50 s (X celeste). Los rótulos de peso viajan sobre su propia línea: si dos líneas quedan a menos de la altura de un rótulo, la inferior se cuantiza hacia abajo hasta separarse.', render: (p, gap) => finalProfile(p, u => u, gap)},
    {name: '02 · Escala potencia (k = 2)', desc: 'La altura usa (peso/techo)²: la zona de guardias gana espacio y la parte baja se comprime de forma progresiva. La cuantización garantiza que el piso y el objetivo nunca se peguen.', render: (p, gap) => finalProfile(p, u => u * u, gap)},
    {name: '03 · Escala exponencial suavizada (k = 2)', desc: 'Altura = (e^(2u)−1)/(e²−1): expansión progresiva parecida a la potencia pero con el crecimiento más contenido en la parte baja. La cuantización de líneas se aplica igual.', render: (p, gap) => finalProfile(p, u => (Math.exp(2 * u) - 1) / (Math.exp(2) - 1), gap)},
    {name: '04 · Mezcla lineal-cuadrática (m = 0.7)', desc: 'Altura = 30% lineal + 70% cuadrática: la más suave de las tres —conserva parte de la linealidad original—. Con m = 0 devuelve exactamente la escala lineal. La cuantización de líneas se aplica igual.', render: (p, gap) => finalProfile(p, u => .3 * u + .7 * u * u, gap)},
  ];

  const zoneState = new Map(); // zone element -> {render, preset, wrapW}
  sections.forEach(s => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${s.name}</h2><p>${s.desc}</p></div>${frameHtml(false)}${frameHtml(true)}`;
    host.append(section);
    section.querySelectorAll('.gpZone').forEach(zone => zoneState.set(zone, {render: s.render, preset: 0}));
  });

  // Paint a zone with the minimum line gap derived from its real width:
  // the labels are fixed-size HTML, so the gap in canvas units grows as the
  // chart gets narrower.
  function paintZone(zone) {
    const st = zoneState.get(zone);
    const wrap = zone.querySelector('.gpWrap');
    const wrapW = wrap ? wrap.clientWidth : zone.closest('.frame').clientWidth - 32;
    const svgW = Math.max(120, wrapW - GUTTER_PX);
    const gap = MIN_GAP_PX * W / svgW;
    zone.innerHTML = st.render(PRESETS[st.preset], gap);
    st.wrapW = wrapW;
  }

  function paintAll() { for (const zone of zoneState.keys()) paintZone(zone); }
  paintAll();
  paintAll(); // second pass measures the real wrap widths and refines the gaps

  window.addEventListener('resize', () => {
    for (const [zone, st] of zoneState) {
      const wrap = zone.querySelector('.gpWrap');
      if (wrap && Math.abs(wrap.clientWidth - st.wrapW) > 1) paintZone(zone);
    }
  });

  host.addEventListener('click', event => {
    const card = event.target.closest('.presetCard[data-preset]');
    if (!card) return;
    const frameEl = card.closest('.frame');
    frameEl.querySelectorAll('.presetCard').forEach(c => {
      const on = c === card;
      c.classList.toggle('active', on);
      c.classList.toggle('selected', on);
    });
    const zone = frameEl.querySelector('.gpZone');
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
