'use strict';

(() => {
  // Íconos reales del encabezado (shell.html) para que la comparación sea fiel.
  const WIFI_SVG = '<svg viewBox="0 0 24 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path data-step="3" d="M3 8.5a14 14 0 0 1 18 0"/><path data-step="2" d="M6.2 12a9 9 0 0 1 11.6 0"/><path data-step="1" d="M9.4 15.4a4 4 0 0 1 5.2 0"/><path class="signalSlash" d="m4 4 16 16"/></g><circle cx="12" cy="19" r="1.3" fill="currentColor"/></svg>';
  const BLUETOOTH_SVG = '<svg viewBox="0 0 32 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="m3 8.5 8 7-4 3.5V5l4 3.5L3 15.5"/><path class="signalSlash" d="m2 5 11 14"/></g><g fill="currentColor"><rect data-step="1" x="17" y="15" width="3.5" height="5" rx="1"/><rect data-step="2" x="22" y="10" width="3.5" height="10" rx="1"/><rect data-step="3" x="27" y="4" width="3.5" height="16" rx="1"/></g></svg>';

  const glyph = inner => '<svg viewBox="0 0 24 24" aria-hidden="true"><g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round">' + inner + '</g></svg>';
  const SUN = '<circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2m16 0h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41"/>';
  const MOON = '<path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9z"/>';
  const SUN_RAYS = 'M12 2v2m0 16v2M4.93 4.93l1.41 1.41M17.66 17.66l1.41 1.41M2 12h2m16 0h2M6.34 17.66l-1.41 1.41M19.07 4.93l-1.41 1.41';
  const halfDisc = r => `<path d="M12 ${12 - r}a${r} ${r} 0 0 1 0 ${2 * r}z" fill="currentColor" stroke="none"/>`;
  const SCREEN = '<rect x="3" y="4.5" width="18" height="12.5" rx="2"/><path d="M8.5 20.5h7M12 17v3.5"/>';
  const PILL = '<rect x="2.9" y="7.7" width="18.2" height="8.6" rx="4.3"/>';
  const knob = cx => `<circle cx="${cx}" cy="12" r="2.9" fill="currentColor" stroke="none"/>`;
  const HORIZON = '<path d="M3.5 17.8h17"/>';
  const MEDALLION = '<circle cx="12" cy="12" r="7.6"/><path d="M12 4.4a3.8 3.8 0 0 1 0 7.6 3.8 3.8 0 0 0 0 7.6"/>' + halfDisc(7.6);
  const dot = (x, y) => `<circle cx="${x}" cy="${y}" r="1.5" fill="currentColor" stroke="none"/>`;

  const OPTIONS = [
    {star: true, name: 'Sol, luna y A', desc: 'El patrón más conocido en apps y sistemas: sol para claro, luna para oscuro y la A circulada para automático. Se entiende de inmediato, sin aprender nada.',
      glyphs: {auto: glyph('<circle cx="12" cy="12" r="8.6"/><path d="M8.8 15.6 12 8.4l3.2 7.2M10.1 13.2h3.8"/>'), light: glyph(SUN), dark: glyph(MOON)}},
    {name: 'Pantalla', desc: 'Cada modo vive dentro de una pantalla, como en los editores de código: el disco a medio llenar indica el modo automático.',
      glyphs: {auto: glyph(SCREEN + halfDisc(3.8)), light: glyph(SCREEN + '<g stroke-width="1.7"><circle cx="12" cy="10.7" r="2.3"/><path d="M12 7.4V6.3M12 14v1.1M8.4 10.7H7.3M15.6 10.7h1.1"/></g>'), dark: glyph(SCREEN + '<g stroke-width="1.7"><path d="M11.9 7.4a2.9 2.9 0 0 0 4.3 4.3 4.3 4.3 0 1 1-4.3-4.3z"/></g>')}},
    {star: true, name: 'Fases de relleno', desc: 'Una sola forma que gana tinta: círculo vacío para claro, medio lleno para automático y lleno para oscuro. Discreto y muy legible en ambos temas.',
      glyphs: {auto: glyph('<circle cx="12" cy="12" r="7.4"/>' + halfDisc(7.4)), light: glyph('<circle cx="12" cy="12" r="7.4"/>'), dark: glyph('<circle cx="12" cy="12" r="7.4" fill="currentColor"/>')}},
    {name: 'Luna con sol', desc: 'En automático el sol asoma por detrás de la luna, resumiendo el ciclo día-noche en un solo glifo; cada modo fijo muestra solo su astro.',
      glyphs: {auto: glyph('<path d="M9.6 5.4a4.8 4.8 0 0 0 7.2 7.2 7.2 7.2 0 1 1-7.2-7.2z"/><g stroke-width="1.7"><circle cx="17.6" cy="5.6" r="1.8"/><path d="M17.6 2.5v1.2M17.6 8.7V7.5M14.5 5.6h1.2M19.5 5.6h1.2"/></g>'), light: glyph(SUN), dark: glyph(MOON)}},
    {name: 'Rayos de puntos', desc: 'El mismo sol en claro y automático, pero con los rayos punteados cuando sigue al sistema; la luna se reserva para el modo oscuro.',
      glyphs: {auto: glyph(`<circle cx="12" cy="12" r="4"/><g stroke-dasharray="0.1 3.1"><path d="${SUN_RAYS}"/></g>`), light: glyph(SUN), dark: glyph(MOON)}},
    {star: true, name: 'Píldora deslizante', desc: 'Reutiliza el interruptor de la app: perilla a la izquierda para claro, centrada para automático y a la derecha para oscuro, con su astro en el lado libre.',
      glyphs: {auto: glyph(PILL + knob(12)), light: glyph(PILL + knob(7.7) + '<g stroke-width="1.7"><circle cx="16.2" cy="12" r="1.5"/><path d="M16.2 9.8V8.8M16.2 14.2v1M14 12h-1M18.4 12h1"/></g>'), dark: glyph(PILL + knob(16.3) + '<g stroke-width="1.7"><path d="M8.2 10.4a1.9 1.9 0 1 0 3 2.3 2.6 2.6 0 1 1-3-2.3z"/></g>')}},
    {name: 'Horizonte', desc: 'Una línea de horizonte constante: sol en alto para claro, amanecer para automático y luna nocturna para oscuro.',
      glyphs: {auto: glyph('<path d="M6.9 17.8a5.1 5.1 0 0 1 10.2 0"/><path d="M12 11.9v-1.4M7.7 13.5l-1-1M16.3 13.5l1-1"/>' + HORIZON), light: glyph('<circle cx="12" cy="13" r="3.4"/><path d="M12 7.6V6.4M7.6 9.3l-.9-.9M16.4 9.3l.9-.9M6.4 13H5.2M17.6 13h1.2"/>' + HORIZON), dark: glyph('<path d="M12.9 9.7a2.9 2.9 0 0 0 4.4 4.4 4.4 4.4 0 1 1-4.4-4.4z"/>' + HORIZON)}},
    {name: 'Medallón día/noche', desc: 'Un glifo único de día y noche; un punto satélite gira alrededor del aro para señalar el modo: arriba automático, izquierda claro, derecha oscuro.',
      glyphs: {auto: glyph(MEDALLION + dot(12, 2)), light: glyph(MEDALLION + dot(2.2, 12)), dark: glyph(MEDALLION + dot(21.8, 12))}},
    {name: 'Sincronía', desc: 'La flecha circular comunica «sincronizado con el sistema» sobre un disco a medio llenar; sol y luna completan claro y oscuro.',
      glyphs: {auto: glyph('<path d="M18.5 12a7.5 7.5 0 1 1-7.5-7.5c2.1 0 4.1.9 5.6 2.3l1.9 1.9"/><path d="M18.5 4.5v4.2h-4.2"/>' + halfDisc(2.1)), light: glyph(SUN), dark: glyph(MOON)}},
    {name: 'Sol y luna intercambiables', desc: 'Los dos astros con flechas de intercambio cuentan la historia de alternarse; cada modo fijo muestra su astro en grande.',
      glyphs: {auto: glyph('<g stroke-width="1.8"><circle cx="6.2" cy="12" r="1.9"/><path d="M6.2 9.4V8.2M6.2 14.6v1.2M3.6 12H2.4M8.8 12h1.2"/><path d="M18.3 9.9a2.5 2.5 0 0 0 3.75 3.75A3.75 3.75 0 1 1 18.3 9.9z"/><path d="M10.3 9.6h3.4m0 0-1.3-1.3m1.3 1.3-1.3 1.3M13.7 14.4h-3.4m0 0 1.3-1.3m-1.3 1.3 1.3 1.3"/></g>'), light: glyph(SUN), dark: glyph(MOON)}},
  ];

  const MODES = ['auto', 'light', 'dark'];
  const LABELS = {auto: 'Automático', light: 'Claro', dark: 'Oscuro'};
  const frameHTML = phone => `<div class="frame${phone ? ' phone' : ''}"><header class="topBar demoTopBar"><h1 class="brand"><a href="#"><svg class="brandMark" viewBox="0 0 36 48" aria-hidden="true"><use href="#brandMark"/></svg><span><small>Advanced</small>Shot Stopper</span></a></h1><div class="headerSignals"><button type="button" class="signalIndicator" data-level="3" tabindex="-1" aria-hidden="true">${WIFI_SVG}</button><button type="button" class="signalIndicator" data-level="2" tabindex="-1" aria-hidden="true">${BLUETOOTH_SVG}</button><button type="button" class="signalIndicator themeBtn"></button></div></header><div class="modeRow">${MODES.map(mode => `<button type="button" class="modeBtn" data-mode="${mode}"></button>`).join('')}<span class="stateChip"></span></div></div>`;

  const host = document.getElementById('options');
  OPTIONS.forEach((option, index) => {
    const section = document.createElement('section');
    section.className = 'opt';
    section.innerHTML = `<div class="optHead"><h2>${String(index + 1).padStart(2, '0')} · ${option.name}${option.star ? ' <span class="star">Recomendada</span>' : ''}</h2><p>${option.desc}</p></div>${frameHTML(false)}${frameHTML(true)}`;
    section.querySelectorAll('.modeBtn').forEach(button => {
      const mode = button.dataset.mode;
      button.innerHTML = option.glyphs[mode] + LABELS[mode];
    });
    const apply = mode => {
      section.dataset.mode = mode;
      const next = MODES[(MODES.indexOf(mode) + 1) % MODES.length];
      section.querySelectorAll('.themeBtn').forEach(button => {
        button.innerHTML = option.glyphs[mode];
        button.setAttribute('aria-label', `Tema: ${LABELS[mode].toLowerCase()}. Cambiar a ${LABELS[next].toLowerCase()}`);
      });
      section.querySelectorAll('.modeBtn').forEach(button => {
        const on = button.dataset.mode === mode;
        button.classList.toggle('on', on);
        button.setAttribute('aria-pressed', String(on));
      });
      section.querySelectorAll('.stateChip').forEach(chip => { chip.textContent = `Modo: ${LABELS[mode]}`; });
    };
    section.querySelectorAll('.themeBtn').forEach(button => button.addEventListener('click', () => apply(MODES[(MODES.indexOf(section.dataset.mode || 'auto') + 1) % MODES.length])));
    section.querySelectorAll('.modeBtn').forEach(button => button.addEventListener('click', () => apply(button.dataset.mode)));
    apply('auto');
    host.append(section);
  });
  document.querySelectorAll('.demoTopBar a').forEach(link => link.addEventListener('click', event => event.preventDefault()));

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
