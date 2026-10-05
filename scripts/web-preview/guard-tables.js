'use strict';

(() => {
  // Real preset data from the Home quick settings screenshots.
  const TIME = {prot: 12, tMin: 28, tMaxBbw: 44, wall: 50}; // BBW cut protection (s), min/max BBW brew time (s), machine operational wall (s)
  const PRESETS = [
    {name: 'Double', badge: 'Factory', target: 36, floor: 34, ceil: 42.5},
    {name: 'Single', badge: 'Factory', target: 18, floor: 16, ceil: 20},
  ];
  const g = v => (Number.isInteger(v) ? v : v.toFixed(1)) + ' g';
  const dot = c => `<i style="background:${c}" aria-hidden="true"></i>`;
  const FC = '#d97706', GC = 'var(--ok)', SC = '#5594dd';

  // The brief rules per preset (values from settings).
  const rules = p => ({
    fast: `Cuts from ${TIME.prot} s at ${g(p.ceil)}, or at ${TIME.tMin} s`,
    bbw: `Cuts at ${g(p.target)} between ${TIME.tMin} and ${TIME.tMaxBbw} s`,
    slow: `Cuts at ${g(p.floor)} from ${TIME.tMaxBbw} s, or at ${TIME.wall} s`,
  });

  // Desktop + phone frames sharing one accordion per preset.
  function frameHtml(label) { return `
    <div class="frame"><p class="frameLabel">${label}</p>
    <fieldset class="qsPanel"><legend>Quick Settings</legend><p class="qsPresetLabel">Presets</p>
    <div class="gtAcc">${PRESETS.map((p, i) =>
      `<div class="gtPreset${i === 0 ? ' open' : ''}">` +
      `<button type="button" class="gtAccHead" aria-expanded="${i === 0}">` +
      `<span class="gtAccName">${p.name}</span><span class="gtAccBadge">${p.badge}</span>` +
      `<span class="gtAccTarget">Target ${g(p.target)}</span>` +
      `<span class="gtAccChev" aria-hidden="true">▾</span></button>` +
      `<div class="gtPanel"><div class="gtPanelIn"><div class="gtPanelPad">` +
      `<div class="gtRows">` +
      `<div class="gtRow"><span class="gtName">${dot(FC)}Fast</span><span class="gtRule">${rules(p).fast}</span></div>` +
      `<div class="gtRow"><span class="gtName">${dot(GC)}BBW</span><span class="gtRule">${rules(p).bbw}</span></div>` +
      `<div class="gtRow"><span class="gtName">${dot(SC)}Slow</span><span class="gtRule">${rules(p).slow}</span></div>` +
      `</div></div></div></div></div>`).join('')}</div>
    </fieldset></div>`; }

  const host = document.getElementById('options');
  host.innerHTML = frameHtml('Escritorio') + frameHtml('Teléfono · 360 px');

  // Accordion: one block open at a time, animated by the CSS grid transition.
  document.getElementById('options').addEventListener('click', event => {
    const head = event.target.closest('.gtAccHead');
    if (!head) return;
    const preset = head.closest('.gtPreset');
    if (preset.classList.contains('open')) return;
    preset.parentElement.querySelectorAll('.gtPreset.open').forEach(o => {
      o.classList.remove('open');
      o.querySelector('.gtAccHead').setAttribute('aria-expanded', 'false');
    });
    preset.classList.add('open');
    head.setAttribute('aria-expanded', 'true');
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
