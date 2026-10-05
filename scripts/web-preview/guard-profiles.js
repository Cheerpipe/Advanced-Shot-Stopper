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
  // `piecewise` magnifies the guard zone with a fixed visual rule: the break
  // always sits at 30% of the plot height - 0..65% of the ceiling takes the
  // lower 30% and 65%..ceiling the upper 70%, for every preset.
  function finalProfile(p, piecewise = false) {
    const W = 260, L = 0, R = 2, T = 8, B = 76;
    const FC = '#d97706', SC = '#5594dd'; // rule colors: orange fast, blue slow
    const x = t => L + (W - R - L) * t / TIME.wall;
    const bp = p.ceil * .65;
    const y = v => piecewise
      ? (v <= bp ? T + (B - T) * .3 * v / bp
                 : T + (B - T) * (.3 + .7 * (v - bp) / (p.ceil - bp)))
      : B - (B - T) * v / p.ceil;
    const path = pts => pts.map((q, i) => (i ? 'L' : 'M') + x(q[0]).toFixed(1) + ' ' + y(q[1]).toFixed(1)).join(' ');
    const line = (pts, color, dash) => `<path d="${path(pts)}" fill="none" stroke="${color}" stroke-width=".8" stroke-linecap="round" stroke-linejoin="round"${dash ? ` stroke-dasharray="${dash}"` : ''}/>`;
    const xMark = (t, v, color) => { const cx = x(t), cy = y(v);
      return `<path d="M${(cx - 1.6).toFixed(1)} ${(cy - 1.6).toFixed(1)}L${(cx + 1.6).toFixed(1)} ${(cy + 1.6).toFixed(1)}M${(cx - 1.6).toFixed(1)} ${(cy + 1.6).toFixed(1)}L${(cx + 1.6).toFixed(1)} ${(cy - 1.6).toFixed(1)}" stroke="${color}" stroke-width=".9" stroke-linecap="round"/>`; };
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${T}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${B - T}" fill="${color}" fill-opacity="${op}"/>`;
    const spread = (items, min) => { const list = [...items].sort((a, b) => a - b);
      for (let i = 1; i < list.length; i++) if (list[i] - list[i - 1] < min) list[i] = list[i - 1] + min;
      return list; };
    const [cyy, tyy, fyy] = spread([y(p.ceil), y(p.target), y(p.floor)], 10);
    const top = u => (u / 78 * 100).toFixed(2) + '%';
    return `<figure class="gpFig"><div class="gpWrap">` +
      `<svg viewBox="0 0 ${W} 78" role="img" aria-label="Guard limits for ${esc(p.name)}: a fast shot reaches the ceiling by second ${TIME.prot} (the earliest the firmware can cut) and the fast guard cuts at ${TIME.tMin} seconds; a normal shot cuts at ${esc(g(p.target))} anywhere between ${TIME.tMin} and ${TIME.tMaxBbw} seconds; a slow shot is poured to ${esc(g(p.floor))} between ${TIME.tMaxBbw} and ${TIME.wall} seconds; machine limit ${TIME.wall} seconds"> a normal shot cuts at ${esc(g(p.target))} anywhere between ${TIME.tMin} and ${TIME.tMaxBbw} seconds; a slow shot is poured to ${esc(g(p.floor))} between ${TIME.tMaxBbw} and ${TIME.wall} seconds; machine limit ${TIME.wall} seconds">` +
      band(0, TIME.tMin, FC, .22) + band(TIME.tMin, TIME.tMaxBbw, 'var(--ok)', .22) + band(TIME.tMaxBbw, TIME.wall, SC, .22) +
      `<path d="M${x(0).toFixed(1)} ${y(0).toFixed(1)}L${x(TIME.prot).toFixed(1)} ${y(p.ceil).toFixed(1)}H${x(TIME.tMin).toFixed(1)}V${y(p.target).toFixed(1)}Z" fill="${FC}" fill-opacity=".12"/>` +
      [p.ceil, p.target, p.floor].map(v => `<path d="M${L} ${y(v).toFixed(1)}H${W - R}" stroke="var(--mu)" stroke-width=".5" stroke-dasharray="1.6 2.6" opacity=".7"/>`).join('') +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${T}V${B}M${x(TIME.tMaxBbw).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width=".5" stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      xMark(TIME.tMin, p.ceil, FC) + xMark(TIME.tMaxBbw, p.target, 'var(--ok)') + xMark(TIME.wall, p.floor, SC) +
      (piecewise ? `<path d="M0 ${(y(bp) - 1.6).toFixed(1)}l6.5 -2.4M0 ${(y(bp) + 1.6).toFixed(1)}l6.5 -2.4" stroke="var(--mu)" stroke-width=".7" stroke-linecap="round"/>` : '') +
      line([[0, 0], [TIME.tMin, p.target]], 'var(--ok)', '3 2.6') +
      line([[TIME.tMin, p.target], [TIME.tMaxBbw, p.target]], 'var(--ok)') +
      line([[0, 0], [TIME.tMaxBbw, p.floor]], SC, '3 2.6') +
      line([[TIME.tMaxBbw, p.floor], [TIME.wall, p.floor]], SC) +
      line([[0, 0], [TIME.prot, p.ceil]], FC, '3 2.6') +
      line([[TIME.prot, p.ceil], [TIME.tMin, p.ceil]], FC) +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${y(p.ceil).toFixed(1)}V${y(p.target).toFixed(1)}" stroke="${FC}" stroke-width=".8" stroke-linecap="round"/>` +
      `</svg>` +
      `<span class="shotYTick" style="top:${top(cyy)}">${esc(g(p.ceil))}</span>` +
      `<span class="shotYTick" style="top:${top(tyy)}">${esc(g(p.target))}</span>` +
      `<span class="shotYTick" style="top:${top(fyy)}">${esc(g(p.floor))}</span>` +
      `</div>` +
      `<div class="ruleChartTicks" style="margin:.15rem 2.7rem 0 0"><span class="ruleTick" style="left:0">0 s</span><span class="ruleTick" style="left:56%">28 s</span><span class="ruleTick" style="left:88%">44 s</span><span class="ruleTick" style="left:100%">50 s</span></div>` +
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
    {name: '01 · Versión final · escala lineal', desc: 'Los límites de los guardias dibujados solo con datos reales, en los colores de los gráficos de Stats: la rápida (naranjo) sube hasta su corte de 28 s × 42.5 g —su rango posible, entre 36 y 42.5 g, es el abanico sobre la compuerta—; la BBW normal (verde) llega en diagonal a 36 g y sigue plana entre 28 y 44 s, donde puede ocurrir el corte; la lenta (celeste) sube hasta el piso de 34 g y se corta en plano de 44 a 50 s. Cada línea va punteada hasta su activación y sólida desde ahí. En escala lineal, el piso (34 g) y el objetivo (36 g) quedan muy cerca.', render: finalProfile},
    {name: '02 · Escala partida en la zona de guardias', desc: 'Mismo gráfico con una escala lineal por tramos, con regla fija para cualquier preset: el 30% inferior de la altura cubre de 0 al 65% del techo (comprimido) y el 70% superior magnifica la zona de guardias, donde los valores siempre vienen juntos. El piso y el objetivo se separan al doble sin dejar de ser monotónico; las marcas dobles del borde izquierdo, siempre a la misma altura, revelan el punto de corte de escala.', render: p => finalProfile(p, true)},
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
