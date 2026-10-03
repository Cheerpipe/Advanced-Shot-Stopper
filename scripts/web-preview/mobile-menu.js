'use strict';

const menuIcons = {
  '/': '<path d="m3 10 9-7 9 7v10H3zM9 20v-7h6v7"/>',
  '/stats': '<path d="M3 4v16h18M6 16l5-5 4 2 6-8"/>',
  '/history': '<path d="M3 11a9 9 0 1 1 2.6 7.4M3 4v7h7M12 7v5l3 2"/>',
  '/settings': '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3" fill="var(--sf)"/><circle cx="15" cy="17" r="3" fill="var(--sf)"/>',
};
const mobileMenu = document.createElement('nav');
mobileMenu.className = 'mobileMenu';
mobileMenu.setAttribute('aria-label', 'Main navigation');
for (const [route, icon] of Object.entries(menuIcons)) {
  const link = document.querySelector(`.pageNav [data-route="${route}"]`).cloneNode(true);
  link.innerHTML = `<svg viewBox="0 0 24 24" aria-hidden="true">${icon}</svg><span>${link.textContent}</span>`;
  mobileMenu.append(link);
}
document.body.append(mobileMenu);
const previewControls = document.querySelector('.previewControls');
previewControls.querySelector('summary').textContent = 'Mobile navigation proposal · sample data';
$('app').prepend(previewControls);
previewControls.querySelector('.previewControlGrid').insertAdjacentHTML('beforeend',
    '<label>Diagnostics<select id="previewDiagnostics"><option value="visible">Visible</option><option value="hidden">Hidden</option></select></label>');

function closePreviewMenu() {
  document.body.classList.remove('navOpen');
  $('navToggle').setAttribute('aria-expanded', 'false');
}
function showPreviewRoute(focus = false) {
  const route = location.pathname.replace(/^\/mobile-menu/, '').replace(/\/$/, '') || '/';
  const view = route === '/' ? 'home' : route.slice(1);
  document.querySelectorAll('.view').forEach(section => { section.hidden = section.dataset.view !== view; });
  document.querySelectorAll('[data-route]').forEach(link => {
    const active = link.dataset.route === route;
    link.classList.toggle('active', active);
    if (active) link.setAttribute('aria-current', 'page');
    else link.removeAttribute('aria-current');
  });
  closePreviewMenu();
  document.title = `${view === 'home' ? 'Home' : view[0].toUpperCase() + view.slice(1)} · Navigation preview`;
  if (focus) {
    const section = $('view-' + view);
    section.tabIndex = -1;
    section.focus({preventScroll: true});
    window.scrollTo(0, 0);
  }
}
document.querySelectorAll('[data-route]').forEach(link => {
  link.href = '/mobile-menu' + (link.dataset.route === '/' ? '' : link.dataset.route);
  link.onclick = event => {
    if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
    event.preventDefault();
    history.pushState(null, '', link.href + location.search);
    showPreviewRoute(true);
  };
});
window.addEventListener('popstate', () => showPreviewRoute(true));
$('navToggle').setAttribute('aria-controls', 'previewPageNav');
document.querySelector('.pageNav').id = 'previewPageNav';
document.addEventListener('keydown', event => {
  if (event.key === 'Escape' && document.body.classList.contains('navOpen')) {
    closePreviewMenu();
    $('navToggle').focus();
  }
});
matchMedia('(min-width:700px)').addEventListener('change', closePreviewMenu);
window.addEventListener('scroll', () => {
  document.body.style.setProperty('--header-progress', Math.min(1, Math.max(0, window.scrollY / 120)));
}, {passive: true});
$('previewDiagnostics').onchange = event => {
  const hidden = event.target.value === 'hidden';
  document.querySelector('.pageNav [data-route="/diagnostic"]').hidden = hidden;
  if (hidden && location.pathname.endsWith('/diagnostic')) {
    history.replaceState(null, '', '/mobile-menu' + location.search);
    showPreviewRoute(true);
  }
};
if (new URLSearchParams(location.search).get('diagnostics') === 'hidden') {
  $('previewDiagnostics').value = 'hidden';
  $('previewDiagnostics').dispatchEvent(new Event('change'));
}

const menuSamples = {statsAvgDur: '28.4 s', statsAvgWeight: '36.2 g', statsAvgDaily: '3.2',
  statsAvgErr: '+0.6%', statsAvgFlow: '1.3 g/s', shotTableState: 'Sample shots · no device connected',
  historyTableState: 'Sample activations · no device connected'};
for (const [id, value] of Object.entries(menuSamples)) {
  $(id).textContent = value;
  $(id).hidden = false;
}
for (const [index, time] of ['08:42', '08:15', '07:58'].entries()) {
  const shot = document.createElement('tr');
  shot.className = 'noSpark';
  const values = [time, `${28.4 + index} s`, '36.0 g', '36.2 g', '+0.6%', '1.3 g/s', '2.1 g/s', '0.4 s', '6.2 s', 'Target reached', 'Brew by weight', 'Classic espresso', 'Acaia Lunar', '★★★★★'];
  values.forEach((value, i) => {
    const cell = shot.insertCell();
    cell.textContent = value;
    cell.dataset.label = document.querySelectorAll('#shotTable th')[i].textContent;
    if (i === 1) cell.className = 'shotDur';
    if (i === 3) cell.className = 'shotActual';
    if (i === 13) cell.className = 'shotRateCell';
  });
  $('shotRows').append(shot);
  const activation = document.createElement('tr');
  [time, `${28.4 + index} s`, 'Brew by weight'].forEach((value, i) => {
    const cell = activation.insertCell();
    cell.textContent = value;
    cell.dataset.label = document.querySelectorAll('#historyTable th')[i].textContent;
  });
  $('historyRows').append(activation);
}
$('presetCards').innerHTML = $('homePresetCards').innerHTML;
document.querySelectorAll('.view button, .view input, .view select, .view textarea').forEach(control => { control.disabled = true; });
showPreviewRoute();
