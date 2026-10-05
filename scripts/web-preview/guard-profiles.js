'use strict';

(() => {
  // Real preset data from the Home quick settings screenshots.
  const TIME = {tMin: 28, tMaxBbw: 44, wall: 50}; // min/max BBW brew time (s) and machine operational wall (s)
  const PRESETS = [
    {name: 'Double', badge: 'Factory', target: 36, floor: 34, ceil: 42.5},
    {name: 'Single', badge: 'Factory', target: 18, floor: 16, ceil: 20},
  ];
  const g = v => (Number.isInteger(v) ? v : v.toFixed(1)) + ' g';
  const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
  const n1 = v => +v.toFixed(1);

  // The original proposal-10 profile. `thin` is iteration 1: thinner colored strokes.
  function profile(p, thin) {
    const W = 260, L = 8, R = 34, T = 8, B = 76;
    const sw = thin ? {ok: 1, sc: .8, grid: .5, gate: .5} : {ok: 1.7, sc: 1.2, grid: 1, gate: .7};
    const x = t => L + (W - R - L) * t / TIME.wall;
    const y = v => B - (B - T) * v / p.ceil;
    const path = pts => pts.map((q, i) => (i ? 'L' : 'M') + x(q[0]).toFixed(1) + ' ' + y(q[1]).toFixed(1)).join(' ');
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${T}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${B - T}" fill="${color}" fill-opacity="${op}"/>`;
    const grid = v => { const gy = y(v).toFixed(1); return {line: `<path d="M${L} ${gy}H${W - R}" stroke="var(--ln)" stroke-width="${sw.grid}" stroke-dasharray="2 3"/>`, label: `<text x="${W - R + 4}" y="${+gy + 2.3}" font-size="6.5" fill="var(--mu)">${esc(g(v))}</text>`}; };
    const gFloor = grid(p.floor), gTarget = grid(p.target), gCeil = grid(p.ceil);
    const slowHold = n1(p.floor * .93), slowNear = n1(p.floor * .97), fastMid = n1(p.target + (p.ceil - p.target) / 2);
    return `<figure class="gpFig"><svg viewBox="0 0 ${W} 88" role="img" aria-label="Weight over time from 0 to ${TIME.wall} seconds: the on-target shot reaches ${esc(g(p.target))} inside the ${TIME.tMin} to ${TIME.tMaxBbw} second window; a fast shot reaches the target before ${TIME.tMin} seconds and the guard extends it to ${esc(g(p.ceil))}; a slow shot is still short after ${TIME.tMaxBbw} seconds and the guard stops it once it reaches ${esc(g(p.floor))}">` +
      band(0, TIME.tMin, 'var(--wn)', .07) + band(TIME.tMin, TIME.tMaxBbw, 'var(--ok)', .06) + band(TIME.tMaxBbw, TIME.wall, 'var(--dn)', .07) +
      `<text x="${L + 4}" y="${T + 7}" font-size="6" font-weight="600" fill="var(--wn)">Fast guard</text>` +
      `<text x="${W - R - 4}" y="${T + 7}" font-size="6" font-weight="600" fill="var(--dn)" text-anchor="end">Slow guard</text>` +
      gCeil.line + gTarget.line + gFloor.line + gCeil.label + gTarget.label + gFloor.label +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${T}V${B}M${x(TIME.tMaxBbw).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width="${sw.gate}" stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      `<path d="${path([[0, 0], [10, 2], [26, n1(.55 * p.target)], [42, p.target]])}" fill="none" stroke="var(--ok)" stroke-width="${sw.ok}" stroke-linecap="round" stroke-linejoin="round"/>` +
      `<path d="${path([[0, 0], [5, 3], [13, n1(.5 * p.target)], [22, p.target]])}" fill="none" stroke="var(--wn)" stroke-width="${sw.sc}" stroke-linecap="round" stroke-linejoin="round"/><path d="${path([[22, p.target], [32, fastMid], [40, p.ceil]])}" fill="none" stroke="var(--wn)" stroke-width="${sw.sc}" stroke-dasharray="3 2.4" stroke-linecap="round"/>` +
      `<path d="${path([[0, 0], [12, 1], [28, n1(.45 * p.target)], [44, slowHold]])}" fill="none" stroke="var(--dn)" stroke-width="${sw.sc}" stroke-linecap="round" stroke-linejoin="round"/><path d="${path([[44, slowHold], [47, slowNear], [50, p.floor]])}" fill="none" stroke="var(--dn)" stroke-width="${sw.sc}" stroke-dasharray="3 2.4" stroke-linecap="round"/>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${L}" y="85">0</text><text x="${x(TIME.tMin).toFixed(1)}" y="85">28 s</text><text x="${x(TIME.tMaxBbw).toFixed(1)}" y="85">44 s</text><text x="${x(TIME.wall).toFixed(1)}" y="85" text-anchor="end">50 s</text></g>` +
      `</svg><figcaption class="gpRefLegend"><span class="ok">on target · ${esc(g(p.target))}</span><span class="wn">fast · extends to ${esc(g(p.ceil))}</span><span class="dn">slow · stops at ${esc(g(p.floor))}</span></figcaption></figure>`;
  }

  // Iteration 3+ · the final chart, with weight-label placement strategies.
  function finalProfile(p, mode = 'right') {
    const geo = {
      right: {L: 8, Rp: 34, vbh: 88},
      leftGutter: {L: 30, Rp: 2, vbh: 88},
      leftHalo: {L: 8, Rp: 2, vbh: 88},
      rightHalo: {L: 8, Rp: 2, vbh: 88},
      interline: {L: 8, Rp: 2, vbh: 88},
      rotated: {L: 8, Rp: 16, vbh: 88},
      none: {L: 8, Rp: 2, vbh: 88},
      atCuts: {L: 8, Rp: 2, vbh: 88},
      underAxis: {L: 8, Rp: 2, vbh: 100},
      segStarts: {L: 8, Rp: 2, vbh: 88},
      miniLegend: {L: 8, Rp: 2, vbh: 88},
    }[mode] || {L: 8, Rp: 2, vbh: 88};
    const W = 260, L = geo.L, R = geo.Rp, T = 8, B = 76, VBH = geo.vbh;
    const FC = '#d97706', SC = '#2563eb'; // original rule-chart colors: orange fast, light blue slow
    const x = t => L + (W - R - L) * t / TIME.wall;
    const y = v => B - (B - T) * v / p.ceil;
    const spread = (items, min) => { const list = [...items].sort((a, b) => a - b);
      for (let i = 1; i < list.length; i++) if (list[i] - list[i - 1] < min) list[i] = list[i - 1] + min;
      return list; };
    const [cy, ty, fy] = spread([y(p.ceil), y(p.target), y(p.floor)], 7);
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${T}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${B - T}" fill="${color}" fill-opacity="${op}"/>`;
    const limits = [p.ceil, p.target, p.floor].map(v =>
      `<path d="M${L} ${y(v).toFixed(1)}H${W - R}" stroke="var(--ln)" stroke-width=".5" stroke-dasharray="2 3"/>`);
    const line = (pts, color, width, dash) => `<path d="${path(pts)}" fill="none" stroke="${color}" stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round"${dash ? ` stroke-dasharray="${dash}"` : ''}/>`;
    function path(pts) { return pts.map((q, i) => (i ? 'L' : 'M') + x(q[0]).toFixed(1) + ' ' + y(q[1]).toFixed(1)).join(' '); }
    const halo = t => t.replace('<text ', '<text paint-order="stroke" stroke="var(--sf)" stroke-width="2.4" ');
    const wText = (x0, y0, color, txt, anchor, rot) => halo(`<text x="${x0}" y="${y0.toFixed(1)}" font-size="6.5" font-weight="650" fill="${color}"${anchor === 'end' ? ' text-anchor="end"' : anchor === 'middle' ? ' text-anchor="middle"' : ''}${rot ? ` transform="rotate(-90 ${x0} ${y0.toFixed(1)})"` : ''}>${esc(txt)}</text>`);
    let weightLabels = '';
    if (mode === 'right') {
      weightLabels = [[cy, FC, g(p.ceil)], [ty, 'var(--ok)', g(p.target)], [fy, SC, g(p.floor)]]
        .map(it => wText(W - R + 4, it[0] + 2.3, it[1], it[2])).join('');
    } else if (mode === 'leftGutter') {
      weightLabels = [[cy, FC, g(p.ceil)], [ty, 'var(--ok)', g(p.target)], [fy, SC, g(p.floor)]]
        .map(it => wText(L - 4, it[0] + 2.3, it[1], it[2], 'end')).join('');
    } else if (mode === 'leftHalo' || mode === 'rightHalo') {
      const x0 = mode === 'leftHalo' ? L + 3 : W - 3;
      const anchor = mode === 'leftHalo' ? '' : 'end';
      weightLabels = [[cy, FC, g(p.ceil)], [ty, 'var(--ok)', g(p.target)], [fy, SC, g(p.floor)]]
        .map(it => wText(x0, it[0] + 2.3, it[1], it[2], anchor)).join('');
    } else if (mode === 'interline') {
      weightLabels = wText(W - 3, 6.8, FC, g(p.ceil), 'end') +
        wText(W - 3, (cy + ty) / 2 + 2.3, 'var(--ok)', g(p.target), 'end') +
        wText(W - 3, fy + 7.5, SC, g(p.floor), 'end');
    } else if (mode === 'rotated') {
      const rys = spread([Math.max(12, y(p.ceil)), Math.max(12, y(p.target)), Math.max(12, y(p.floor))], 16);
      weightLabels = [[p.ceil, FC], [p.target, 'var(--ok)'], [p.floor, SC]]
        .map((it, i) => wText(W - 13, rys[i], it[1], it[0].toFixed(1), 'middle', true)).join('');
    } else if (mode === 'atCuts') {
      weightLabels = wText(x(TIME.tMin) + 5, (y(p.ceil) + y(p.target)) / 2 + 2.3, FC, `${g(p.target)}–${g(p.ceil)}`) +
        wText(x(TIME.tMaxBbw) + 3, y(p.floor) + 7.5, SC, g(p.floor));
    } else if (mode === 'underAxis') {
      weightLabels = `<text x="${((L + W - R) / 2).toFixed(1)}" y="${B + 21}" text-anchor="middle" font-size="6.5" font-weight="650" fill="var(--mu)">${esc('weights (g):')} <tspan fill="${FC}">${p.ceil.toFixed(1)}</tspan> · <tspan fill="var(--ok)">${p.target.toFixed(1)}</tspan> · <tspan fill="${SC}">${p.floor.toFixed(1)}</tspan></text>`;
    } else if (mode === 'segStarts') {
      weightLabels = halo(wText(60, cy - 1.5, FC, g(p.ceil))) +
        halo(wText(x(TIME.tMin) + 4, ty - 2.5, 'var(--ok)', g(p.target))) +
        halo(wText(x(TIME.tMaxBbw) + 4, fy + 6.5, SC, g(p.floor)));
    } else if (mode === 'miniLegend') {
      weightLabels = [[p.ceil, FC, 'máx'], [p.target, 'var(--ok)', 'objetivo'], [p.floor, SC, 'mín']]
        .map(it => halo(wText(W - 3, spread([11, 19, 27], 11)[[p.ceil, p.target, p.floor].indexOf(it[0])], it[1], `${g(it[0])} ${it[2]}`, 'end'))).join('');
    }
    return `<figure class="gpFig"><svg viewBox="0 0 ${W} ${VBH}" role="img" aria-label="Guard limits for ${esc(p.name)}: a fast shot cuts at ${TIME.tMin} seconds with the weight anywhere between ${esc(g(p.target))} and ${esc(g(p.ceil))}; a normal shot cuts at ${esc(g(p.target))} anywhere between ${TIME.tMin} and ${TIME.tMaxBbw} seconds; a slow shot is poured to ${esc(g(p.floor))} between ${TIME.tMaxBbw} and ${TIME.wall} seconds; machine limit ${TIME.wall} seconds">` +
      band(0, TIME.tMin, FC, .22) + band(TIME.tMin, TIME.tMaxBbw, 'var(--ok)', .22) + band(TIME.tMaxBbw, TIME.wall, SC, .22) +
      `<path d="M${x(0).toFixed(1)} ${y(0).toFixed(1)}L${x(TIME.tMin).toFixed(1)} ${y(p.ceil).toFixed(1)}V${y(p.target).toFixed(1)}Z" fill="${FC}" fill-opacity=".12"/>` +
      limits.join('') +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${T}V${B}M${x(TIME.tMaxBbw).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width=".5" stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      line([[0, 0], [TIME.tMin, p.target]], 'var(--ok)', .8, '3 2.6') +
      line([[TIME.tMin, p.target], [TIME.tMaxBbw, p.target]], 'var(--ok)', .8) +
      line([[0, 0], [TIME.tMaxBbw, p.floor]], SC, .8, '3 2.6') +
      line([[TIME.tMaxBbw, p.floor], [TIME.wall, p.floor]], SC, .8) +
      line([[0, 0], [TIME.tMin, p.ceil]], FC, .8, '3 2.6') +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${y(p.ceil).toFixed(1)}V${y(p.target).toFixed(1)}" stroke="${FC}" stroke-width=".8" stroke-linecap="round"/>` +
      weightLabels +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${L}" y="85">0</text><text x="${x(TIME.tMin).toFixed(1)}" y="85">28 s</text><text x="${x(TIME.tMaxBbw).toFixed(1)}" y="85">44 s</text><text x="${x(TIME.wall).toFixed(1)}" y="85" text-anchor="end">50 s</text></g>` +
      `</svg><figcaption class="gpRefLegend"><span class="fc">fast · cuts at 28 s, ${esc(g(p.target))}–${esc(g(p.ceil))}</span><span class="ok">BBW · cuts at ${esc(g(p.target))}, 28–44 s</span><span class="sc">slow · ${esc(g(p.floor))} from 44 s</span></figcaption></figure>`;
  }

  const frameHtml = (phone, render) =>
    `<div class="frame${phone ? ' phone' : ''}"><p class="frameLabel">${phone ? 'Teléfono · 360 px' : 'Escritorio'}</p>` +
    `<fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>` +
    `<div class="presetCards">${PRESETS.map((p, i) =>
      `<div class="presetCard${i === 0 ? ' active selected' : ''}" data-preset="${i}"><div class="presetCardTitleRow"><div class="presetCardTitle">${p.name}</div><span class="presetCardBadge">${p.badge}</span></div><div class="presetCardMeta">Target ${g(p.target)}</div></div>`).join('')}</div>` +
    `<div class="gpZone">${render(PRESETS[0])}</div></fieldset></div>`;

  const host = document.getElementById('options');
  const sections = [
    {name: '00 · Perfil original (referencia)', desc: 'El perfil de la propuesta 10 con sus curvas de escenario. Se conserva como referencia del punto de partida.', render: p => profile(p, false)},
    {name: '01 · Versión final · etiquetas a la derecha (actual)', desc: 'El estado actual: los pesos a la derecha cuestan 34 px de ancho al gráfico.', render: p => finalProfile(p, 'right')},
    {name: '02 · Gutter izquierdo', desc: 'Los pesos pasan a una columna estrecha a la izquierda y el gráfico se extiende completa hasta el borde derecho.', render: p => finalProfile(p, 'leftGutter')},
    {name: '03 · Izquierda con halo', desc: 'Etiquetas dentro del gráfico, sobre el inicio de cada línea, con un contorno del color del fondo que las mantiene legibles sin tapar nada más.', render: p => finalProfile(p, 'leftHalo')},
    {name: '04 · Derecha con halo', desc: 'Igual que la actual pero dentro del área: el gráfico llega hasta el borde derecho y los pesos, con halo, se apoyan sobre el final de cada línea.', render: p => finalProfile(p, 'rightHalo')},
    {name: '05 · Interlineal derecha', desc: 'Cada peso se coloca en el hueco libre más cercano a su línea, alineado a la derecha: el techo sobre el borde superior, el objetivo entre líneas y el piso bajo su línea. Sin halo.', render: p => finalProfile(p, 'interline')},
    {name: '06 · Rotadas en el borde', desc: 'Pesos en vertical sobre el borde derecho, centrados con cada línea; el gráfico casi no cede ancho.', render: p => finalProfile(p, 'rotated')},
    {name: '07 · Sin etiquetas', desc: 'El gráfico se extiende completa y limpia; los tres pesos viven en la leyenda inferior, junto a cada situación.', render: p => finalProfile(p, 'none')},
    {name: '08 · En los puntos de corte', desc: 'Sin etiquetas de eje: el rango 36–42.5 g aparece junto al segmento vertical de 28 s y el piso 34 g junto al tramo lento, justo donde se usan.', render: p => finalProfile(p, 'atCuts')},
    {name: '09 · Bajo el eje de tiempo', desc: 'Los tres pesos, coloreados, en una línea bajo el eje de tiempo; el gráfico completo llega al borde derecho.', render: p => finalProfile(p, 'underAxis')},
    {name: '10 · Al inicio de cada tramo', desc: 'Cada peso se apoya, con halo, sobre el inicio del tramo donde aplica: el techo sobre su línea, el objetivo sobre el tramo plano BBW y el piso sobre el tramo lento.', render: p => finalProfile(p, 'segStarts')},
    {name: '11 · Mini-leyenda superior', desc: 'Una mini-leyenda de tres filas en la esquina superior derecha (techo, objetivo, piso) con halo; el gráfico completo llega al borde derecho.', render: p => finalProfile(p, 'miniLegend')},
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
