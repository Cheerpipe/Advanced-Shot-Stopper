'use strict';

const $ = id => document.getElementById(id);
const levels = ['Disconnected', 'Weak', 'Medium', 'Strong'];
const readings = ['Not connected', '−84 dBm', '−71 dBm', '−52 dBm'];
const option = Math.min(6, Math.max(1, Number(new URLSearchParams(location.search).get('option')) || 1));
document.body.dataset.signalOption = option;
const controls = document.createElement('details');
controls.className = 'previewControls';
controls.innerHTML = `<summary>Proposal 0${option} · simulated signals</summary><div class="previewControlGrid">` +
  ['Wi-Fi', 'Bluetooth'].map((name, index) => `<label>${name}<select id="preview${index}">${levels.map((level, value) => `<option value="${value}"${value === (index ? 2 : 3) ? ' selected' : ''}>${level}</option>`).join('')}</select></label>`).join('') +
  '<label>Theme<select id="previewTheme"><option value="system">System</option><option value="light">Light</option><option value="dark">Dark</option></select></label></div>';
$('view-home').prepend(controls);
if (option === 4) document.querySelectorAll('.signalIndicator').forEach(button => {
  button.insertAdjacentHTML('afterbegin', '<svg class="signalRing" viewBox="0 0 40 40" aria-hidden="true"><circle cx="20" cy="20" r="18"/><circle class="signalRingValue" cx="20" cy="20" r="18"/></svg>');
});
if (option === 5) ['wifi', 'bluetooth'].forEach((kind, index) => {
  $(kind + 'Signal').insertAdjacentHTML('beforeend', `<small>${index ? 'BLE' : 'Wi-Fi'}</small>`);
});
if (option === 6) $('wifiSignal').querySelector('svg').innerHTML = '<g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round"><path d="M2 8a9 9 0 0 1 12 0M5 11a4.5 4.5 0 0 1 6 0"/><path class="signalSlash" d="m2 3 12 16"/></g><circle cx="8" cy="15" r="1.2" fill="currentColor"/><g fill="currentColor"><rect data-step="1" x="16" y="15" width="2" height="5" rx="1"/><rect data-step="2" x="19" y="11" width="2" height="9" rx="1"/><rect data-step="3" x="22" y="6" width="2" height="14" rx="1"/></g>';

function updateSignal(index) {
  const kind = index ? 'bluetooth' : 'wifi', name = index ? 'Bluetooth' : 'Wi-Fi';
  const level = Number($(`preview${index}`).value), button = $(kind + 'Signal');
  button.dataset.level = level;
  button.setAttribute('aria-label', `${name}: ${level ? levels[level].toLowerCase() + ' signal' : index ? 'scale disconnected' : 'disconnected'}`);
  $(kind + 'Detail').textContent = level ? `${levels[level]} · ${readings[level]}` : index ? 'Scale disconnected' : readings[0];
  const ring = button.querySelector('.signalRingValue');
  if (ring) ring.style.strokeDasharray = `${level * 113.1 / 3} 113.1`;
}
for (const index of [0, 1]) {
  updateSignal(index);
  $(`preview${index}`).onchange = () => { updateSignal(index); if (index) applyScalePreview(); };
  $(index ? 'bluetoothSignal' : 'wifiSignal').onclick = () => {
    const open = $('signalDetails').hidden;
    $('signalDetails').hidden = !open;
    document.querySelectorAll('.signalIndicator').forEach(button => button.setAttribute('aria-expanded', String(open)));
  };
}
function closeDetails() {
  $('signalDetails').hidden = true;
  document.querySelectorAll('.signalIndicator').forEach(button => button.setAttribute('aria-expanded', 'false'));
}
document.addEventListener('click', event => { if (!event.target.closest('.headerSignals')) closeDetails(); });
document.addEventListener('keydown', event => { if (event.key === 'Escape') closeDetails(); });
$('previewTheme').onchange = event => {
  document.documentElement.classList.toggle('theme-light', event.target.value === 'light');
  document.documentElement.style.colorScheme = event.target.value === 'system' ? 'light dark' : event.target.value;
  if (event.target.value === 'dark') document.documentElement.classList.add('previewDark');
  else document.documentElement.classList.remove('previewDark');
};
$('navToggle').setAttribute('aria-expanded', 'false');
$('navToggle').onclick = () => {
  const open = document.body.classList.toggle('navOpen');
  $('navToggle').setAttribute('aria-expanded', String(open));
};
document.querySelectorAll('[data-route]').forEach(link => {
  link.onclick = event => { event.preventDefault(); document.body.classList.remove('navOpen'); $('navToggle').setAttribute('aria-expanded', 'false'); };
  if (link.getAttribute('data-route') === '/') link.classList.add('active');
});

const sample = {homeBbwSub: 'Stop at target weight', homeNoScaleSub: 'Warn before brewing',
  homeAtmSub: '32 s limit after scale loss', homeSlowSub: 'Allow a slower extraction',
  homeFastSub: 'Minimum extraction time', homeTouchSub: 'Ignore brief paddle touches',
  homeCupSub: 'Stop if the cup is removed', ruleChartPreset: 'Classic espresso', ruleChartMode: 'Brew by weight',
  shotElapsed: '28.4 s', shotCurrentWeight: '36.2 g', shotGoalWeight: '36.0 g', shotErr: '+0.6%',
  shotFlow: '1.3 g/s', shotMaxFlow: '2.1 g/s', shotTareTime: '0.4 s', shotFirstDrop: '6.2 s',
  shotEnded: 'Target reached', shotType: 'Brew by weight', shotPreset: 'Classic espresso',
  shotScale: 'Acaia Lunar', machineState: 'Idle', homeMicraPower: 'ON', state: 'Ready',
  scale: 'Connected · Acaia Lunar', preferredScale: 'Acaia Lunar', scaleWeight: '0.0 g', scaleTimer: '0.0 s',
  cupState: 'Present', cupWeight: '142.5 g', idleTareStatus: 'Ready',
  firmwareFooter: 'Design preview · no device connected', navFirmware: 'Design preview · no device connected'};
for (const [id, value] of Object.entries(sample)) if ($(id)) $(id).textContent = value;
document.documentElement.classList.add('lineaMicraIntegration');
document.querySelectorAll('#quickSettingsPanel input').forEach(input => { input.checked = input.id !== 'homeNoScaleBbwEnabled'; });
$('homePresetCards').innerHTML = '<div class="presetCard active selected"><strong class="presetCardTitle">Classic espresso</strong><small class="presetCardMeta">36.0 g · 25–35 s</small></div>';
$('shotBar').style.width = '72%';
for (const [id, end] of [['shotBarTicks', 50], ['ruleChartTimeTicks', 40], ['ruleChartWeightTicks', 50]]) {
  $(id).innerHTML = [0, 1, 2, 3, 4].map(n => `<span style="left:${n * 25}%">${n * end / 4}</span>`).join('');
}
function applyScalePreview() {
  const connected = $('preview1').value !== '0';
  const disconnected = {scale: 'Disconnected', scaleWeight: '—', scaleTimer: '—',
    cupState: 'Unknown · no scale', cupWeight: '—', idleTareStatus: 'Waiting for scale', state: 'No scale connected'};
  for (const [id, value] of Object.entries(disconnected)) $(id).textContent = connected ? sample[id] : value;
}
if (new URLSearchParams(location.search).get('scale') === 'disconnected') {
  $('preview1').value = '0';
  updateSignal(1);
  applyScalePreview();
}
