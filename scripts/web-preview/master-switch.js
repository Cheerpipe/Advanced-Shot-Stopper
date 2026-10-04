'use strict';

(() => {
  const button = document.getElementById('themeToggle');
  if (button) {
    const states = [
      {cls: '', label: 'Tema: auto'},
      {cls: 'theme-light', label: 'Tema: claro'},
      {cls: 'theme-dark', label: 'Tema: oscuro'},
    ];
    let index = 0;
    button.addEventListener('click', () => {
      index = (index + 1) % states.length;
      const state = states[index];
      document.documentElement.classList.remove('theme-light', 'theme-dark');
      if (state.cls) document.documentElement.classList.add(state.cls);
      button.textContent = state.label;
    });
  }
  // Cada demo tiene un solo maestro (.btItem): tocarlo alterna el modo, cambia
  // las palabras de estado (data-on/data-off) y atenúa las protecciones de contexto.
  document.querySelectorAll('.demo').forEach(demo => {
    const master = demo.querySelector('.btItem');
    if (!master) return;
    const apply = on => {
      master.classList.toggle('btOn', on);
      master.setAttribute('aria-pressed', String(on));
      demo.classList.toggle('isOff', !on);
      demo.querySelectorAll('[data-on]').forEach(el => {
        el.textContent = on ? el.dataset.on : el.dataset.off;
      });
    };
    master.addEventListener('click', () => apply(!master.classList.contains('btOn')));
    apply(master.classList.contains('btOn'));
  });
})();
