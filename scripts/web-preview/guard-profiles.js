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
  const n1 = v => +v.toFixed(1);

  // The final chart: one straight line per cut situation, stats-style HTML labels.
  // `f` maps the normalized weight (0..1) to the vertical fraction, enabling
  // alternative smooth scales; `spreadMin` sets the label de-overlap gap.
  function finalProfile(p, f = (u => u), spreadMin = 14) {
    const W = 260, L = 0, R = 2, T = 8, B = 76;
    const FC = '#d97706', SC = '#5594dd'; // rule colors: orange fast, blue slow
    const VE = ' vector-effect="non-scaling-stroke"';
    const x = t => L + (W - R - L) * t / TIME.wall;
    const y = v => B - (B - T) * f(v / p.ceil);
    const path = pts => pts.map((q, i) => (i ? 'L' : 'M') + x(q[0]).toFixed(1) + ' ' + y(q[1]).toFixed(1)).join(' ');
    const line = (pts, color, dash) => `<path d="${path(pts)}" fill="none" stroke="${color}" stroke-width="1.35"${VE} stroke-linecap="round" stroke-linejoin="round"${dash ? ` stroke-dasharray="${dash}"` : ''}/>`;
    const xMark = (t, v, color, cyOverride) => { const cx = x(t), cy = cyOverride ?? y(v);
      return `<path d="M${(cx - 1.2).toFixed(1)} ${(cy - 1.2).toFixed(1)}L${(cx + 1.2).toFixed(1)} ${(cy + 1.2).toFixed(1)}M${(cx - 1.2).toFixed(1)} ${(cy + 1.2).toFixed(1)}L${(cx + 1.2).toFixed(1)} ${(cy - 1.2).toFixed(1)}" stroke="${color}" stroke-width="1"${VE} stroke-linecap="round"/>`; };
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${T}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${B - T}" fill="${color}" fill-opacity="${op}"/>`;
    const spread = (items, min) => { const list = [...items].sort((a, b) => a - b);
      for (let i = 1; i < list.length; i++) if (list[i] - list[i - 1] < min) list[i] = list[i - 1] + min;
      return list; };
    const [cyy, tyy, fyy] = spread([y(p.ceil), y(p.target), y(p.floor)], spreadMin);
    const top = u => (u / 78 * 100).toFixed(2) + '%';
    return `<figure class="gpFig"><div class="gpWrap">` +
      `<svg viewBox="0 0 ${W} 78" role="img" aria-label="Guard limits for ${esc(p.name)}: a fast shot reaches the ceiling by second ${TIME.prot} (the earliest the firmware can cut) and the fast guard cuts at ${TIME.tMin} seconds; a normal shot cuts at ${esc(g(p.target))} anywhere between ${TIME.tMin} and ${TIME.tMaxBbw} seconds; at ${TIME.tMaxBbw} seconds a shot already between ${esc(g(p.floor))} and ${esc(g(p.target))} is cut by the slow guard, and one below ${esc(g(p.floor))} is poured down to it by ${TIME.wall} seconds; machine limit ${TIME.wall} seconds">` +
      band(0, TIME.tMin, FC, .22) + band(TIME.tMin, TIME.tMaxBbw, 'var(--ok)', .22) + band(TIME.tMaxBbw, TIME.wall, SC, .22) +
      [p.ceil, p.target, p.floor].map(v => `<path d="M${L} ${y(v).toFixed(1)}H${W - R}" stroke="var(--mu)" stroke-width=".8"${VE} stroke-dasharray="1.6 2.6" opacity=".7"/>`).join('') +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${T}V${B}M${x(TIME.tMaxBbw).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width=".8"${VE} stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      line([[0, 0], [TIME.prot, p.ceil]], FC, '3 2.6') +
      line([[TIME.prot, p.ceil], [TIME.tMin, p.ceil]], FC) +
      line([[0, 0], [TIME.tMin, p.target]], 'var(--ok)', '3 2.6') +
      line([[TIME.tMin, p.target], [TIME.tMaxBbw, p.target]], 'var(--ok)') +
      line([[0, 0], [TIME.tMaxBbw, p.floor]], SC, '3 2.6') +
      line([[TIME.tMaxBbw, p.floor], [TIME.wall, p.floor]], SC) +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${y(p.ceil).toFixed(1)}V${y(p.target).toFixed(1)}" stroke="${FC}" stroke-width="1.35"${VE} stroke-linecap="round"/>` +
      `<path d="M${x(TIME.tMaxBbw).toFixed(1)} ${y(p.target).toFixed(1)}V${y(p.floor).toFixed(1)}" stroke="${SC}" stroke-width="1.35"${VE} stroke-linecap="round"/>` +
      xMark(TIME.prot, p.ceil, FC) + xMark(TIME.tMin, p.ceil, FC) + xMark(TIME.tMin, p.target, 'var(--ok)') +
      xMark(TIME.tMaxBbw, p.target, 'var(--ok)') + xMark(TIME.wall, p.floor, SC) +
      `</svg>` +
      `<span class="shotYTick" style="top:${top(cyy)}">${esc(g(p.ceil))}</span>` +
      `<span class="shotYTick" style="top:${top(tyy)}">${esc(g(p.target))}</span>` +
      `<span class="shotYTick" style="top:${top(fyy)}">${esc(g(p.floor))}</span>` +
      `</div>` +
      `<div class="ruleChartTicks" style="margin:.15rem 1.4rem 0 0"><span class="ruleTick" style="left:0">0 s</span><span class="ruleTick" style="left:56%">28 s</span><span class="ruleTick" style="left:88%">44 s</span><span class="ruleTick" style="left:100%">50 s</span></div>` +
      `</figure>`;

  }

  const frameHtml = (phone, render) =>
    `<div class="frame${phone ? ' phone' : ''}"><p class="frameLabel">${phone ? 'Teléfono · 360 px' : 'Escritorio'}</p>` +
    `<fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>` +
    `<div class="presetCards">${PRESETS.map((p, i) =>
      `<div class="presetCard${i === 0 ? ' active selected' : ''}" data-preset="${i}"><div class="presetCardTitleRow"><div class="presetCardTitle">${p.name}</div><span class="presetCardBadge">${p.badge}</span></div><div class="presetCardMeta">Target ${g(p.target)}</div></div>`).join('')}</div>` +
    `<div class="gpZone">${render(PRESETS[0])}</div></fieldset></div>`;

  const host = document.getElementById('options');
  const sections = [
    {name: '01 · Versión final · escala lineal (referencia)', desc: 'Los límites de los guardias dibujados solo con datos reales, en los colores de los gráficos de Stats: la rápida (naranjo) sube hasta su corte de 28 s × 42.5 g —su rango posible, entre 36 y 42.5 g, es el abanico sobre la compuerta—; la BBW normal (verde) llega en diagonal a 36 g y sigue plana entre 28 y 44 s, donde puede ocurrir el corte; a los 44 s el segmento vertical celeste corta a todo tiro que ya esté entre 34 y 36 g, mientras la lenta sube hasta el piso de 34 g para cortar en plano de 44 a 50 s. Cada línea va punteada hasta su activación y sólida desde ahí. En escala lineal, el piso (34 g) y el objetivo (36 g) quedan muy cerca.', render: p => finalProfile(p)},
    {name: '02 · Escala potencia (k = 2)', desc: 'La altura usa (peso/techo)²: la mitad superior del eje gana espacio y el piso y el objetivo se separan ~1.65×. Monótona y suave; la parte baja se comprime de forma progresiva y los propios rótulos de peso revelan la escala.', render: p => finalProfile(p, u => u * u, 5)},
    {name: '03 · Escala exponencial suavizada (k = 2)', desc: 'Altura = (e^(2u)−1)/(e²−1): expansión progresiva parecida a la potencia pero con el crecimiento más contenido en la parte baja; el piso y el objetivo se separan ~1.6×.', render: p => finalProfile(p, u => (Math.exp(2 * u) - 1) / (Math.exp(2) - 1), 5)},
    {name: '04 · Mezcla lineal-cuadrática (m = 0.7)', desc: 'Altura = 30% lineal + 70% cuadrática: la más suave de las tres —conserva parte de la linealidad original, así la parte baja no se aplasta del todo— y separa el piso y el objetivo ~1.44×. Con m = 0 devuelve exactamente la escala lineal.', render: p => finalProfile(p, u => .3 * u + .7 * u * u, 5)},
  ];


  const zoneRenders = new Map();
  sections.forEach(s => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${s.name}</h2><p>${s.desc}</p></div>${frameHtml(false, s.render)}${frameHtml(true, s.render)}`;
    section.querySelectorAll('.gpZone').forEach(zone => zoneRenders.set(zone, s.render));
    host.append(section);
  });

  host.addEventListener('click', event => {
    const card = event.target.closest('.presetCard[data-preset]');
    if (!card) return;
    const frameEl = card.closest('.frame');
    const zone = frameEl.querySelector('.gpZone');
    frameEl.querySelectorAll('.presetCard').forEach(c => {
      const on = c === card;
      c.classList.toggle('active', on);
      c.classList.toggle('selected', on);
    });
    zone.innerHTML = zoneRenders.get(zone)(PRESETS[+card.dataset.preset]);
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
