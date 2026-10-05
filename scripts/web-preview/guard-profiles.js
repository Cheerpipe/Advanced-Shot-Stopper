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
  const icon = inner => `<svg viewBox="0 0 24 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">${inner}</g></svg>`;
  const CLOCK = icon('<circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/>');
  const TARGET = icon('<circle cx="12" cy="12" r="8.5"/><circle cx="12" cy="12" r="4"/><circle cx="12" cy="12" r="1" fill="currentColor" stroke="none"/>');
  const BOLT = icon('<path d="M13 3 5 13.5h5L11 21l8-10.5h-5z"/>');
  const HOURGLASS = icon('<path d="M8 3h8v3.2L12 11 8 6.2V3zM8 21h8v-3.2L12 13l-4 4.8V21z"/>');
  const n1 = v => +v.toFixed(1);

  // Shared frame for the drawn variants: plot, time gates and axis labels.
  const W = 300, L = 34, R = 12, T = 8, B = 24, VBW = 324, VBH = 104;
  const xT = t => L + (W - L - R) * t / TIME.wall;
  const yW = (p, w) => B - (B - T) * Math.max(0, w) / p.ceil;
  const hLine = (p, w, color, width, op) =>
    `<path d="M${L} ${yW(p, w).toFixed(1)}H${W - R}" stroke="${color}" stroke-width="${width}"${op ? ` stroke-opacity="${op}"` : ''}/>`;
  const gate = t => `<path d="M${xT(t).toFixed(1)} ${T}V${B}" stroke="var(--mu)" stroke-width=".7" stroke-dasharray="2 2.6" opacity=".8"/>`;
  const dot = (p, t, w, kind) => `<circle cx="${xT(t).toFixed(1)}" cy="${yW(p, w).toFixed(1)}" r="3.2" fill="var(--sf)" stroke="var(--${kind})" stroke-width="1.7"/>`;
  const curveTo = (p, tEnd, width = 1.8) => {
    const full = [[0, 0], [10, n1(p.target * .04)], [20, n1(p.target * .6)], [28, p.target],
      [36, n1(p.target + (p.ceil - p.target) * .55)], [44, p.ceil]];
    const pts = full.filter(q => q[0] <= tEnd);
    return `<path d="${pts.map((q, i) => (i ? 'L' : 'M') + xT(q[0]).toFixed(1) + ' ' + yW(p, q[1]).toFixed(1)).join(' ')}" fill="none" stroke="var(--ok)" stroke-width="${width}" stroke-linecap="round" stroke-linejoin="round"/>`;
  };
  const curve = p => curveTo(p, TIME.tMaxBbw);
  const dash = (p, pts, kind) =>
    `<path d="${pts.map((q, i) => (i ? 'L' : 'M') + xT(q[0]).toFixed(1) + ' ' + yW(p, q[1]).toFixed(1)).join(' ')}" fill="none" stroke="var(--${kind})" stroke-width="1.3" stroke-dasharray="3 2.4" stroke-linecap="round"/>`;
  const spread = (items, min) => { const s = [...items].sort((a, b) => a.y - b.y);
    for (let i = 1; i < s.length; i++) if (s[i].y - s[i - 1].y < min) s[i].y = s[i - 1].y + min;
    return s; };
  const sideLabels = (p, items, min, x, anchor, size) => spread(items, min).map(it =>
    `<text x="${x}" y="${(it.y + 2.3).toFixed(1)}" font-size="${size}" font-weight="650" fill="var(--${it.kind})"${anchor === 'end' ? ' text-anchor="end"' : ''}>${esc(it.label)}</text>`).join('');
  const rightLabels = (p, items, min = 7) => sideLabels(p, items, min, W - R + 3.5, 'start', 6.6);
  const leftLabels = (p, items, min = 7) => sideLabels(p, items, min, L - 4, 'end', 6.4);

  const frame = (p, inner, tLabelY = B + 10.4) =>
    `<rect x="${L}" y="${T}" width="${W - L - R}" height="${B - T}" fill="var(--in)"/>` +
    `<rect x="${L}" y="${T}" width="${W - L - R}" height="${B - T}" fill="none" stroke="var(--ln)" stroke-width=".6"/>` + inner +
    `<path d="M${L} ${B + .5}H${W - R}" stroke="var(--ln)" stroke-width=".6"/>` +
    `<path d="M${xT(TIME.tMin).toFixed(1)} ${B}v3.4M${xT(TIME.tMaxBbw).toFixed(1)} ${B}v3.4" stroke="var(--mu)" stroke-width=".8"/>` +
    `<g font-size="6.4" fill="var(--mu)" text-anchor="middle"><text x="${xT(TIME.tMin).toFixed(1)}" y="${tLabelY.toFixed(1)}">28 s</text><text x="${xT(TIME.tMaxBbw).toFixed(1)}" y="${tLabelY.toFixed(1)}">44 s</text></g>` +
    `<text x="${W - R}" y="${tLabelY.toFixed(1)}" font-size="6.4" fill="var(--mu)" text-anchor="end">50 s</text>`;

  const refLegend = p => `<figcaption class="gpRefLegend"><span class="ok">on target · ${esc(g(p.target))}</span><span class="wn">fast · extends to ${esc(g(p.ceil))}</span><span class="dn">slow · stops at ${esc(g(p.floor))}</span></figcaption>`;
  const fig = (p, inner, tail = '', legend = false, tLabelY) =>
    `<figure class="gpFig"><svg viewBox="0 0 ${VBW} ${VBH}" role="img" aria-label="${esc(p.name)}: pour target ${esc(g(p.target))} within ${TIME.tMin} to ${TIME.tMaxBbw} seconds; Fast guard extends a shot that reached ${esc(g(p.target))} by ${TIME.tMin} seconds up to ${esc(g(p.ceil))}; Slow guard stops a shot still short of ${esc(g(p.floor))} after ${TIME.tMaxBbw} seconds">${frame(p, inner, tLabelY)}</svg>${tail}${legend ? refLegend(p) : ''}</figure>`;

  // 00 · The current proposal-10 profile, kept as the reference.
  function refProfile(p) {
    const RW = 260, RL = 8, RR = 34, RT = 8, RB = 76;
    const x = t => RL + (RW - RR - RL) * t / TIME.wall;
    const y = w => RB - (RB - RT) * w / p.ceil;
    const path = pts => pts.map((q, i) => (i ? 'L' : 'M') + x(q[0]).toFixed(1) + ' ' + y(q[1]).toFixed(1)).join(' ');
    const band = (t0, t1, color, op) => `<rect x="${x(t0).toFixed(1)}" y="${RT}" width="${(x(t1) - x(t0)).toFixed(1)}" height="${RB - RT}" fill="${color}" fill-opacity="${op}"/>`;
    const grid = w => { const gy = y(w).toFixed(1); return {line: `<path d="M${RL} ${gy}H${RW - RR}" stroke="var(--ln)" stroke-dasharray="2 3"/>`, label: `<text x="${RW - RR + 4}" y="${+gy + 2.3}" font-size="6.5" fill="var(--mu)">${esc(g(w))}</text>`}; };
    const gFloor = grid(p.floor), gTarget = grid(p.target), gCeil = grid(p.ceil);
    const slowHold = n1(p.floor * .93), slowNear = n1(p.floor * .97), fastMid = n1(p.target + (p.ceil - p.target) / 2);
    return `<figure class="gpFig"><svg viewBox="0 0 ${RW} 88" role="img" aria-label="Reference: the previous profile version">` +
      band(0, TIME.tMin, 'var(--wn)', .07) + band(TIME.tMin, TIME.tMaxBbw, 'var(--ok)', .06) + band(TIME.tMaxBbw, TIME.wall, 'var(--dn)', .07) +
      `<text x="${RL + 4}" y="${RT + 7}" font-size="6" font-weight="600" fill="var(--wn)">Fast guard</text>` +
      `<text x="${RW - RR - 4}" y="${RT + 7}" font-size="6" font-weight="600" fill="var(--dn)" text-anchor="end">Slow guard</text>` +
      gCeil.line + gTarget.line + gFloor.line + gCeil.label + gTarget.label + gFloor.label +
      `<path d="M${x(TIME.tMin).toFixed(1)} ${RT}V${RB}M${x(TIME.tMaxBbw).toFixed(1)} ${RT}V${RB}" stroke="var(--mu)" stroke-width=".7" stroke-dasharray="1.6 2.6" opacity=".7"/>` +
      `<path d="${path([[0, 0], [10, 2], [26, n1(.55 * p.target)], [42, p.target]])}" fill="none" stroke="var(--ok)" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"/>` +
      `<path d="${path([[0, 0], [5, 3], [13, n1(.5 * p.target)], [22, p.target]])}" fill="none" stroke="var(--wn)" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/><path d="${path([[22, p.target], [32, fastMid], [40, p.ceil]])}" fill="none" stroke="var(--wn)" stroke-width="1.2" stroke-dasharray="3 2.4" stroke-linecap="round"/>` +
      `<path d="${path([[0, 0], [12, 1], [28, n1(.45 * p.target)], [44, slowHold]])}" fill="none" stroke="var(--dn)" stroke-width="1.2" stroke-linecap="round" stroke-linejoin="round"/><path d="${path([[44, slowHold], [47, slowNear], [50, p.floor]])}" fill="none" stroke="var(--dn)" stroke-width="1.2" stroke-dasharray="3 2.4" stroke-linecap="round"/>` +
      `<g font-size="6.5" fill="var(--mu)" text-anchor="middle"><text x="${RL}" y="85">0</text><text x="${x(TIME.tMin).toFixed(1)}" y="85">28 s</text><text x="${x(TIME.tMaxBbw).toFixed(1)}" y="85">44 s</text><text x="${x(TIME.wall).toFixed(1)}" y="85">50 s</text></g>` +
      `</svg><figcaption class="gpRefLegend"><span class="ok">on target · ${esc(g(p.target))}</span><span class="wn">fast · extends to ${esc(g(p.ceil))}</span><span class="dn">slow · stops at ${esc(g(p.floor))}</span></figcaption></figure>`;
  }

  const VARIANTS = [
    {name: 'Puntos de decisión', star: true,
      desc: 'Un anillo vacío marca el cruce exacto en cada compuerta —28 s × 36 g y 44 s × 34 g—, con el par tiempo×peso escrito junto al anillo y la consecuencia resumida debajo en chips HTML. Solo quedan dos líneas horizontales (objetivo y piso); el techo vive en el texto, así no hay tres líneas pegadas.',
      html: p => { const fy = yW(p, p.target), sy = yW(p, p.floor);
        return fig(p, hLine(p, p.target, 'var(--ok)', 1.1) + hLine(p, p.floor, 'var(--dn)', 1, .8) +
          gate(TIME.tMin) + gate(TIME.tMaxBbw) + curve(p) + dot(p, TIME.tMin, p.target, 'wn') + dot(p, TIME.tMaxBbw, p.floor, 'dn') +
          `<text x="${(xT(TIME.tMin) + 8).toFixed(1)}" y="${(fy + 7.4).toFixed(1)}" font-size="6.8" font-weight="700" fill="var(--fg)" text-anchor="start">28 s × ${esc(g(p.target))}</text>` +
          `<text x="${(xT(TIME.tMaxBbw) + 6).toFixed(1)}" y="${(sy + 7.8).toFixed(1)}" font-size="6.8" font-weight="700" fill="var(--fg)" text-anchor="start">44 s × ${esc(g(p.floor))}</text>`,
          `<div class="gpRuleRow"><span class="gpRule wn">${BOLT}At 28 s × ${esc(g(p.target))} → extends to ${esc(g(p.ceil))}</span><span class="gpRule dn">${HOURGLASS}At 44 s short of ${esc(g(p.floor))} → pours to it</span></div>`); } },
    {name: 'Umbrales en las compuertas', star: true,
      desc: 'Cada umbral viaja anclado a su compuerta en una píldora de dos líneas —quién decide y qué par tiempo×peso dispara— con una guía vertical punteada hasta la curva. Los tres pesos quedan a la derecha, separados automáticamente para que nunca se toquen.',
      html: p => { const chip = (t, kind, l1, l2, anchor, stemW) => { const wpx = Math.max(l1.length, l2.length) * 4.15 + 10, h = 12.6;
          let x0 = xT(t) - wpx / 2; if (anchor === 'start') x0 = xT(t) - 3; if (anchor === 'end') x0 = xT(t) - wpx + 3;
          x0 = Math.max(L + 1, Math.min(x0, W - R - wpx - 1)); const cx = x0 + wpx / 2;
          return `<path d="M${xT(t).toFixed(1)} ${(T + h + 1).toFixed(1)}V${(yW(p, stemW) - 5.5).toFixed(1)}" stroke="var(--mu)" stroke-width=".6" stroke-dasharray="2 2.4"/>` +
            `<g font-size="6.8" text-anchor="middle"><rect x="${x0.toFixed(1)}" y="${T}" width="${wpx.toFixed(1)}" height="${h}" rx="${h / 2}" fill="var(--${kind === 'wn' ? 'mw' : 'me'})"/><text x="${cx.toFixed(1)}" y="${T + 5.5}" fill="var(--${kind})" font-weight="650">${esc(l1)}</text><text x="${cx.toFixed(1)}" y="${T + 11.4}" fill="var(--${kind})" font-weight="700">${esc(l2)}</text></g>`; };
        return fig(p, curve(p) + hLine(p, p.target, 'var(--ok)', 1) + hLine(p, p.floor, 'var(--dn)', 1, .75) + hLine(p, p.ceil, 'var(--wn)', .8, .7) +
          gate(TIME.tMin) + gate(TIME.tMaxBbw) +
          chip(TIME.tMin, 'wn', 'Fast', `${p.target} → ${g(p.ceil)}`, 'start', p.target) +
          chip(TIME.tMaxBbw, 'dn', 'Slow', `→ ${g(p.floor)}`, 'end', p.floor) +
          rightLabels(p, [{y: yW(p, p.ceil), kind: 'wn', label: g(p.ceil)}, {y: yW(p, p.target), kind: 'ok', label: g(p.target)}, {y: yW(p, p.floor), kind: 'dn', label: g(p.floor)}], 6.8)); } },
    {name: 'Consecuencias en texto',
      desc: 'El dibujo se reduce a la curva, las dos compuertas y una sola referencia (el objetivo). Toda consecuencia vive en chips HTML debajo: nítidos a cualquier zoom y sin ninguna posibilidad de solape dentro del gráfico.',
      html: p => fig(p, hLine(p, p.target, 'var(--ok)', 1.1) + gate(TIME.tMin) + gate(TIME.tMaxBbw) + curve(p) +
        dot(p, TIME.tMin, p.target, 'wn') + dot(p, TIME.tMaxBbw, p.floor, 'dn'),
        `<div class="gpRuleRow"><span class="gpRule acc">${CLOCK}Pour 28–44 s to ${esc(g(p.target))}</span><span class="gpRule wn">${BOLT}28 s × ${esc(g(p.target))} → to ${esc(g(p.ceil))}</span><span class="gpRule dn">${HOURGLASS}44 s → stop at ${esc(g(p.floor))}</span></div>`) },
    {name: 'Puntos + colas de resultado', star: true,
      desc: 'La curva medida termina en el cruce de 28 s y desde ahí cada guardia continúa punteado: subida al techo si el peso llegó, relevo hasta el piso si a los 44 s seguía corto. Las colas muestran el cruce sin escribir nada dentro del gráfico.',
      html: p => fig(p, hLine(p, p.target, 'var(--ok)', 1) + hLine(p, p.ceil, 'var(--wn)', .8, .7) + hLine(p, p.floor, 'var(--dn)', 1, .8) +
        gate(TIME.tMin) + gate(TIME.tMaxBbw) +
        dash(p, [[28, p.target], [38, n1(p.target + (p.ceil - p.target) * .6)], [44, p.ceil]], 'wn') +
        dash(p, [[44, n1(p.floor * .96)], [47, n1(p.floor - .3)], [50, p.floor]], 'dn') +
        curveTo(p, TIME.tMin) + dot(p, TIME.tMin, p.target, 'wn') + dot(p, TIME.tMaxBbw, p.floor, 'dn') +
        rightLabels(p, [{y: yW(p, p.ceil), kind: 'wn', label: g(p.ceil)}, {y: yW(p, p.target), kind: 'ok', label: g(p.target)}, {y: yW(p, p.floor), kind: 'dn', label: g(p.floor)}], 6.8),
        `<div class="gpRuleRow"><span class="gpRule wn">${BOLT}Reached by 28 s → Fast pours to ${esc(g(p.ceil))}</span><span class="gpRule dn">${HOURGLASS}Short at 44 s → Slow stops at ${esc(g(p.floor))}</span></div>`) },
    {name: 'Escalera de pesos',
      desc: 'Sin líneas horizontales: los tres pesos cuelgan de la escala derecha como peldaos y su nombre viaja en el margen izquierdo, con separación mínima garantizada entre etiquetas. El cruce queda en los anillos; la jerarquía la dan los propios números.',
      html: p => fig(p, gate(TIME.tMin) + gate(TIME.tMaxBbw) + curve(p) +
        dot(p, TIME.tMin, p.target, 'wn') + dot(p, TIME.tMaxBbw, p.floor, 'dn') +
        [p.ceil, p.target, p.floor].map(w => `<path d="M${W - R} ${yW(p, w).toFixed(1)}h-3.6" stroke="var(--mu)" stroke-width=".8"/>`).join('') +
        rightLabels(p, [{y: yW(p, p.ceil), kind: 'wn', label: g(p.ceil)}, {y: yW(p, p.target), kind: 'fg', label: g(p.target)}, {y: yW(p, p.floor), kind: 'dn', label: g(p.floor)}], 6.6) +
        leftLabels(p, [{y: yW(p, p.ceil), kind: 'wn', label: 'ceiling'}, {y: yW(p, p.target), kind: 'fg', label: 'target'}, {y: yW(p, p.floor), kind: 'dn', label: 'floor'}], 7)) },
    {name: 'Cinta de guardias',
      desc: 'Una cinta bajo el eje nombra los tres tramos —FAST, POUR, SLOW— en su propio color, así las compuertas se leen sin una sola etiqueta vertical; los pesos quedan reducidos a dos referencias a la derecha.',
      html: p => { const y0 = B + 1.2;
        const seg = (t0, t1, kind, label) => `<rect x="${xT(t0).toFixed(1)}" y="${y0.toFixed(1)}" width="${(xT(t1) - xT(t0)).toFixed(1)}" height="8.6" fill="var(--${kind})" fill-opacity=".16"/><text x="${((xT(t0) + xT(t1)) / 2).toFixed(1)}" y="${(y0 + 6.3).toFixed(1)}" font-size="5.8" font-weight="700" letter-spacing=".06em" fill="var(--${kind})" text-anchor="middle">${label}</text>`;
        return fig(p, curve(p) + hLine(p, p.target, 'var(--ok)', 1) + hLine(p, p.floor, 'var(--dn)', 1, .8) +
          seg(0, TIME.tMin, 'wn', 'FAST') + seg(TIME.tMin, TIME.tMaxBbw, 'ok', 'POUR') + seg(TIME.tMaxBbw, TIME.wall, 'dn', 'SLOW') +
          dot(p, TIME.tMin, p.target, 'wn') + dot(p, TIME.tMaxBbw, p.floor, 'dn') +
          rightLabels(p, [{y: yW(p, p.target), kind: 'ok', label: g(p.target)}, {y: yW(p, p.floor), kind: 'dn', label: g(p.floor)}], 7.5), '', false, B + 18.8); } },
    {name: 'Reglas en chips', star: true,
      desc: 'Las tres reglas completas viven en chips HTML —ventana, Fast y Slow— con su par de números; el gráfico aporta la forma y los anillos de cruce, sin ninguna etiqueta suelta dentro: nada puede solaparse.',
      html: p => fig(p, hLine(p, p.target, 'var(--ok)', 1) + gate(TIME.tMin) + gate(TIME.tMaxBbw) + curve(p) +
        dot(p, TIME.tMin, p.target, 'wn') + dot(p, TIME.tMaxBbw, p.floor, 'dn'),
        `<div class="gpRuleRow"><span class="gpRule acc">${CLOCK}Window 28–44 s · target ${esc(g(p.target))}</span><span class="gpRule wn">${BOLT}At 28 s × ${esc(g(p.target))} → ${esc(g(p.ceil))}</span><span class="gpRule dn">${HOURGLASS}At 44 s → stop ${esc(g(p.floor))}</span></div>`) },
    {name: 'Tres pantallas de decisión',
      desc: 'La lectura se divide en tres franjas apiladas —ventana, Fast y Slow—, cada una con su mini-curva y su frase-resultado en texto grande. En teléfono cada sentencia conserva su tamaño: nada se comprime para caber.',
      html: p => { const sx = t => 20 + 256 * t / TIME.wall, sy = w => 22 - 18 * Math.max(0, w) / p.ceil;
        const pts = a => a.map((q, i) => (i ? 'L' : 'M') + sx(q[0]).toFixed(1) + ' ' + sy(q[1]).toFixed(1)).join(' ');
        const base = `<rect x="20" y="4" width="256" height="18" fill="var(--in)"/><path d="M${sx(TIME.tMin).toFixed(1)} 4V22M${sx(TIME.tMaxBbw).toFixed(1)} 4V22" stroke="var(--mu)" stroke-width=".7" stroke-dasharray="2 2"/>`;
        const strip = inner => `<svg viewBox="0 0 300 30" aria-hidden="true">${base}${inner}<path d="M20 22.4H276" stroke="var(--ln)" stroke-width=".6"/></svg>`;
        const nominal = pts([[0, 0], [10, n1(p.target * .04)], [20, n1(p.target * .6)], [28, p.target]]);
        const rise = pts([[28, p.target], [33, n1(p.target + (p.ceil - p.target) * .5)], [36, p.ceil]]);
        const relief = pts([[44, n1(p.floor * .93)], [47, n1(p.floor * .97)], [50, p.floor]]);
        const line = (w, color, width, d = '') => `<path d="M20 ${sy(w).toFixed(1)}H276" fill="none" stroke="${color}" stroke-width="${width}"${d ? ` stroke-dasharray="${d}"` : ''}/>`;
        return `<div class="gpSteps">` +
          `<div class="gpStep ok">${strip(`<path d="${nominal}" fill="none" stroke="var(--ok)" stroke-width="1.6" stroke-linecap="round"/>` + line(p.target, 'var(--ok)', 1))}<p>28–44 s · pours to <b>${esc(g(p.target))}</b></p></div>` +
          `<div class="gpStep wn">${strip(`<path d="${nominal}" fill="none" stroke="var(--ok)" stroke-width="1.4" stroke-linecap="round"/><path d="${rise}" fill="none" stroke="var(--wn)" stroke-width="1.5" stroke-dasharray="3 2.2" stroke-linecap="round"/>` + line(p.ceil, 'var(--wn)', .8, '2.4 2.4') + `<circle cx="${sx(TIME.tMin).toFixed(1)}" cy="${sy(p.target).toFixed(1)}" r="2.6" fill="var(--sf)" stroke="var(--wn)" stroke-width="1.4"/>`)}<p>28 s × <b>${esc(g(p.target))}</b> → Fast pours to <b>${esc(g(p.ceil))}</b></p></div>` +
          `<div class="gpStep dn">${strip(`<path d="${relief}" fill="none" stroke="var(--dn)" stroke-width="1.5" stroke-dasharray="3 2.2" stroke-linecap="round"/>` + line(p.floor, 'var(--dn)', 1) + `<circle cx="${sx(TIME.tMaxBbw).toFixed(1)}" cy="${sy(p.floor).toFixed(1)}" r="2.6" fill="var(--sf)" stroke="var(--dn)" stroke-width="1.4"/>`)}<p>44 s short of <b>${esc(g(p.floor))}</b> → stops at <b>${esc(g(p.floor))}</b></p></div>` +
          `</div>`; } },
    {name: 'Flechas de decisión',
      desc: 'Desde cada cruce parte una flecha hacia su consecuencia: al techo cuando el peso llegó a 36 g antes de 28 s, hacia el piso cuando a los 44 s seguía corto. El recorrido se ve, no se imagina.',
      html: p => { const fx = xT(TIME.tMin), fy = yW(p, p.target), sx2 = xT(TIME.tMaxBbw), sy2 = yW(p, p.floor);
        const aEnd = `${xT(43.4).toFixed(1)} ${(yW(p, p.ceil) + 1.6).toFixed(1)}`;
        return fig(p, hLine(p, p.target, 'var(--ok)', 1) + hLine(p, p.ceil, 'var(--wn)', .8, .7) + hLine(p, p.floor, 'var(--dn)', 1, .8) +
          gate(TIME.tMin) + gate(TIME.tMaxBbw) + curve(p) +
          dot(p, TIME.tMin, p.target, 'wn') + dot(p, TIME.tMaxBbw, p.floor, 'dn') +
          `<path d="M${(fx + 4.5).toFixed(1)} ${(fy - 3).toFixed(1)} Q ${xT(36).toFixed(1)} 4.2 ${aEnd}" fill="none" stroke="var(--wn)" stroke-width="1.3"/>` +
          `<path d="M${aEnd}l3.6 -.6m-3.6 .6l2.6 2.4" fill="none" stroke="var(--wn)" stroke-width="1.3" stroke-linecap="round"/>` +
          `<path d="M${(sx2 + 1).toFixed(1)} ${(sy2 + 2.2).toFixed(1)} Q ${(xT(47.5)).toFixed(1)} ${(sy2 + 4.4).toFixed(1)} ${(xT(49.4)).toFixed(1)} ${(sy2 + 3.2).toFixed(1)}" fill="none" stroke="var(--dn)" stroke-width="1.3"/>` +
          `<path d="M${xT(49.4).toFixed(1)} ${(sy2 + 3.2).toFixed(1)}l3.8 -.5m-3.8 .5l2.4 2.5" fill="none" stroke="var(--dn)" stroke-width="1.3" stroke-linecap="round"/>` +
          rightLabels(p, [{y: yW(p, p.ceil), kind: 'wn', label: g(p.ceil)}, {y: fy, kind: 'ok', label: g(p.target)}, {y: sy2, kind: 'dn', label: g(p.floor)}], 6.8)); } },
    {name: 'Perfil con etiquetas separadas', star: true,
      desc: 'La versión corregida de la original: 34 y 36 g ya no se tocan (separación mínima automática), cada peso lleva su nombre en el margen izquierdo, y el cruce queda escrito dentro —28 s × 36 g, 44 s × 34 g— sin pisar ninguna línea.',
      html: p => { const fy = yW(p, p.target), sy = yW(p, p.floor);
        return fig(p, hLine(p, p.target, 'var(--ok)', 1.1) + hLine(p, p.floor, 'var(--dn)', 1, .8) + hLine(p, p.ceil, 'var(--wn)', .8, .7) +
          gate(TIME.tMin) + gate(TIME.tMaxBbw) + curve(p) +
          leftLabels(p, [{y: yW(p, p.ceil), kind: 'wn', label: 'ceiling'}, {y: fy, kind: 'ok', label: 'target'}, {y: sy, kind: 'dn', label: 'floor'}], 7) +
          rightLabels(p, [{y: yW(p, p.ceil), kind: 'wn', label: g(p.ceil)}, {y: fy, kind: 'ok', label: g(p.target)}, {y: sy, kind: 'dn', label: g(p.floor)}], 7) +
          dot(p, TIME.tMin, p.target, 'wn') + dot(p, TIME.tMaxBbw, p.floor, 'dn') +
          `<text x="${(xT(TIME.tMin) - 6).toFixed(1)}" y="${(fy + 7.6).toFixed(1)}" font-size="6.6" font-weight="650" fill="var(--fg)" text-anchor="end">28 s × ${esc(g(p.target))}</text>` +
          `<text x="${(xT(TIME.tMaxBbw) - 6).toFixed(1)}" y="${(sy + 7.6).toFixed(1)}" font-size="6.6" font-weight="650" fill="var(--fg)" text-anchor="end">44 s × ${esc(g(p.floor))}</text>`,
          '', true); } },
  ];

  const frameHtml = (phone, render) =>
    `<div class="frame${phone ? ' phone' : ''}"><p class="frameLabel">${phone ? 'Teléfono · 360 px' : 'Escritorio'}</p>` +
    `<fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>` +
    `<div class="presetCards">${PRESETS.map((p, i) =>
      `<div class="presetCard${i === 0 ? ' active selected' : ''}" data-preset="${i}"><div class="presetCardTitleRow"><div class="presetCardTitle">${p.name}</div><span class="presetCardBadge">${p.badge}</span></div><div class="presetCardMeta">Target ${g(p.target)}</div></div>`).join('')}</div>` +
    `<div class="gpZone">${render(PRESETS[0])}</div></fieldset></div>`;

  const host = document.getElementById('options');
  const sections = [{name: 'Referencia · perfil actual', desc: 'El perfil tal como estaba en la propuesta 10: banda de color por tramo y tres guías punteadas. Es la versión cuyos números se solapaban (34/36 g) y cuyas líneas iban demasiado juntas.', render: refProfile}];
  VARIANTS.forEach(v => sections.push({...v, render: v.html}));
  const zoneRenders = new Map();
  sections.forEach((s, i) => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${String(i).padStart(2, '0')} · ${s.name}${s.star ? ' <span class="star">Recomendada</span>' : ''}</h2><p>${s.desc}</p></div>${frameHtml(false, s.render)}${frameHtml(true, s.render)}`;
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
