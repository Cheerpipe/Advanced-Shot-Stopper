'use strict';

(() => {
  const NAV_ITEMS = [
    {label: 'Inicio', icon: '<path d="m3 10 9-7 9 7v10H3zM9 20v-7h6v7"/>'},
    {label: 'Estadísticas', icon: '<path d="M3 4v16h18M6 16l5-5 4 2 6-8"/>'},
    {label: 'Historial', icon: '<path d="M3 11a9 9 0 1 1 2.6 7.4M3 4v7h7M12 7v5l3 2"/>'},
    {label: 'Ajustes', icon: '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3"/><circle cx="15" cy="17" r="3"/>'},
    {label: 'Admin', icon: '<path d="m12 3 8 3v6c0 4-4 7-8 9-4-2-8-5-8-9V6z"/><rect x="9" y="10" width="6" height="5" rx="1"/><path d="M10 10V8a2 2 0 0 1 4 0v2"/>'},
  ];
  const OPTIONS = [
    {cls: 'opt1', star: true, name: 'Vidrio esmerilado',
      desc: 'La barra deja ver el fondo desenfocado y saturado, con el filo superior iluminado como el borde de un cristal. El material es estático: toda la vida la pone la pastilla, que se desliza con un pequeño rebote hacia el destino elegido. Funciona idéntico en Safari, Chrome y Firefox.'},
    {cls: 'opt2', name: 'Especular vivo',
      desc: 'Sobre la misma base de vidrio, una banda de luz recorre el borde cada pocos segundos y cada botón enciende un brillo que sigue al puntero o al dedo. La pastilla deslizante lleva su propio brillo superior. Es la más llamativa y la que más le pide al GPU en equipos antiguos.'},
  ];

  const host = document.getElementById('options');
  for (const option of OPTIONS) {
    const section = document.createElement('section');
    section.className = `opt ${option.cls}`;
    section.innerHTML = `<div class="optHead"><h2>${option.name}${option.star ? ' <span class="star">Recomendada</span>' : ''}</h2><p>${option.desc}</p></div>` +
        `<div class="scene"><p>Fondo cualquiera de la página: la barra lo desenfoca de verdad.</p>` +
        '<div class="blob a"></div><div class="blob b"></div><div class="blob c"></div>' +
        `<nav class="glassNav" aria-label="${option.name}"><div class="glassPill"></div></nav></div>`;
    host.append(section);
    wireNav(section.querySelector('.glassNav'), option.cls === 'opt2');
  }

  function wireNav(nav, living) {
    const pill = nav.querySelector('.glassPill');
    const buttons = NAV_ITEMS.map((item, index) => {
      const button = document.createElement('button');
      button.type = 'button';
      button.title = item.label;
      button.setAttribute('aria-label', item.label);
      button.setAttribute('aria-pressed', String(index === 0));
      button.innerHTML = `<svg viewBox="0 0 24 24" aria-hidden="true">${item.icon}</svg>`;
      if (index === 0) button.classList.add('active');
      button.addEventListener('click', () => {
        for (const other of buttons) {
          other.classList.toggle('active', other === button);
          other.setAttribute('aria-pressed', String(other === button));
        }
        place(button, true);
      });
      nav.append(button);
      return button;
    });
    const place = (button, animate) => {
      if (!animate) pill.style.transition = 'none';
      pill.style.width = `${button.offsetWidth}px`;
      pill.style.translate = `${button.offsetLeft}px 0`;
      if (!animate) requestAnimationFrame(() => requestAnimationFrame(() => { pill.style.transition = ''; }));
    };
    if (living) for (const button of buttons) button.addEventListener('pointermove', event => {
      const rect = button.getBoundingClientRect();
      button.style.setProperty('--mx', `${((event.clientX - rect.left) / rect.width * 100).toFixed(1)}%`);
      button.style.setProperty('--my', `${((event.clientY - rect.top) / rect.height * 100).toFixed(1)}%`);
    });
    new ResizeObserver(() => place(buttons.find(button => button.classList.contains('active')), false)).observe(nav);
    place(buttons[0], false);
  }

  const toggle = document.getElementById('themeToggle');
  const themes = [{cls: '', label: 'Tema: auto'}, {cls: 'theme-light', label: 'Tema: claro'}, {cls: 'theme-dark', label: 'Tema: oscuro'}];
  let index = 0;
  toggle.addEventListener('click', () => {
    index = (index + 1) % themes.length;
    document.documentElement.classList.remove('theme-light', 'theme-dark');
    if (themes[index].cls) document.documentElement.classList.add(themes[index].cls);
    toggle.textContent = themes[index].label;
  });
})();
