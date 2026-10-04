'use strict';

(() => {
  // Sample preset values, matching the Home preview sample ("Classic espresso").
  const DATA = {
    preset: 'Classic espresso',
    target: 36.0,          // goal weight (g)
    tMin: 25,              // min BBW brew time (s)
    tMaxBbw: 35,           // max BBW brew time (s)
    wall: 45,              // machine operational wall (s)
    floor: 30.0,           // slow guard recovery floor (g)
    ceil: 40.0,            // fast guard recovery ceiling (g)
    wMax: 40.0,            // weight axis end (g)
  };
  const g = v => v.toFixed(1) + ' g';
  const pct = (v, max) => Math.max(0, Math.min(100, v / max * 100));
  const icon = inner => `<svg viewBox="0 0 24 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${inner}</g></svg>`;
  const CLOCK = icon('<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>');
  const TARGET = icon('<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="4"/><circle cx="12" cy="12" r="1" fill="currentColor" stroke="none"/>');
  const BOLT = icon('<path d="M13 3 5 13.5h5L11 21l8-10.5h-5z"/>');
  const HOURGLASS = icon('<path d="M8 3h8v3.2L12 11 8 6.2V3zM8 21h8v-3.2L12 13l-4 4.8V21z"/>');

  // 00 · Replica of the current two-track chart, with its hard-coded colors.
  function baselineZone() {
    const segs = (list, max, lab) => list.map(([kind, a, b]) =>
      `<span class="ruleSeg ruleSeg-${kind}" style="left:${pct(a, max).toFixed(2)}%;width:${(pct(b, max) - pct(a, max)).toFixed(2)}%">${kind === 'bbw' ? lab : kind[0].toUpperCase() + kind.slice(1)}</span>`).join('');
    const ticks = (list, max) => list.map(([v, txt]) => `<span class="ruleTick" style="left:${pct(v, max).toFixed(2)}%">${txt}</span>`).join('');
    return `<div class="ruleChart" aria-live="polite">` +
      `<div class="ruleChartRow"><div class="ruleChartLabel">Time (s)</div><div class="ruleChartTrack" role="img" aria-label="Time: fast guard before ${DATA.tMin} seconds, brew window ${DATA.tMin} to ${DATA.tMaxBbw} seconds, slow guard after">${segs([['fast', 0, DATA.tMin], ['bbw', DATA.tMin, DATA.tMaxBbw], ['slow', DATA.tMaxBbw, DATA.wall]], DATA.wall, 'BBW')}</div><div class="ruleChartTicks">${ticks([[0, '0 s'], [DATA.tMin, '25 s'], [DATA.tMaxBbw, '35 s'], [DATA.wall, '45 s']], DATA.wall)}</div></div>` +
      `<div class="ruleChartRow"><div class="ruleChartLabel">Weight (g)</div><div class="ruleChartTrack" role="img" aria-label="Weight: slow guard floor 30 grams, target 36 grams, fast guard ceiling 40 grams">${segs([['slow', 0, DATA.floor], ['bbw', DATA.floor, DATA.target], ['fast', DATA.target, DATA.ceil]], DATA.wMax, 'BBW')}</div><div class="ruleChartTicks">${ticks([[0, '0 g'], [DATA.floor, '30 g'], [DATA.target, '36 g'], [DATA.wMax, '40 g']], DATA.wMax)}</div></div></div>`;
  }

  // 09 · Arc gauge spanning the machine wall, with the brew window highlighted.
  function gauge() {
    const cx = 75, cy = 66;
    const P = (t, rad) => { const a = Math.PI * (1 - t / DATA.wall); return [cx + rad * Math.cos(a), cy - rad * Math.sin(a)]; };
    const arc = (t0, t1, rad) => { const [x0, y0] = P(t0, rad), [x1, y1] = P(t1, rad); return `M${x0.toFixed(1)} ${y0.toFixed(1)} A${rad} ${rad} 0 0 1 ${x1.toFixed(1)} ${y1.toFixed(1)}`; };
    const notch = (t, r0, r1) => { const [x0, y0] = P(t, r0), [x1, y1] = P(t, r1); return `M${x0.toFixed(1)} ${y0.toFixed(1)}L${x1.toFixed(1)} ${y1.toFixed(1)}`; };
    const at = (t, rad) => P(t, rad).map(v => v.toFixed(1));
    const [b1x, b1y] = at(DATA.tMin, 60), [b2x, b2y] = at(DATA.tMaxBbw, 60);
    const [lw, lwY] = at(11, 61), [rw, rwY] = at(40, 61);
    const [lf, lfY] = at(11.5, 36), [ls, lsY] = at(40, 36);
    return `<div class="grGaugeWrap"><figure class="grGauge"><svg viewBox="0 0 150 80" role="img" aria-label="Arc from 0 to 45 seconds: fast guard before 25 seconds may run to 40 grams; 25 to 35 seconds on target at 36 grams; slow guard after 35 seconds stops at 30 grams">` +
      `<g fill="none" stroke-width="9"><path d="${arc(0, DATA.tMin, 50)}" stroke="var(--wn)" stroke-opacity=".32"/><path d="${arc(DATA.tMin, DATA.tMaxBbw, 50)}" stroke="var(--ok)"/><path d="${arc(DATA.tMaxBbw, DATA.wall, 50)}" stroke="var(--dn)" stroke-opacity=".32"/></g>` +
      `<path d="${notch(DATA.tMin, 44, 57)}${notch(DATA.tMaxBbw, 44, 57)}" stroke="var(--fg)" stroke-width="1" fill="none"/>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle" font-weight="600"><text x="${b1x}" y="${+b1y + 2.4}">25 s</text><text x="${b2x}" y="${+b2y + 2.4}">35 s</text></g>` +
      `<g font-size="7" font-weight="650"><text x="${lw}" y="${+lwY + 2}" text-anchor="middle" fill="var(--wn)">to 40 g</text><text x="${rw}" y="${+rwY + 2}" text-anchor="middle" fill="var(--dn)">stop 30 g</text></g>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${lf}" y="${+lfY + 2}">Fast</text><text x="${ls}" y="${+lsY + 2}">Slow</text></g>` +
      `<text x="${cx}" y="55" text-anchor="middle" font-size="13" font-weight="700" fill="var(--fg)">36 g</text>` +
      `<text x="${cx}" y="64" text-anchor="middle" font-size="6.5" fill="var(--mu)">target · pour 25–35 s</text>` +
      `</svg></figure></div>`;
  }

  // 10 · Fill profile with the three scenarios: on target, fast-extended, slow-stopped.
  function profile() {
    const W = 260, L = 8, R = 34, T = 8, B = 76;
    const x = t => L + (W - R - L) * t / DATA.wall;
    const y = w => B - (B - T) * w / 42;
    const path = pts => pts.map((p, i) => (i ? 'L' : 'M') + x(p[0]).toFixed(1) + ' ' + y(p[1]).toFixed(1)).join(' ');
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${T}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${B - T}" fill="${color}" fill-opacity="${op}"/>`;
    const grid = w => { const gy = y(w).toFixed(1); return {line: `<path d="M${L} ${gy}H${W - R}" stroke="var(--ln)" stroke-dasharray="2 3"/>`, label: `<text x="${W - R + 4}" y="${+gy + 2.3}" font-size="6.5" fill="var(--mu)">${w} g</text>`}; };
    const g30 = grid(30), g36 = grid(36), g40 = grid(40);
    return `<figure class="grProfile"><svg viewBox="0 0 ${W} 88" role="img" aria-label="Weight over time from 0 to 45 seconds: the on-target shot reaches 36 grams in about 33 seconds; a fast shot reaches the target before 25 seconds and the guard extends it to 40 grams; a slow shot is still short after 35 seconds and the guard stops it once it reaches 30 grams">` +
      band(0, DATA.tMin, 'var(--wn)', .07) + band(DATA.tMin, DATA.tMaxBbw, 'var(--ok)', .06) + band(DATA.tMaxBbw, DATA.wall, 'var(--dn)', .07) +
      `<text x="${L + 4}" y="${T + 7}" font-size="6" font-weight="600" fill="var(--wn)">Fast guard</text>` +
      `<text x="${W - R - 4}" y="${T + 7}" font-size="6" font-weight="600" fill="var(--dn)" text-anchor="end">Slow guard</text>` +
      g40.line + g36.line + g30.line + g40.label + g36.label + g30.label +
      `<path d="M${x(DATA.tMin).toFixed(1)} ${T}V${B}M${x(DATA.tMaxBbw).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width=".7" stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      `<path d="${path([[0, 0], [6, 1.5], [18, 21], [33, 36]])}" fill="none" stroke="var(--ok)" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>` +
      `<path d="${path([[0, 0], [4, 2], [10, 16], [20, 36]])}" fill="none" stroke="var(--wn)" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/><path d="${path([[20, 36], [30, 38.5], [40, 40]])}" fill="none" stroke="var(--wn)" stroke-width="1.2" stroke-dasharray="3 2.4" stroke-linecap="round"/>` +
      `<path d="${path([[0, 0], [8, 1], [20, 9], [35, 28]])}" fill="none" stroke="var(--dn)" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/><path d="${path([[35, 28], [40, 29.5], [45, 30]])}" fill="none" stroke="var(--dn)" stroke-width="1.2" stroke-dasharray="3 2.4" stroke-linecap="round"/>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${L}" y="85">0</text><text x="${x(DATA.tMin).toFixed(1)}" y="85">25 s</text><text x="${x(DATA.tMaxBbw).toFixed(1)}" y="85">35 s</text><text x="${x(DATA.wall).toFixed(1)}" y="85">45 s</text></g>` +
      `</svg><figcaption class="grProfileLegend"><span class="ok">on target · 36 g</span><span class="wn">fast · extends to 40 g</span><span class="dn">slow · stops at 30 g</span></figcaption></figure>`;
  }

  const PROPOSALS = [
    {name: 'Línea de receta', star: true,
      desc: 'Una línea con el objetivo y la ventana, y una segunda corta con lo que hace cada guardia. Cada número va nombrado y el texto ocupa una fracción del espacio actual; sin gráfica y sin color adicional.',
      html: () => `<div class="grLine"><p class="grMain"><b>${g(DATA.target)}</b> in <b>${DATA.tMin}–${DATA.tMaxBbw} s</b></p><p class="grSub">Fast guard: target before ${DATA.tMin} s → up to <b>${g(DATA.ceil)}</b> · Slow guard: past ${DATA.tMaxBbw} s → stops at <b>${g(DATA.floor)}</b></p></div>`},
    {name: 'Frase guía',
      desc: 'El comportamiento contado en lenguaje natural: qué es un buen tiro y qué hace cada guardia con su tiempo y su peso. Texto puro, sin gráfica ni códigos de color, con los nombres de los guardias en el tono de aviso del tema.',
      html: () => `<p class="grProse">A good shot pours <b>${g(DATA.target)}</b> in <b>${DATA.tMin}–${DATA.tMaxBbw} s</b>. Reach the target before <b>${DATA.tMin} s</b> and the <b class="grWn">Fast guard</b> may let it run to <b>${g(DATA.ceil)}</b>; still short after <b>${DATA.tMaxBbw} s</b> and the <b class="grDn">Slow guard</b> stops it at <b>${g(DATA.floor)}</b>.</p>`},
    {name: 'Filas por guardia',
      desc: 'Una fila por guardia, cada una con su punto de color del tema: el tiempo que vigila y el peso que aplica, juntos en la misma línea, así la relación queda dentro de cada fila.',
      html: () => `<div class="grRows"><p class="grHead">Target <b>${g(DATA.target)}</b> · window <b>${DATA.tMin}–${DATA.tMaxBbw} s</b></p><div class="grGuard"><i class="grDot wn" aria-hidden="true"></i><div><b>Fast guard</b><span>Target before <b>${DATA.tMin} s</b> → may run to <b>${g(DATA.ceil)}</b></span></div></div><div class="grGuard"><i class="grDot dn" aria-hidden="true"></i><div><b>Slow guard</b><span>Past <b>${DATA.tMaxBbw} s</b> → pours to at least <b>${g(DATA.floor)}</b></span></div></div></div>`},
    {name: 'Tarjeta de valores',
      desc: 'Cuatro celdas tipo insignia con los fondos suaves que la app ya usa para métricas: objetivo, ventana de llenado y cada guardia con su par tiempo → peso.',
      html: () => `<div class="grStats"><div><small>Target</small><b>${g(DATA.target)}</b></div><div><small>Brew window</small><b>${DATA.tMin}–${DATA.tMaxBbw} s</b></div><div class="wn"><small>Fast guard</small><b>&lt;${DATA.tMin} s · to ${g(DATA.ceil)}</b></div><div class="dn"><small>Slow guard</small><b>&gt;${DATA.tMaxBbw} s · stop ${g(DATA.floor)}</b></div></div>`},
    {name: 'Chips de rango',
      desc: 'Pastillas compactas con ícono: dos para la receta y una por guardia con su par de límites. En escritorio cabe en una sola línea; en teléfono se envuelve en dos.',
      html: () => `<div class="grChips"><span class="grChip acc">${CLOCK}${DATA.tMin}–${DATA.tMaxBbw} s</span><span class="grChip acc">${TARGET}${g(DATA.target)}</span><span class="grChip wn">${BOLT}&lt;${DATA.tMin} s → ${g(DATA.ceil)}</span><span class="grChip dn">${HOURGLASS}&gt;${DATA.tMaxBbw} s → ${g(DATA.floor)}</span></div>`},
    {name: 'Barra única, doble lectura', star: true,
      desc: 'Una sola barra de tres zonas con los tonos suaves del tema: dentro de cada zona el peso que aplica, y debajo los ticks de tiempo alineados con las fronteras. Relaciona tiempo y peso en una sola lectura y ocupa menos de la mitad que la zona actual.',
      html: () => `<div class="grBar" role="img" aria-label="Fast guard before ${DATA.tMin} seconds may run to 40 grams; ${DATA.tMin} to ${DATA.tMaxBbw} seconds on target at 36 grams; slow guard after ${DATA.tMaxBbw} seconds stops at 30 grams"><div class="wn grSeg"><b>to ${g(DATA.ceil)}</b><span>Fast</span></div><div class="ok grSeg"><b>${g(DATA.target)}</b><span>On target</span></div><div class="dn grSeg"><b>stop ${g(DATA.floor)}</b><span>Slow</span></div></div><div class="ruleChartTicks grBarTicks"><span class="ruleTick" style="left:0">0 s</span><span class="ruleTick" style="left:${pct(DATA.tMin, DATA.wall).toFixed(2)}%">25 s</span><span class="ruleTick" style="left:${pct(DATA.tMaxBbw, DATA.wall).toFixed(2)}%">35 s</span><span class="ruleTick" style="left:100%">45 s</span></div>`},
    {name: 'Tarjetas de umbral',
      desc: 'Dos mini-tarjetas, una por guardia, con el par de números en grande y el verbo que explica cada uno: termina antes de 25 s y puede llegar a 40 g; se pasa de 35 s y se detiene al llegar a 30 g.',
      html: () => `<div class="grPairs"><div class="grPair"><header class="fast"><i aria-hidden="true"></i>Fast guard</header><div class="grNums"><div><small>Ends before</small><b>${DATA.tMin} s</b></div><div><small>May reach</small><b>${g(DATA.ceil)}</b></div></div></div><div class="grPair"><header class="slow"><i aria-hidden="true"></i>Slow guard</header><div class="grNums"><div><small>Runs past</small><b>${DATA.tMaxBbw} s</b></div><div><small>Stops at</small><b>${g(DATA.floor)}</b></div></div></div></div><p class="grPairsHint">On target: <b>${g(DATA.target)}</b> within <b>${DATA.tMin}–${DATA.tMaxBbw} s</b></p>`},
    {name: 'Matriz guardia × magnitud',
      desc: 'Tabla mínima con una fila por guardia y una columna por magnitud, con el objetivo como pie de tabla. La forma más densa de escanear los cuatro números.',
      html: () => `<table class="grTable"><thead><tr><th scope="col">Guard</th><th scope="col">Time</th><th scope="col">Weight</th></tr></thead><tbody><tr><th scope="row"><i class="fastI" aria-hidden="true"></i>Fast</th><td>before <b>${DATA.tMin} s</b></td><td>may run to <b>${g(DATA.ceil)}</b></td></tr><tr><th scope="row"><i class="slowI" aria-hidden="true"></i>Slow</th><td>after <b>${DATA.tMaxBbw} s</b></td><td>stops at <b>${g(DATA.floor)}</b></td></tr></tbody><caption>On target: <b>${g(DATA.target)}</b> in <b>${DATA.tMin}–${DATA.tMaxBbw} s</b></caption></table>`},
    {name: 'Medidor en arco',
      desc: 'Un arco de 0 a 45 s con la ventana buena resaltada en el verde del tema y las fronteras 25/35 s marcadas; a los lados, el peso consecuencia de cada guardia y, al centro, el objetivo. La gráfica «rica» en poco espacio.',
      html: gauge},
    {name: 'Perfil tiempo → peso', star: true,
      desc: 'La curva de llenado con los tres escenarios: el tiro bueno alcanza 36 g a tiempo; el rápido llega antes de 25 s y el guardia lo extiende hasta 40 g; el lento sigue corto pasado 35 s y se detiene al llegar a 30 g. Ejes con unidades y leyenda; la relación tiempo–peso es explícita.',
      html: profile},
  ];

  const frame = (phone, zone) =>
    `<div class="frame${phone ? ' phone' : ''}"><p class="frameLabel">${phone ? 'Teléfono · 360 px' : 'Escritorio'}</p>` +
    `<fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>` +
    `<div class="presetCards"><div class="presetCard active selected"><strong class="presetCardTitle">${DATA.preset}</strong><small class="presetCardMeta">${g(DATA.target)} · ${DATA.tMin}–${DATA.tMaxBbw} s</small></div></div>` +
    `<div class="grZone">${zone}</div></fieldset></div>`;

  const host = document.getElementById('options');
  const sections = [{name: 'Zona actual (referencia)', desc: 'La zona tal como se ve hoy en el Home: dos pistas con etiquetas y ticks, cuatro filas de alto, números sin explicación y colores fijos ajenos al tema. Se conserva como referencia para comparar.', zone: baselineZone()}];
  PROPOSALS.forEach((p, i) => sections.push({...p, zone: p.html()}));
  sections.forEach((s, i) => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${i === 0 ? '00' : String(i).padStart(2, '0')} · ${s.name}${s.star ? ' <span class="star">Recomendada</span>' : ''}</h2><p>${s.desc}</p></div>${frame(false, s.zone)}${frame(true, s.zone)}`;
    host.append(section);
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
