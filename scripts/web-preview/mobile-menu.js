'use strict';

const navigationOption = new URLSearchParams(location.search).get('navigation');
const floatingNavigation = navigationOption === '2';
const headerNavigation = navigationOption === '4';
const compactNavigation = navigationOption === '3' || headerNavigation;
document.body.dataset.navigation = floatingNavigation || compactNavigation ? navigationOption : '1';
const menuIcons = {
  '/': '<path d="m3 10 9-7 9 7v10H3zM9 20v-7h6v7"/>',
  '/stats': '<path d="M3 4v16h18M6 16l5-5 4 2 6-8"/>',
  '/history': '<path d="M3 11a9 9 0 1 1 2.6 7.4M3 4v7h7M12 7v5l3 2"/>',
  '/settings': '<path d="M4 7h16M4 17h16"/><circle cx="9" cy="7" r="3" fill="var(--sf)"/><circle cx="15" cy="17" r="3" fill="var(--sf)"/>',
  '/diagnostic': '<path d="M3 12h4l3-7 4 14 3-7h4"/>',
  '/admin': '<path d="m12 3 8 3v6c0 4-4 7-8 9-4-2-8-5-8-9V6z"/><rect x="9" y="10" width="6" height="5" rx="1"/><path d="M10 10V8a2 2 0 0 1 4 0v2"/>',
};
const mobileMenu = document.createElement('nav');
mobileMenu.className = 'mobileMenu';
mobileMenu.setAttribute('aria-label', 'Main navigation');
for (const [route, icon] of Object.entries(menuIcons).slice(0, compactNavigation ? 6 : 4)) {
  const link = document.querySelector(`.pageNav [data-route="${route}"]`).cloneNode(true);
  const label = link.textContent;
  link.innerHTML = `<svg viewBox="0 0 24 24" aria-hidden="true">${icon}</svg>` + (compactNavigation && !headerNavigation ? '' : `<span>${label}</span>`);
  if (compactNavigation) { link.setAttribute('aria-label', label); link.title = label; }
  mobileMenu.append(link);
}
document.body.append(mobileMenu);
const previewControls = document.querySelector('.previewControls');
previewControls.querySelector('summary').textContent = floatingNavigation || compactNavigation ? `Proposal 0${navigationOption} · ${headerNavigation ? 'adaptive header' : compactNavigation ? 'compact' : 'floating'} navigation · sample data` : 'Mobile navigation proposal · sample data';
$('app').prepend(previewControls);
previewControls.querySelector('.previewControlGrid').insertAdjacentHTML('beforeend',
    '<label>Diagnostics<select id="previewDiagnostics"><option value="visible">Visible</option><option value="hidden">Hidden</option></select></label>');
