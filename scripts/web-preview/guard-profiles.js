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
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${L}" y="85">0</text><text x="${x(TIME.tMin).toFixed(1)}" y="85">28 s</text><text x="${x(TIME.tMaxBbw).toFixed(1)}" y="85">44 s</text><text x="${x(TIME.wall).toFixed(1)}" y="85">50 s</text></g>` +
      `</svg><figcaption class="gpRefLegend"><span class="ok">on target · ${esc(g(p.target))}</span><span class="wn">fast · extends to ${esc(g(p.ceil))}</span><span class="dn">slow · stops at ${esc(g(p.floor))}</span></figcaption></figure>`;
  }

  // Iteration 2 · the final chart: every element is a guard limit, no measured curves.
  function finalProfile(p) {
    const W = 260, L = 8, R = 34, T = 8, B = 76;
    const x = t => L + (W - R - L) * t / TIME.wall;
    const y = v => B - (B - T) * v / p.ceil;
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${T}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${B - T}" fill="${color}" fill-opacity="${op}"/>`;
    const spread = (items, min) => { const list = [...items].sort((a, b) => a - b);
      for (let i = 1; i < list.length; i++) if (list[i] - list[i - 1] < min) list[i] = list[i - 1] + min;
      return list; };
    const [cy, ty, fy] = spread([y(p.ceil), y(p.target), y(p.floor)], 7);
    const limit = (v, ly) => { const gy = ly.toFixed(1); return {line: `<path d="M${L} ${y(v).toFixed(1)}H${W - R}" stroke="var(--ln)" stroke-width=".5" stroke-dasharray="2 3"/>`, label: `<text x="${W - R + 4}" y="${+gy + 2.3}" font-size="6.5" fill="var(--mu)">${esc(g(v))}</text>`}; };
    const lFloor = limit(p.floor, fy), lTarget = limit(p.target, ty), lCeil = limit(p.ceil, cy);
    return `<figure class="gpFig"><svg viewBox="0 0 ${W} 88" role="img" aria-label="Guard limits for ${esc(p.name)}: pour target ${esc(g(p.target))} between ${TIME.tMin} and ${TIME.tMaxBbw} seconds; a shot that reaches ${esc(g(p.target))} by ${TIME.tMin} seconds may extend to ${esc(g(p.ceil))}; a shot still short of ${esc(g(p.floor))} at ${TIME.tMaxBbw} seconds is poured down to it; machine limit ${TIME.wall} seconds">` +
      band(0, TIME.tMin, 'var(--wn)', .07) + band(TIME.tMin, TIME.tMaxBbw, 'var(--ok)', .06) + band(TIME.tMaxBbw, TIME.wall, 'var(--dn)', .07) +
      `<text x="${L + 4}" y="${T + 7}" font-size="6" font-weight="600" fill="var(--wn)">Fast guard</text>` +
      `<text x="${W - R - 4}" y="${T + 7}" font-size="6" font-weight="600" fill="var(--dn)" text-anchor="end">Slow guard</text>` +
      lCeil.line + lTarget.line + lFloor.line + lCeil.label + lTarget.label + lFloor.label +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${T}V${B}M${x(TIME.tMaxBbw).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width=".5" stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      `<circle cx="${x(TIME.tMin).toFixed(1)}" cy="${y(p.target).toFixed(1)}" r="3.2" fill="var(--sf)" stroke="var(--wn)" stroke-width="1.7"/>` +
      `<circle cx="${x(TIME.tMaxBbw).toFixed(1)}" cy="${y(p.floor).toFixed(1)}" r="3.2" fill="var(--sf)" stroke="var(--dn)" stroke-width="1.7"/>` +
      `<text x="${(x(TIME.tMin) + 6).toFixed(1)}" y="${(y(p.floor) + 10).toFixed(1)}" font-size="6" font-weight="650" fill="var(--wn)">${esc(g(p.target))} → ${esc(g(p.ceil))}</text>` +
      `<text x="${(x(TIME.tMaxBbw) - 7).toFixed(1)}" y="${(y(p.floor) + 20).toFixed(1)}" font-size="6" font-weight="650" fill="var(--dn)" text-anchor="end">stops at ${esc(g(p.floor))}</text>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${L}" y="85">0</text><text x="${x(TIME.tMin).toFixed(1)}" y="85">28 s</text><text x="${x(TIME.tMaxBbw).toFixed(1)}" y="85">44 s</text><text x="${x(TIME.wall).toFixed(1)}" y="85">50 s</text></g>` +
      `</svg><figcaption class="gpRefLegend"><span class="ok">pour 28–44 s · ${esc(g(p.target))}</span><span class="wn">28 s × ${esc(g(p.target))} → ${esc(g(p.ceil))}</span><span class="dn">44 s short → ${esc(g(p.floor))}</span></figcaption></figure>`;
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
    {name: '01 · Versión final · solo límites', desc: 'Sin curvas inventadas: solo los datos reales de los guardias. Bandas por tramo, los tres límites de peso (piso, objetivo, techo), las dos compuertas de tiempo, y un anillo en cada cruce con su consecuencia escrita al lado: 36 g a los 28 s → 42.5 g; corto de 34 g a los 44 s → parar ahí. Trazos finos de la iteración 1.', render: finalProfile},
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
