'use strict';

(() => {
  const button = document.getElementById('themeToggle');
  if (!button) return;
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
})();
