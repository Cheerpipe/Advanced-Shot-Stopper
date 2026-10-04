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
  // En el home tocar una tarjeta aplica el preset: la demo mueve el estado activo.
  document.querySelectorAll('.demo').forEach(demo => {
    demo.addEventListener('click', event => {
      const item = event.target.closest('.pcItem');
      if (!item || !demo.contains(item)) return;
      demo.querySelectorAll('.pcItem.pcOn').forEach(el => el.classList.remove('pcOn'));
      item.classList.add('pcOn');
      const panel = demo.querySelector('.segpanel');
      if (panel) {
        panel.querySelector('[data-pname]').textContent = item.dataset.name;
        panel.querySelector('[data-ptarget]').textContent = item.dataset.target;
        panel.querySelector('[data-pbadge]').textContent = item.dataset.factory === '1' ? 'Fábrica' : 'Propio';
      }
    });
  });
})();
