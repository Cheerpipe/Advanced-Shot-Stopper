'use strict';

(() => {
  // Escenarios de demostración con el vocabulario real de la app.
  const READY_SCALE = {status: 'Connected', preferred: 'Acaia Lunar', weight: '0.0 g', timer: '0:00.0'};
  const READY_CUP = {state: 'Present', weight: '287.5 g', tare: 'Tared'};
  const SCENARIOS = {
    ready: {machine: {state: 'Idle', brew: 'Ready'}, scale: {...READY_SCALE}, cup: {...READY_CUP}},
    paddle: {machine: {state: 'Assumed on', brew: 'Stopping', human: 'Waiting for paddle off', action: 'Turn off the paddle'}, scale: {...READY_SCALE}, cup: {...READY_CUP}},
    noscale: {machine: {state: 'Idle', brew: 'Ready'}, scale: {status: 'Disconnected', preferred: 'Acaia Lunar', weight: 'Unknown', timer: 'Unknown'}, cup: {...READY_CUP}},
    nocup: {machine: {state: 'Idle', brew: 'Ready'}, scale: {...READY_SCALE, waiting: true}, cup: {state: 'Absent', weight: 'Unknown', tare: 'Taring — waiting for zero'}},
  };

  // Reglas del semáforo: Machine solo en verde con Idle + Brew Ready; Scale
  // verde conectada, naranja intenso desconectada y ámbar esperando; Cup verde
  // con taza presente o tara lista para la taza.
  const lampsOf = sc => ({
    machine: sc.machine.state === 'Idle' && sc.machine.brew === 'Ready'
      ? {tone: 'ok', text: 'Ready'}
      : {tone: 'warn', text: sc.machine.human || sc.machine.brew},
    scale: sc.scale.status !== 'Connected'
      ? {tone: 'hot', text: sc.scale.status}
      : sc.scale.waiting ? {tone: 'warn', text: 'Waiting to tare'} : {tone: 'ok', text: 'Connected'},
    cup: (sc.cup.state === 'Present' || sc.cup.tare === 'Ready for a cup')
      ? {tone: 'ok', text: 'Present'}
      : {tone: 'warn', text: 'Waiting for cup'},
  });
  const overallOf = l => {
    for (const tone of ['hot', 'warn']) for (const g of ['machine', 'scale', 'cup'])
      if (l[g].tone === tone) return {tone, text: l[g].text};
    return {tone: 'ok', text: 'Ready'};
  };

  const chev = '<svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg>';
  const lamp = (tone, cls = '') => `<i class="lamp ${cls} lt-${tone}"></i>`;
  const stateText = l => `<span class="tState ${l.tone}">${l.text}</span>`;
  const kv = (label, value) => `<div class="kv"><span>${label}</span><b>${value}</b></div>`;
  const bodiesOf = sc => ({
    machine: kv('Machine', sc.machine.state) + kv('Brew', sc.machine.brew),
    scale: kv('Status', sc.scale.status) + kv('Preferred', sc.scale.preferred) + kv('Weight', sc.scale.weight) + kv('Timer', sc.scale.timer),
    cup: kv('Status', sc.cup.state) + kv('Weight', sc.cup.weight) + kv('Automatic tare', sc.cup.tare),
  });
  const row = (name, l, body, {flat = false, stack = false, act = ''} = {}) => {
    const head = stack
      ? `<span class="tCol"><span class="devName">${name}</span>${stateText(l)}${act ? `<span class="tAct">${act}</span>` : ''}</span>`
      : `<span class="devName">${name}</span>${stateText(l)}`;
    return `<details class="tRow"><summary>${lamp(l.tone, flat ? 'flat' : '')}${head}${chev}</summary><div class="rows">${body}</div></details>`;
  };

  const VERSIONS = [
    {name: 'Semáforo base',
      desc: 'La esencia: cada fila abre con su luz —verde listo, ámbar atento, naranja intenso para la balanza caída— y el estado en palabras; al desplegar quedan las lecturas de siempre. Cambia el escenario de arriba para ver cómo reaccionan.',
      html: sc => {const l = lampsOf(sc), b = bodiesOf(sc); return `<section class="panelCard">${row('Machine', l.machine, b.machine)}${row('Scale', l.scale, b.scale)}${row('Cup', l.cup, b.cup)}</section>`;}},
    {name: 'Con luz general',
      desc: 'Añade un semáforo de resumen sobre las filas: una sola luz dice si todo está listo y, si no lo está, qué equipo mirar primero. Es la lectura a distancia; las filas conservan el detalle desplegable.',
      html: sc => {const l = lampsOf(sc), o = overallOf(l), b = bodiesOf(sc); return `<section class="panelCard"><div class="tOverall ${o.tone}"><i class="lamp big"></i>${o.text}</div>${row('Machine', l.machine, b.machine)}${row('Scale', l.scale, b.scale)}${row('Cup', l.cup, b.cup)}</section>`;}},
    {name: 'Semáforo en riel',
      desc: 'Las tres luces viven en un riel vertical a la izquierda, como un semáforo de verdad: de arriba abajo, Machine, Scale y Cup, con la línea que las encadena. La fila se despliega igual que en la base.',
      html: sc => {const l = lampsOf(sc), b = bodiesOf(sc);
        return `<section class="panelCard tl">${row('Machine', l.machine, b.machine, {flat: true})}${row('Scale', l.scale, b.scale, {flat: true})}${row('Cup', l.cup, b.cup, {flat: true})}</section>`;}},
    {name: 'Con acción sugerida',
      desc: 'Cuando una luz no está en verde, la fila dice qué hacer —«Turn off the paddle», «Connect the scale», «Place the cup»—; en verde no añade ruido. El semáforo no solo informa: guía.',
      html: sc => {const l = lampsOf(sc), b = bodiesOf(sc);
        const act = {machine: l.machine.tone === 'ok' ? '' : sc.machine.action || 'Check the machine', scale: l.scale.tone === 'ok' ? '' : 'Connect the scale', cup: l.cup.tone === 'ok' ? '' : 'Place the cup'};
        return `<section class="panelCard">${row('Machine', l.machine, b.machine, {stack: true, act: act.machine})}${row('Scale', l.scale, b.scale, {stack: true, act: act.scale})}${row('Cup', l.cup, b.cup, {stack: true, act: act.cup})}</section>`;}},
    {name: 'Píldora plegable',
      desc: 'La versión mínima: una píldora con las tres luces y el veredicto del semáforo. Toca una luz para desplegar solo ese grupo; plegada ocupa una sola línea de Home.',
      html: sc => {const l = lampsOf(sc), o = overallOf(l), b = bodiesOf(sc);
        const pillBtn = g => `<button type="button" class="pillBtn" data-g="${g}">${lamp(l[g].tone)}${g[0].toUpperCase() + g.slice(1)}</button>`;
        const pillRow = g => `<details class="tRow" data-det="${g}"><summary><span class="devName">${g[0].toUpperCase() + g.slice(1)}</span>${stateText(l[g])}${chev}</summary><div class="rows">${b[g]}</div></details>`;
        return `<section class="panelCard"><div class="pillBar">${pillBtn('machine')}${pillBtn('scale')}${pillBtn('cup')}<span class="pillSum tState ${o.tone}">${o.text}</span></div><div class="pillBody">${pillRow('machine')}${pillRow('scale')}${pillRow('cup')}</div></section>`;},
      wire: root => root.querySelectorAll('.pillBtn').forEach(button => button.addEventListener('click', () => {
        const det = root.querySelector(`[data-det="${button.dataset.g}"]`);
        if (det) det.open = !det.open;
      }))},
  ];

  const frame = (phone, inner) => `<div class="frame${phone ? ' phone' : ''}"><p class="cap">${phone ? 'Móvil · 360 px' : 'Escritorio · 720 px'}</p><div class="mock">${inner}</div></div>`;
  const host = document.getElementById('options');
  const sections = VERSIONS.map((version, index) => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${String(index + 1).padStart(2, '0')} · ${version.name}</h2><p>${version.desc}</p></div>`;
    host.append(section);
    return section;
  });
  const render = scenarioKey => {
    const sc = SCENARIOS[scenarioKey];
    VERSIONS.forEach((version, index) => {
      const section = sections[index];
      section.querySelectorAll('.frame').forEach(f => f.remove());
      section.insertAdjacentHTML('beforeend', frame(false, version.html(sc)) + frame(true, version.html(sc)));
      if (version.wire) version.wire(section);
    });
  };
  render('ready');

  const chips = [...document.querySelectorAll('.scenBtn')];
  chips.forEach(chip => chip.addEventListener('click', () => {
    chips.forEach(c => {const on = c === chip; c.classList.toggle('on', on);});
    render(chip.dataset.s);
  }));

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
