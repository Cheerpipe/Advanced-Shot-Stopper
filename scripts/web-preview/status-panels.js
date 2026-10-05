'use strict';

(() => {
  // Estado de demostración: equipo listo, taza colocada, balanza recién tarada.
  const IC = {
    machine: '<svg class="ic" viewBox="0 0 24 24" aria-hidden="true"><path d="M5 3h14v5H5z"/><path d="M7 8v6.5a5 5 0 0 0 10 0V8"/><path d="M9 21h6"/></svg>',
    scale: '<svg class="ic" viewBox="0 0 24 24" aria-hidden="true"><rect x="4" y="4" width="16" height="16" rx="3.5"/><circle cx="12" cy="10.6" r="3.1"/><path d="m12 10.6 1.7-1.7"/><path d="M8.5 17h7"/></svg>',
    cup: '<svg class="ic" viewBox="0 0 24 24" aria-hidden="true"><path d="M4 8h11v4.5a5.5 5.5 0 0 1-11 0V8z"/><path d="M15 9.5h2a2.5 2.5 0 0 1 0 5h-2.2"/><path d="M6 21h8"/></svg>',
    timer: '<svg class="ic" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12.5" r="8"/><path d="M12 8.5v4l2.6 1.6"/><path d="M9.5 2.5h5"/></svg>',
  };
  const chev = '<svg class="chev" viewBox="0 0 24 24" aria-hidden="true"><path d="m9 6 6 6-6 6"/></svg>';
  const pill = (tone, text) => `<span class="pill ${tone}">${text}</span>`;
  const kv = (label, value) => `<div class="kv"><span>${label}</span><b>${value}</b></div>`;
  const kvPill = (label, tone, text) => kv(label, pill(tone, text));
  const head = (icon, name, tone, state) => `<div class="icWrap">${icon}</div><span class="devName">${name}</span>${pill(tone, state)}`;
  const BODIES = {
    machine: kvPill('Machine', 'ok', 'Idle') + kvPill('Brew', 'ok', 'Ready'),
    scale: kvPill('Status', 'ok', 'Connected') + kv('Preferred', 'Acaia Lunar') + kv('Weight', '0.0 g') + kv('Timer', '0:00.0'),
    cup: kvPill('Status', 'ok', 'Present') + kv('Weight', '287.5 g') + kvPill('Automatic tare', 'ok', 'Tared'),
  };

  const OPTIONS = [
    {star: true, name: 'Segmentos unificados',
      desc: 'Un solo bloque divide el ancho en tres segmentos —Machine, Scale y Cup— separados por líneas finas: cada uno abre con su ícono y estado, y cierra con sus datos secundarios. Sustituye tres recuadros por uno y reduce a menos de la mitad el alto actual; en móvil los segmentos se apilan solos.',
      html: () => `<section class="p1 panelCard"><div class="seg">
        <div class="segCol"><div class="segHead">${head(IC.machine, 'Machine', 'ok', 'Idle')}</div><div class="segFoot">Brew: <b>Ready</b></div></div>
        <div class="segCol"><div class="segHead">${head(IC.scale, 'Scale', 'ok', 'Connected')}</div><div class="segFoot"><b>0.0 g</b> · 0:00.0 · Acaia Lunar</div></div>
        <div class="segCol"><div class="segHead">${head(IC.cup, 'Cup', 'ok', 'Present')}</div><div class="segFoot"><b>287.5 g</b> · Tared</div></div>
      </div></section>`},
    {star: true, name: 'Pestañas del equipo',
      desc: 'Machine, Scale y Cup pasan a pestañas dentro de un único bloque: la cabecera muestra el punto de estado de cada grupo y el contenido enseña solo lo elegido. Es el diseño que menos espacio ocupa; toca las pestañas para navegarlo.',
      html: () => `<section class="p2 panelCard">
        <div class="tabs" role="tablist" aria-label="Equipment">
          <button type="button" class="tab on" data-g="machine" role="tab" aria-selected="true"><i class="dot ok" aria-hidden="true"></i>Machine</button>
          <button type="button" class="tab" data-g="scale" role="tab" aria-selected="false"><i class="dot ok" aria-hidden="true"></i>Scale</button>
          <button type="button" class="tab" data-g="cup" role="tab" aria-selected="false"><i class="dot ok" aria-hidden="true"></i>Cup</button>
        </div><div class="tabBody" data-body>${BODIES.machine}</div></section>`,
      wire: root => {
        root.querySelectorAll('.p2').forEach(panel => {
          const tabs = [...panel.querySelectorAll('.tab')], body = panel.querySelector('[data-body]');
          tabs.forEach(tab => tab.addEventListener('click', () => {
            tabs.forEach(b => {const on = b === tab; b.classList.toggle('on', on); b.setAttribute('aria-selected', String(on));});
            body.innerHTML = BODIES[tab.dataset.g];
          }));
        });
      }},
    {name: 'Acordeón de estado',
      desc: 'Cada grupo se resume en una línea —punto de estado, nombre y valor actual— que se despliega al tocarla para revelar el resto de lecturas. Cerrado ocupa tres filas; abierto conserva toda la información, sin recuadros anidados.',
      html: () => `<section class="p3 panelCard">
        <details><summary><i class="dot ok" aria-hidden="true"></i><span class="devName">Machine</span>${pill('ok', 'Idle')}${chev}</summary><div class="rows">${BODIES.machine}</div></details>
        <details><summary><i class="dot ok" aria-hidden="true"></i><span class="devName">Scale</span>${pill('ok', 'Connected')}${chev}</summary><div class="rows">${BODIES.scale}</div></details>
        <details><summary><i class="dot ok" aria-hidden="true"></i><span class="devName">Cup</span>${pill('ok', 'Present')}${chev}</summary><div class="rows">${BODIES.cup}</div></details>
      </section>`},
    {name: 'Bento de equipo',
      desc: 'Cuadrícula asimétrica con jerarquía: Scale gana la celda alta porque su peso y temporizador son los datos vivos que se miran durante el preparado; Machine y Cup quedan como tarjetas de contexto. En móvil el bento se ordena en una columna.',
      html: () => `<section class="p4">
        <div class="bm"><div class="bHead">${head(IC.machine, 'Machine', 'ok', 'Idle')}</div><div class="rows">${kv('Brew', 'Ready')}</div></div>
        <div class="bs"><div class="bHead">${head(IC.scale, 'Scale', 'ok', 'Connected')}</div><div class="bigNum"><b>0.0</b><span>g</span></div><div class="rows">${kv('Preferred', 'Acaia Lunar')}${kv('Timer', '0:00.0')}</div></div>
        <div class="bc"><div class="bHead">${head(IC.cup, 'Cup', 'ok', 'Present')}</div><div class="rows">${kv('Weight', '287.5 g')}${kv('Automatic tare', 'Tared')}</div></div>
      </section>`},
    {name: 'Resumen en una línea',
      desc: 'La versión mínima: una píldora con los tres estados y una línea secundaria con el detalle (balanza, peso, temporizador, taza y tara). Toca la píldora para ocultar el detalle y dejar Home en una sola línea de estado.',
      html: () => `<section class="p5">
        <button type="button" class="strip" aria-expanded="true" aria-label="Equipment status, toggle detail line">
          <span class="chipS"><i class="dot ok" aria-hidden="true"></i>Machine&nbsp;<b>Idle</b></span>
          <span class="chipS">Scale&nbsp;<b>Connected</b></span>
          <span class="chipS">Cup&nbsp;<b>Present</b></span>
        </button>
        <div class="detail" data-detail><span>Brew <b>Ready</b></span><span>Acaia Lunar</span><span>Weight <b>0.0 g</b></span><span>Timer <b>0:00.0</b></span><span>Cup <b>287.5 g</b></span><span>Tare <b>Tared</b></span></div>
      </section>`,
      wire: root => root.querySelectorAll('.p5').forEach(section => {
        const strip = section.querySelector('.strip'), detail = section.querySelector('[data-detail]');
        strip.addEventListener('click', () => {
          const open = strip.getAttribute('aria-expanded') === 'true';
          strip.setAttribute('aria-expanded', String(!open));
          detail.hidden = open;
        });
      })},
    {name: 'Matriz de inventario',
      desc: 'Filas alineadas con columnas Grupo / Estado / Detalle y divisores finos: las nueve lecturas caben en tres filas escaneables a la misma altura visual. Densa y ordenada, pensada para revisar de un vistazo sin jerarquías decorativas.',
      html: () => `<section class="p6 panelCard">
        <div class="mrow mhead"><span>Group</span><span>Status</span><span>Detail</span></div>
        <div class="mrow"><div class="mgroup">${IC.machine}<span>Machine</span></div><div class="mstate"><i class="dot ok" aria-hidden="true"></i>Idle</div><div class="mdetail">Brew <b>Ready</b></div></div>
        <div class="mrow"><div class="mgroup">${IC.scale}<span>Scale</span></div><div class="mstate"><i class="dot ok" aria-hidden="true"></i>Connected</div><div class="mdetail">Acaia Lunar · <b>0.0 g</b> · <b>0:00.0</b></div></div>
        <div class="mrow"><div class="mgroup">${IC.cup}<span>Cup</span></div><div class="mstate"><i class="dot ok" aria-hidden="true"></i>Present</div><div class="mdetail"><b>287.5 g</b> · Tared</div></div>
      </section>`},
    {name: 'Peso protagonista',
      desc: 'Divide el bloque en dos: a la izquierda el peso en vivo en grande con su temporizador —lo único que cambia durante el shot— y a la derecha Machine y Cup como filas compactas. Alineado con cómo se usa Home de verdad: mirar el peso.',
      html: () => `<section class="p7 panelCard">
        <div class="p7Main"><span class="devName">Scale · live weight</span><div class="bigNum"><b>0.0</b><span>g</span></div><span class="timerChip">${IC.timer}0:00.0</span><span class="p7Sub">Acaia Lunar · Connected</span></div>
        <div class="p7Side">
          <div><div class="p7Row"><span class="icWrap">${IC.machine}</span><div><span class="devName">Machine</span><p class="p7Sub">Brew <b>Ready</b></p></div>${pill('ok', 'Idle')}</div></div>
          <div><div class="p7Row"><span class="icWrap">${IC.cup}</span><div><span class="devName">Cup</span><p class="p7Sub">Weight <b>287.5 g</b> · Tare <b>Tared</b></p></div>${pill('ok', 'Present')}</div></div>
        </div></section>`},
    {name: 'Riel de preparación',
      desc: 'Machine → Scale → Cup encadenados como una línea de dependencias: cada nodo muestra su estado y la barra inferior resume cuándo todo está listo. Cuenta si se puede preparar, no solo qué hay conectado; el detalle completo vive en el pie.',
      html: () => `<section class="p8 panelCard">
        <div class="rail">
          <div class="node"><span class="ndot">${IC.machine}</span><span class="devName">Machine</span><b>Idle</b></div>
          <i class="link" aria-hidden="true"></i>
          <div class="node"><span class="ndot">${IC.scale}</span><span class="devName">Scale</span><b>Connected</b></div>
          <i class="link" aria-hidden="true"></i>
          <div class="node"><span class="ndot">${IC.cup}</span><span class="devName">Cup</span><b>Present</b></div>
        </div>
        <div class="readyBar"><i class="dot ok" aria-hidden="true"></i>Ready</div>
        <div class="foot"><span>Brew <b>Ready</b></span><span>Acaia Lunar</span><span>Weight <b>0.0 g</b></span><span>Timer <b>0:00.0</b></span><span>Cup <b>287.5 g</b></span><span>Tare <b>Tared</b></span></div>
      </section>`},
    {name: 'Tres columnas compactas',
      desc: 'El paso más conservador: conserva la agrupación de hoy pero funde los tres recuadros en uno, elimina la doble caja y compacta las filas a pares etiqueta-valor. En pantallas anchas se reparte en columnas; en móvil queda como una lista.',
      html: () => `<section class="p9 panelCard"><div class="cols">
        <div class="col"><div class="colHead">${IC.machine}<span class="devName">Machine</span></div><div class="rows">${BODIES.machine}</div></div>
        <div class="col"><div class="colHead">${IC.scale}<span class="devName">Scale</span></div><div class="rows">${BODIES.scale}</div></div>
        <div class="col"><div class="colHead">${IC.cup}<span class="devName">Cup</span></div><div class="rows">${BODIES.cup}</div></div>
      </div></section>`},
    {name: 'Cápsula deslizante',
      desc: 'Un control segmentado con pulgar deslizante cambia entre Machine, Scale y Cup, con transición suave entre contenidos. Reutiliza el vocabulario de cápsulas que la app ya usa en su navegación; toca los segmentos para probarlo.',
      html: () => `<section class="p10 panelCard">
        <div class="capWrap"><div class="capsule" data-g="machine" role="tablist" aria-label="Equipment">
          <button type="button" class="capBtn on" data-g="machine" role="tab" aria-selected="true">Machine</button>
          <button type="button" class="capBtn" data-g="scale" role="tab" aria-selected="false">Scale</button>
          <button type="button" class="capBtn" data-g="cup" role="tab" aria-selected="false">Cup</button>
          <span class="thumb" aria-hidden="true"></span>
        </div></div><div class="capBody" data-body>${BODIES.machine}</div></section>`,
      wire: root => root.querySelectorAll('.p10').forEach(panel => {
        const capsule = panel.querySelector('.capsule'), body = panel.querySelector('[data-body]');
        const buttons = [...panel.querySelectorAll('.capBtn')];
        buttons.forEach(button => button.addEventListener('click', () => {
          buttons.forEach(b => {const on = b === button; b.classList.toggle('on', on); b.setAttribute('aria-selected', String(on));});
          capsule.dataset.g = button.dataset.g;
          body.innerHTML = BODIES[button.dataset.g];
        }));
      })},
  ];

  const frame = (phone, inner) => `<div class="frame${phone ? ' phone' : ''}"><p class="cap">${phone ? 'Móvil · 360 px' : 'Escritorio · 720 px'}</p><div class="mock">${inner}</div></div>`;
  const host = document.getElementById('options');
  OPTIONS.forEach((option, index) => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${String(index + 1).padStart(2, '0')} · ${option.name}${option.star ? ' <span class="star">Recomendada</span>' : ''}</h2><p>${option.desc}</p></div>${frame(false, option.html())}${frame(true, option.html())}`;
    if (option.wire) option.wire(section);
    host.append(section);
  });
  document.querySelectorAll('a').forEach(link => link.addEventListener('click', event => event.preventDefault()));

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
