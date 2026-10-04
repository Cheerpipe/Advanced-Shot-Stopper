'use strict';

(() => {
  // Cabecera, navegación y barra de acciones comunes: se inyectan en cada maqueta
  // para que todas se lean como pantallas completas del firmware.
  const top = `
  <div class="mockTop">
    <div class="brand"><svg class="brandMark" viewBox="0 0 36 48" aria-hidden="true"><use href="#brandMark"/></svg><span><small>Open</small>Brew by Weight</span></div>
    <div class="headerSignals">
      <span class="signalIndicator" data-level="3" title="Wi-Fi: strong"><svg viewBox="0 0 24 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path data-step="3" d="M3 8.5a14 14 0 0 1 18 0"/><path data-step="2" d="M6.2 12a9 9 0 0 1 11.6 0"/><path data-step="1" d="M9.4 15.4a4 4 0 0 1 5.2 0"/></g><circle cx="12" cy="19" r="1.3" fill="currentColor"/></svg></span>
      <span class="signalIndicator" data-level="3" title="Bluetooth: Lunar"><svg viewBox="0 0 32 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="m3 8.5 8 7-4 3.5V5l4 3.5L3 15.5"/><g fill="currentColor" stroke="none"><rect data-step="1" x="17" y="15" width="3.5" height="5" rx="1"/><rect data-step="2" x="22" y="10" width="3.5" height="10" rx="1"/><rect data-step="3" x="27" y="4" width="3.5" height="16" rx="1"/></g></g></svg></span>
    </div>
  </div>`;
  const nav = `
  <nav class="pageNav" aria-label="Pages">
    <a class="active" aria-current="page"><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m3 10 9-7 9 7v10H3zM9 20v-7h6v7"/></svg><span>Home</span></a>
    <a><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 4v16h18M6 16l5-5 4 2 6-8"/></svg><span>Stats</span></a>
    <a><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M3 11a9 9 0 1 1 2.6 7.4M3 4v7h7M12 7v5l3 2"/></svg><span>History</span></a>
    <a><svg viewBox="0 0 24 24" aria-hidden="true"><path d="M4 7h2m6 0h8M4 17h8m6 0h2"/><circle cx="9" cy="7" r="3"/><circle cx="15" cy="17" r="3"/></svg><span>Settings</span></a>
    <a><svg viewBox="0 0 24 24" aria-hidden="true"><path d="m12 3 8 3v6c0 4-4 7-8 9-4-2-8-5-8-9V6z"/><rect x="9" y="10" width="6" height="5" rx="1"/><path d="M10 10V8a2 2 0 0 1 4 0v2"/></svg><span>Admin</span></a>
  </nav>`;
  const dock = mode => mode === 'stop' ? `
  <div class="mockDock" role="group" aria-label="Actions">
    <button type="button" class="btnGlyph solid"><span class="g">■</span><span class="t">Stop shot</span></button>
    <button type="button" class="btnGlyph ghost narrow"><span class="g">≋</span><span class="t">Rinse</span></button>
    <button type="button" class="btnGlyph ghost narrow"><span class="g">!</span><span class="t">Push</span></button>
  </div>` : `
  <div class="mockDock" role="group" aria-label="Actions">
    <button type="button" class="btnGlyph ghost narrow"><span class="g">≋</span><span class="t">Rinse</span></button>
    <button type="button" class="btnGlyph"><span class="g">▶</span><span class="t">Start shot</span></button>
    <button type="button" class="btnGlyph ghost narrow"><span class="g">!</span><span class="t">Push</span></button>
  </div>`;

  document.querySelectorAll('.mock[data-chrome]').forEach(mock => {
    mock.insertAdjacentHTML('afterbegin', top);
    mock.insertAdjacentHTML('beforeend', nav);
    if (mock.dataset.dock !== undefined) {
      mock.classList.add('hasDock');
      mock.insertAdjacentHTML('beforeend', dock(mock.dataset.dock));
    }
  });

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
})();