const moreToggle = $('navToggle');
const moreNav = document.querySelector('.pageNav');
moreToggle.innerHTML = '<svg viewBox="0 0 24 24" aria-hidden="true"><circle cx="5" cy="12" r="1.8"/><circle cx="12" cy="12" r="1.8"/><circle cx="19" cy="12" r="1.8"/></svg>';
moreToggle.setAttribute('aria-label', 'More options');
moreToggle.title = 'More options';
moreNav.setAttribute('aria-label', 'More navigation');
if (floatingNavigation) {
  moreToggle.insertAdjacentHTML('beforeend', '<span>More</span>');
  const moreDock = document.createElement('div');
  moreDock.className = 'moreDock';
  mobileMenu.append(moreDock);
  const mobileViewport = matchMedia('(max-width:699px)');
  const placeMoreMenu = () => {
    closePreviewMenu();
    (mobileViewport.matches ? moreDock : document.querySelector('.topBar')).append(moreToggle, moreNav);
    moreNav.setAttribute('aria-label', mobileViewport.matches ? 'More navigation' : 'Page navigation');
  };
  mobileViewport.addEventListener('change', placeMoreMenu);
  moreDock.addEventListener('focusout', event => {
    if (!moreDock.contains(event.relatedTarget)) closePreviewMenu();
  });
  placeMoreMenu();
}
for (const [route, icon] of Object.entries(menuIcons).slice(4)) moreNav.querySelector(`[data-route="${route}"]`).insertAdjacentHTML('afterbegin',
    `<svg class="moreIcon" viewBox="0 0 24 24" aria-hidden="true">${icon}</svg>`);

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
  moreToggle.classList.toggle('active', route === '/admin' || route === '/diagnostic');
  document.title = `${view === 'home' ? 'Home' : view[0].toUpperCase() + view.slice(1)} · Navigation preview`;
  if (focus) {
    const section = $('view-' + view);
    section.tabIndex = -1;
    section.focus({preventScroll: true});
    window.scrollTo(0, 0);
  }
}
document.querySelectorAll('[data-route]').forEach(link => {
  link.href = '/mobile-menu' + (link.dataset.route === '/' ? '' : link.dataset.route) + location.search;
  link.onclick = event => {
    if (event.ctrlKey || event.metaKey || event.shiftKey || event.altKey) return;
    event.preventDefault();
    history.pushState(null, '', link.href);
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
moreToggle.onkeydown = event => {
  if ((event.key === 'ArrowDown' || floatingNavigation && event.key === 'ArrowUp') && matchMedia('(max-width:699px)').matches) {
    event.preventDefault();
    document.body.classList.add('navOpen');
    moreToggle.setAttribute('aria-expanded', 'true');
    [...moreNav.querySelectorAll('a[data-route]')].find(link => link.getClientRects().length).focus();
  }
};
for (const type of ['pointerdown', 'focusin']) document.addEventListener(type, event => {
  if (!moreNav.contains(event.target) && !moreToggle.contains(event.target)) closePreviewMenu();
});
matchMedia('(min-width:700px)').addEventListener('change', closePreviewMenu);
window.addEventListener('scroll', () => {
  closePreviewMenu();
  document.body.style.setProperty('--header-progress', Math.min(1, Math.max(0, window.scrollY / 120)));
  if (headerNavigation) document.body.classList.toggle('previewNavScrolled', window.scrollY > 0);
}, {passive: true});
$('previewDiagnostics').onchange = event => {
  const hidden = event.target.value === 'hidden';
  document.querySelectorAll('[data-route="/diagnostic"]').forEach(link => { link.hidden = hidden; });
  if (compactNavigation) mobileMenu.style.setProperty('--nav-count', hidden ? 5 : 6);
  if (headerNavigation) updateHeaderNavigation();
  if (hidden && location.pathname.endsWith('/diagnostic')) {
    history.replaceState(null, '', '/mobile-menu' + location.search);
    showPreviewRoute(true);
  }
};
function updateHeaderOffset() {
  if (document.body.dataset.navLayout !== 'bottom') document.body.style.setProperty('--preview-menu-offset', mobileMenu.offsetTop + 'px');
}
function updateHeaderNavigation() {
  document.body.dataset.navLayout = 'icons';
  const links = [...mobileMenu.querySelectorAll('a')].filter(link => !link.hidden);
  const barStyle = getComputedStyle(mobileMenu);
  let textWidth = parseFloat(barStyle.paddingLeft) + parseFloat(barStyle.paddingRight) + 2;
  let iconWidth = 0;
  for (const link of links) {
    const style = getComputedStyle(link);
    textWidth += link.querySelector('span').getBoundingClientRect().width +
        parseFloat(style.paddingLeft) + parseFloat(style.paddingRight);
    iconWidth += link.querySelector('svg').getBoundingClientRect().width + parseFloat(style.gap);
  }
  const available = $('app').clientWidth;
  const layout = textWidth + iconWidth <= available ? 'icons' : textWidth <= available ? 'text' : 'bottom';
  document.body.dataset.navLayout = layout;
  (layout === 'bottom' ? document.body : document.querySelector('.topBar')).append(mobileMenu);
  updateHeaderOffset();
}
if (headerNavigation) {
  mobileMenu.setAttribute('aria-label', 'Page navigation');
  new ResizeObserver(updateHeaderNavigation).observe($('app'));
  new ResizeObserver(updateHeaderOffset).observe(document.querySelector('.topBar'));
  document.fonts.ready.then(updateHeaderNavigation);
  updateHeaderNavigation();
}
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
