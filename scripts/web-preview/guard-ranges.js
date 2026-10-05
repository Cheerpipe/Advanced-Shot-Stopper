'use strict';

(() => {
  // Real preset data from the Home quick settings screenshots.
  const TIME = {tMin: 28, tMaxBbw: 44, wall: 50}; // min/max BBW brew time (s) and machine operational wall (s)
  const PRESETS = [
    {name: 'Double', badge: 'Factory', target: 36, floor: 34, ceil: 42.5},
    {name: 'Single', badge: 'Factory', target: 18, floor: 16, ceil: 20},
  ];
  const g = v => (Number.isInteger(v) ? v : v.toFixed(1)) + ' g';
  const pct = (v, max) => Math.max(0, Math.min(100, v / max * 100));
  const icon = inner => `<svg viewBox="0 0 24 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${inner}</g></svg>`;
  const CLOCK = icon('<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>');
  const TARGET = icon('<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="4"/><circle cx="12" cy="12" r="1" fill="currentColor" stroke="none"/>');
  const BOLT = icon('<path d="M13 3 5 13.5h5L11 21l8-10.5h-5z"/>');
  const HOURGLASS = icon('<path d="M8 3h8v3.2L12 11 8 6.2V3zM8 21h8v-3.2L12 13l-4 4.8V21z"/>');

  // 00 · Replica of the current two-track chart, with its hard-coded colors.
  function baselineZone(p) {
    const segs = (list, max, lab) => list.map(([kind, a, b]) =>
      `<span class="ruleSeg ruleSeg-${kind}" style="left:${pct(a, max).toFixed(2)}%;width:${(pct(b, max) - pct(a, max)).toFixed(2)}%">${kind === 'bbw' ? lab : kind[0].toUpperCase() + kind.slice(1)}</span>`).join('');
    const ticks = (list, max) => list.map(([v, txt]) => `<span class="ruleTick" style="left:${pct(v, max).toFixed(2)}%">${txt}</span>`).join('');
    return `<div class="ruleChart" aria-live="polite">` +
      `<div class="ruleChartRow"><div class="ruleChartLabel">Time (s)</div><div class="ruleChartTrack" role="img" aria-label="Time: fast guard before ${TIME.tMin} seconds, brew window ${TIME.tMin} to ${TIME.tMaxBbw} seconds, slow guard after">${segs([['fast', 0, TIME.tMin], ['bbw', TIME.tMin, TIME.tMaxBbw], ['slow', TIME.tMaxBbw, TIME.wall]], TIME.wall, 'BBW')}</div><div class="ruleChartTicks">${ticks([[0, '0 s'], [TIME.tMin, '28 s'], [TIME.tMaxBbw, '44 s'], [TIME.wall, '50 s']], TIME.wall)}</div></div>` +
      `<div class="ruleChartRow"><div class="ruleChartLabel">Weight (g)</div><div class="ruleChartTrack" role="img" aria-label="Weight: slow guard floor ${g(p.floor)}, target ${g(p.target)}, fast guard ceiling ${g(p.ceil)}">${segs([['slow', 0, p.floor], ['bbw', p.floor, p.target], ['fast', p.target, p.ceil]], p.ceil, 'BBW')}</div><div class="ruleChartTicks">${ticks([[0, '0 g'], [p.floor, g(p.floor)], [p.target, g(p.target)], [p.ceil, g(p.ceil)]], p.ceil)}</div></div></div>`;
  }

  // 09 · Arc gauge spanning the machine wall, with the brew window highlighted.
  function gauge(p) {
    const cx = 75, cy = 66;
    const P = (t, rad) => { const a = Math.PI * (1 - t / TIME.wall); return [cx + rad * Math.cos(a), cy - rad * Math.sin(a)]; };
    const arc = (t0, t1, rad) => { const [x0, y0] = P(t0, rad), [x1, y1] = P(t1, rad); return `M${x0.toFixed(1)} ${y0.toFixed(1)} A${rad} ${rad} 0 0 1 ${x1.toFixed(1)} ${y1.toFixed(1)}`; };
    const notch = (t, r0, r1) => { const [x0, y0] = P(t, r0), [x1, y1] = P(t, r1); return `M${x0.toFixed(1)} ${y0.toFixed(1)}L${x1.toFixed(1)} ${y1.toFixed(1)}`; };
    const at = (t, rad) => P(t, rad).map(v => v.toFixed(1));
    const [b1x, b1y] = at(TIME.tMin, 60), [b2x, b2y] = at(TIME.tMaxBbw, 60);
    const [lw, lwY] = at(11, 61), [rw, rwY] = at(42, 61);
    const [lf, lfY] = at(11.5, 36), [ls, lsY] = at(42, 36);
    return `<div class="grGaugeWrap"><figure class="grGauge"><svg viewBox="0 0 150 80" role="img" aria-label="Arc from 0 to ${TIME.wall} seconds: fast guard before ${TIME.tMin} seconds may run to ${g(p.ceil)}; ${TIME.tMin} to ${TIME.tMaxBbw} seconds on target at ${g(p.target)}; slow guard after ${TIME.tMaxBbw} seconds stops at ${g(p.floor)}">` +
      `<g fill="none" stroke-width="9"><path d="${arc(0, TIME.tMin, 50)}" stroke="var(--wn)" stroke-opacity=".32"/><path d="${arc(TIME.tMin, TIME.tMaxBbw, 50)}" stroke="var(--ok)"/><path d="${arc(TIME.tMaxBbw, TIME.wall, 50)}" stroke="var(--dn)" stroke-opacity=".32"/></g>` +
      `<path d="${notch(TIME.tMin, 44, 57)}${notch(TIME.tMaxBbw, 44, 57)}" stroke="var(--fg)" stroke-width="1" fill="none"/>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle" font-weight="600"><text x="${b1x}" y="${+b1y + 2.4}">28 s</text><text x="${b2x}" y="${+b2y + 2.4}">44 s</text></g>` +
      `<g font-size="7" font-weight="650"><text x="${lw}" y="${+lwY + 2}" text-anchor="middle" fill="var(--wn)">to ${g(p.ceil)}</text><text x="${rw}" y="${+rwY + 2}" text-anchor="middle" fill="var(--dn)">stop ${g(p.floor)}</text></g>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${lf}" y="${+lfY + 2}">Fast</text><text x="${ls}" y="${+lsY + 2}">Slow</text></g>` +
      `<text x="${cx}" y="55" text-anchor="middle" font-size="13" font-weight="700" fill="var(--fg)">${g(p.target)}</text>` +
      `<text x="${cx}" y="64" text-anchor="middle" font-size="6.5" fill="var(--mu)">target · pour ${TIME.tMin}–${TIME.tMaxBbw} s</text>` +
      `</svg></figure></div>`;
  }

  // 10 · Fill profile with the three scenarios: on target, fast-extended, slow-stopped.
  function profile(p) {
    const W = 260, L = 8, R = 34, T = 8, B = 76;
    const x = t => L + (W - R - L) * t / TIME.wall;
    const y = w => B - (B - T) * w / p.ceil;
    const path = pts => pts.map((pt, i) => (i ? 'L' : 'M') + x(pt[0]).toFixed(1) + ' ' + y(pt[1]).toFixed(1)).join(' ');
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${T}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${B - T}" fill="${color}" fill-opacity="${op}"/>`;
    const grid = w => { const gy = y(w).toFixed(1); return {line: `<path d="M${L} ${gy}H${W - R}" stroke="var(--ln)" stroke-dasharray="2 3"/>`, label: `<text x="${W - R + 4}" y="${+gy + 2.3}" font-size="6.5" fill="var(--mu)">${g(w)}</text>`}; };
    const gFloor = grid(p.floor), gTarget = grid(p.target), gCeil = grid(p.ceil);
    const slowHold = +(p.floor * .93).toFixed(1), slowNear = +(p.floor * .97).toFixed(1), fastMid = +((p.target + p.ceil) / 2).toFixed(1);
    return `<figure class="grProfile"><svg viewBox="0 0 ${W} 88" role="img" aria-label="Weight over time from 0 to ${TIME.wall} seconds: the on-target shot reaches ${g(p.target)} inside the ${TIME.tMin} to ${TIME.tMaxBbw} second window; a fast shot reaches the target before ${TIME.tMin} seconds and the guard extends it to ${g(p.ceil)}; a slow shot is still short after ${TIME.tMaxBbw} seconds and the guard stops it once it reaches ${g(p.floor)}">` +
      band(0, TIME.tMin, 'var(--wn)', .07) + band(TIME.tMin, TIME.tMaxBbw, 'var(--ok)', .06) + band(TIME.tMaxBbw, TIME.wall, 'var(--dn)', .07) +
      `<text x="${L + 4}" y="${T + 7}" font-size="6" font-weight="600" fill="var(--wn)">Fast guard</text>` +
      `<text x="${W - R - 4}" y="${T + 7}" font-size="6" font-weight="600" fill="var(--dn)" text-anchor="end">Slow guard</text>` +
      gCeil.line + gTarget.line + gFloor.line + gCeil.label + gTarget.label + gFloor.label +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${T}V${B}M${x(TIME.tMaxBbw).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width=".7" stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      `<path d="${path([[0, 0], [10, 2], [26, +(.55 * p.target).toFixed(1)], [42, p.target]])}" fill="none" stroke="var(--ok)" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>` +
      `<path d="${path([[0, 0], [5, 3], [13, +(.5 * p.target).toFixed(1)], [22, p.target]])}" fill="none" stroke="var(--wn)" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/><path d="${path([[22, p.target], [32, fastMid], [40, p.ceil]])}" fill="none" stroke="var(--wn)" stroke-width="1.2" stroke-dasharray="3 2.4" stroke-linecap="round"/>` +
      `<path d="${path([[0, 0], [12, 1], [28, +(.45 * p.target).toFixed(1)], [44, slowHold]])}" fill="none" stroke="var(--dn)" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/><path d="${path([[44, slowHold], [47, slowNear], [50, p.floor]])}" fill="none" stroke="var(--dn)" stroke-width="1.2" stroke-dasharray="3 2.4" stroke-linecap="round"/>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${L}" y="85">0</text><text x="${x(TIME.tMin).toFixed(1)}" y="85">28 s</text><text x="${x(TIME.tMaxBbw).toFixed(1)}" y="85">44 s</text><text x="${x(TIME.wall).toFixed(1)}" y="85">50 s</text></g>` +
      `</svg><figcaption class="grProfileLegend"><span class="ok">on target · ${g(p.target)}</span><span class="wn">fast · extends to ${g(p.ceil)}</span><span class="dn">slow · stops at ${g(p.floor)}</span></figcaption></figure>`;
  }

  const PROPOSALS = [
    {name: 'Línea de receta', star: true,
      desc: 'Una línea con el objetivo y la ventana, y una segunda corta con lo que hace cada guardia. Cada número va nombrado y el texto ocupa una fracción del espacio actual; sin gráfica y sin color adicional.',
      html: p => `<div class="grLine"><p class="grMain"><b>${g(p.target)}</b> in <b>${TIME.tMin}–${TIME.tMaxBbw} s</b></p><p class="grSub">Fast guard: target before ${TIME.tMin} s → up to <b>${g(p.ceil)}</b> · Slow guard: past ${TIME.tMaxBbw} s → stops at <b>${g(p.floor)}</b></p></div>`},
    {name: 'Frase guía',
      desc: 'El comportamiento contado en lenguaje natural: qué es un buen tiro y qué hace cada guardia con su tiempo y su peso. Texto puro, sin gráfica ni códigos de color, con los nombres de los guardias en el tono de aviso del tema.',
      html: p => `<p class="grProse">A good shot pours <b>${g(p.target)}</b> in <b>${TIME.tMin}–${TIME.tMaxBbw} s</b>. Reach the target before <b>${TIME.tMin} s</b> and the <b class="grWn">Fast guard</b> may let it run to <b>${g(p.ceil)}</b>; still short after <b>${TIME.tMaxBbw} s</b> and the <b class="grDn">Slow guard</b> stops it at <b>${g(p.floor)}</b>.</p>`},
    {name: 'Filas por guardia',
      desc: 'Una fila por guardia, cada una con su punto de color del tema: el tiempo que vigila y el peso que aplica, juntos en la misma línea, así la relación queda dentro de cada fila.',
      html: p => `<div class="grRows"><p class="grHead">Target <b>${g(p.target)}</b> · window <b>${TIME.tMin}–${TIME.tMaxBbw} s</b></p><div class="grGuard"><i class="grDot wn" aria-hidden="true"></i><div><b>Fast guard</b><span>Target before <b>${TIME.tMin} s</b> → may run to <b>${g(p.ceil)}</b></span></div></div><div class="grGuard"><i class="grDot dn" aria-hidden="true"></i><div><b>Slow guard</b><span>Past <b>${TIME.tMaxBbw} s</b> → pours to at least <b>${g(p.floor)}</b></span></div></div></div>`},
    {name: 'Tarjeta de valores',
      desc: 'Cuatro celdas tipo insignia con los fondos suaves que la app ya usa para métricas: objetivo, ventana de llenado y cada guardia con su par tiempo → peso.',
      html: p => `<div class="grStats"><div><small>Target</small><b>${g(p.target)}</b></div><div><small>Brew window</small><b>${TIME.tMin}–${TIME.tMaxBbw} s</b></div><div class="wn"><small>Fast guard</small><b>&lt;${TIME.tMin} s · to ${g(p.ceil)}</b></div><div class="dn"><small>Slow guard</small><b>&gt;${TIME.tMaxBbw} s · stop ${g(p.floor)}</b></div></div>`},
    {name: 'Chips de rango',
      desc: 'Pastillas compactas con ícono: dos para la receta y una por guardia con su par de límites. En escritorio cabe en una sola línea; en teléfono se envuelve en dos.',
      html: p => `<div class="grChips"><span class="grChip acc">${CLOCK}${TIME.tMin}–${TIME.tMaxBbw} s</span><span class="grChip acc">${TARGET}${g(p.target)}</span><span class="grChip wn">${BOLT}&lt;${TIME.tMin} s → ${g(p.ceil)}</span><span class="grChip dn">${HOURGLASS}&gt;${TIME.tMaxBbw} s → ${g(p.floor)}</span></div>`},
    {name: 'Barra única, doble lectura', star: true,
      desc: 'Una sola barra de tres zonas con los tonos suaves del tema: dentro de cada zona el peso que aplica, y debajo los ticks de tiempo alineados con las fronteras. Relaciona tiempo y peso en una sola lectura y ocupa menos de la mitad que la zona actual.',
      html: p => `<div class="grBar" role="img" aria-label="Fast guard before ${TIME.tMin} seconds may run to ${g(p.ceil)}; ${TIME.tMin} to ${TIME.tMaxBbw} seconds on target at ${g(p.target)}; slow guard after ${TIME.tMaxBbw} seconds stops at ${g(p.floor)}"><div class="wn grSeg"><b>to ${g(p.ceil)}</b><span>Fast</span></div><div class="ok grSeg"><b>${g(p.target)}</b><span>On target</span></div><div class="dn grSeg"><b>stop ${g(p.floor)}</b><span>Slow</span></div></div><div class="ruleChartTicks grBarTicks"><span class="ruleTick" style="left:0">0 s</span><span class="ruleTick" style="left:${pct(TIME.tMin, TIME.wall).toFixed(2)}%">28 s</span><span class="ruleTick" style="left:${pct(TIME.tMaxBbw, TIME.wall).toFixed(2)}%">44 s</span><span class="ruleTick" style="left:100%">50 s</span></div>`},
    {name: 'Tarjetas de umbral',
      desc: 'Dos mini-tarjetas, una por guardia, con el par de números en grande y el verbo que explica cada uno: termina antes de la frontera rápida y puede llegar al techo; se pasa de la frontera lenta y se detiene al llegar al suelo.',
      html: p => `<div class="grPairs"><div class="grPair"><header class="fast"><i aria-hidden="true"></i>Fast guard</header><div class="grNums"><div><small>Ends before</small><b>${TIME.tMin} s</b></div><div><small>May reach</small><b>${g(p.ceil)}</b></div></div></div><div class="grPair"><header class="slow"><i aria-hidden="true"></i>Slow guard</header><div class="grNums"><div><small>Runs past</small><b>${TIME.tMaxBbw} s</b></div><div><small>Stops at</small><b>${g(p.floor)}</b></div></div></div></div><p class="grPairsHint">On target: <b>${g(p.target)}</b> within <b>${TIME.tMin}–${TIME.tMaxBbw} s</b></p>`},
    {name: 'Matriz guardia × magnitud',
      desc: 'Tabla mínima con una fila por guardia y una columna por magnitud, con el objetivo como pie de tabla. La forma más densa de escanear los cuatro números.',
      html: p => `<table class="grTable"><thead><tr><th scope="col">Guard</th><th scope="col">Time</th><th scope="col">Weight</th></tr></thead><tbody><tr><th scope="row"><i class="fastI" aria-hidden="true"></i>Fast</th><td>before <b>${TIME.tMin} s</b></td><td>may run to <b>${g(p.ceil)}</b></td></tr><tr><th scope="row"><i class="slowI" aria-hidden="true"></i>Slow</th><td>after <b>${TIME.tMaxBbw} s</b></td><td>stops at <b>${g(p.floor)}</b></td></tr></tbody><caption>On target: <b>${g(p.target)}</b> in <b>${TIME.tMin}–${TIME.tMaxBbw} s</b></caption></table>`},
    {name: 'Medidor en arco',
      desc: 'Un arco de 0 a 50 s con la ventana buena resaltada en el verde del tema y las fronteras 28/44 s marcadas; a los lados, el peso consecuencia de cada guardia y, al centro, el objetivo. La gráfica «rica» en poco espacio.',
      html: gauge},
    {name: 'Perfil tiempo → peso', star: true,
      desc: 'La curva de llenado con los tres escenarios: el tiro bueno alcanza el objetivo dentro de la ventana; el rápido llega antes de la frontera rápida y el guardia lo extiende hasta el techo; el lento sigue corto pasada la frontera lenta y se detiene al alcanzar el suelo. Ejes con unidades y leyenda; la relación tiempo–peso es explícita.',
      html: profile},
  ];

  const frame = (phone, render) =>
    `<div class="frame${phone ? ' phone' : ''}"><p class="frameLabel">${phone ? 'Teléfono · 360 px' : 'Escritorio'}</p>` +
    `<fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>` +
    `<div class="presetCards">${PRESETS.map((p, i) =>
      `<div class="presetCard${i === 0 ? ' active selected' : ''}" data-preset="${i}"><div class="presetCardTitleRow"><div class="presetCardTitle">${p.name}</div><span class="presetCardBadge">${p.badge}</span></div><div class="presetCardMeta">Target ${g(p.target)}</div></div>`).join('')}</div>` +
    `<div class="grZone">${render(PRESETS[0])}</div></fieldset></div>`;

  const host = document.getElementById('options');
  const sections = [{name: 'Zona actual (referencia)', desc: 'La zona tal como se ve hoy en el Home: dos pistas con etiquetas y ticks, cuatro filas de alto, números sin explicación y colores fijos ajenos al tema. Se conserva como referencia para comparar.', render: baselineZone}];
  PROPOSALS.forEach(p => sections.push({...p, render: p.html}));
  const zoneRenders = new Map();
  sections.forEach((s, i) => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${String(i).padStart(2, '0')} · ${s.name}${s.star ? ' <span class="star">Recomendada</span>' : ''}</h2><p>${s.desc}</p></div>${frame(false, s.render)}${frame(true, s.render)}`;
    section.querySelectorAll('.grZone').forEach(zone => zoneRenders.set(zone, s.render));
    host.append(section);
  });

  host.addEventListener('click', event => {
    const card = event.target.closest('.presetCard[data-preset]');
    if (!card) return;
    const frameEl = card.closest('.frame');
    const zone = frameEl.querySelector('.grZone');
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
