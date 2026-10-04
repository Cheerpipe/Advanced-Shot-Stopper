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
  // Los seis ajustes son interruptores independientes: tocar una pieza alterna su estado.
  document.querySelectorAll('.demo').forEach(demo => {
    demo.addEventListener('click', event => {
      const item = event.target.closest('.btItem');
      if (!item || !demo.contains(item)) return;
      const on = item.classList.toggle('btOn');
      item.setAttribute('aria-pressed', String(on));
    });
  });
})();
