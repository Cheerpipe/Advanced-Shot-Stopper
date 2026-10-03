'use strict';

const $ = id => document.getElementById(id);
const levels = ['Disconnected', 'Weak', 'Medium', 'Strong'];
const option = Math.min(6, Math.max(1, Number(new URLSearchParams(location.search).get('option')) || 1));
document.body.dataset.signalOption = option;
const controls = document.createElement('details');
controls.className = 'previewControls';
controls.innerHTML = `<summary>Proposal 0${option} · simulated signals</summary><div class="previewControlGrid">` +
  ['Wi-Fi', 'Bluetooth'].map((name, index) => `<label>${name}<select id="preview${index}">${levels.map((level, value) => `<option value="${value}"${value === (index ? 2 : 3) ? ' selected' : ''}>${level}</option>`).join('')}</select></label>`).join('') +
  '<label>Theme<select id="previewTheme"><option value="system">System</option><option value="light">Light</option><option value="dark">Dark</option></select></label></div>';
$('view-home').prepend(controls);
const glyphs = {
  2: [
    '<g fill="currentColor"><path data-step="3" d="M2 8a16 16 0 0 1 20 0l-1.8 2.2a13 13 0 0 0-16.4 0z"/><path data-step="2" d="M5.3 12a10.7 10.7 0 0 1 13.4 0L17 14.2a8 8 0 0 0-10 0z"/><path data-step="1" d="M8.6 16a5.4 5.4 0 0 1 6.8 0L12 20z"/></g><path class="signalSlash" d="m3 3 18 18" stroke="currentColor" stroke-width="2"/>',
    '<g fill="currentColor"><path fill-rule="evenodd" d="M10 1.5 19 7l-6 5 6 5-9 5.5v-8L5 19l-1.5-2L9 12 3.5 7 5 5l5 4.5zm2.5 4.4v3.4L15.8 7zm0 8.8v3.4l3.3-1.1z"/><rect data-step="1" x="21" y="16" width="2.8" height="5" rx="1.4"/><rect data-step="2" x="25" y="11" width="2.8" height="10" rx="1.4"/><rect data-step="3" x="29" y="5" width="2.8" height="16" rx="1.4"/></g><path class="signalSlash" d="m3 3 16 18" stroke="currentColor" stroke-width="2"/>',
  ],
  3: [
    '<g fill="none" stroke="currentColor" stroke-width="1.25" stroke-linecap="round"><path data-step="3" d="M2.5 7.5a15 15 0 0 1 19 0"/><path data-step="2" d="M5.5 11a10 10 0 0 1 13 0"/><path data-step="1" d="M8.5 14.5a5 5 0 0 1 7 0"/><circle cx="12" cy="18.5" r="1.4"/><path class="signalSlash" d="m3 3 18 18"/></g>',
    '<g fill="none" stroke="currentColor" stroke-width="1.25" stroke-linecap="round" stroke-linejoin="round"><path d="m4 6 13 11-6 5V2l6 5L4 18"/><rect data-step="1" x="21" y="16" width="2.5" height="5" rx=".6"/><rect data-step="2" x="25" y="11" width="2.5" height="10" rx=".6"/><rect data-step="3" x="29" y="5" width="2.5" height="16" rx=".6"/><path class="signalSlash" d="m3 3 16 18"/></g>',
  ],
  4: [
    '<g fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round"><path d="M1.5 8a8 8 0 0 1 10 0M4 11a4 4 0 0 1 5 0"/><path class="signalSlash" d="m2 3 12 16"/></g><circle cx="6.5" cy="14" r="1.1" fill="currentColor"/><g fill="currentColor"><rect data-step="1" x="14" y="16" width="2.6" height="5" rx=".5"/><rect data-step="2" x="17.7" y="11" width="2.6" height="10" rx=".5"/><rect data-step="3" x="21.4" y="5" width="2.6" height="16" rx=".5"/></g>',
    '<g fill="none" stroke="currentColor" stroke-width="1.7" stroke-linecap="round" stroke-linejoin="round"><path d="m3 7 11 10-5 4V3l5 4L3 17"/><path class="signalSlash" d="m2 3 14 18"/></g><g fill="currentColor"><rect data-step="1" x="19" y="16" width="3" height="5" rx=".5"/><rect data-step="2" x="23.5" y="11" width="3" height="10" rx=".5"/><rect data-step="3" x="28" y="5" width="3" height="16" rx=".5"/></g>',
  ],
  5: [
    '<g fill="none" stroke="currentColor" stroke-width="2.3" stroke-linecap="round" stroke-dasharray=".1 3.6"><path data-step="3" d="M2.5 8a15 15 0 0 1 19 0"/><path data-step="2" d="M6 12a9 9 0 0 1 12 0"/><path data-step="1" d="M9 15.7a4.5 4.5 0 0 1 6 0"/></g><circle cx="12" cy="20" r="1.25" fill="currentColor"/><path class="signalSlash" d="m3 3 18 18" stroke="currentColor" stroke-width="1.8"/>',
    '<g fill="none" stroke="currentColor" stroke-width="1.8" stroke-linecap="round" stroke-linejoin="round"><path d="m5 7 12 10-6 5V2l6 5L5 17"/><path class="signalSlash" d="m3 3 16 18"/></g><g fill="currentColor"><circle data-step="1" cx="26" cy="19" r="2"/><circle data-step="2" cx="26" cy="12" r="2"/><circle data-step="3" cx="26" cy="5" r="2"/></g>',
  ],
  6: [
    '<g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="square" stroke-linejoin="miter"><path data-step="3" d="m3 8 9-4 9 4"/><path data-step="2" d="m6 12 6-3 6 3"/><path data-step="1" d="m9 16 3-2 3 2"/><path class="signalSlash" d="m3 3 18 18"/></g><path d="m12 18 2 2-2 2-2-2z" fill="currentColor"/>',
    '<g fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="square" stroke-linejoin="miter"><path d="m4 6 14 12-7 4V2l7 4L4 18"/><path class="signalSlash" d="m3 3 16 18"/></g><g fill="currentColor"><path data-step="1" d="M21 16h3v5h-3z"/><path data-step="2" d="M25 11h3v10h-3z"/><path data-step="3" d="M29 5h3v16h-3z"/></g>',
  ],
};
if (glyphs[option]) ['wifi', 'bluetooth'].forEach((kind, index) => {
  $(kind + 'Signal').querySelector('svg').innerHTML = glyphs[option][index];
});

function updateSignal() {
  const connections = {};
  for (const [i, kind] of ['wifi', 'bluetooth'].entries()) {
    const level = Number($(`preview${i}`).value);
    connections[kind + 'Connected'] = level > 0;
    connections[kind + 'Rssi'] = [null, -84, -71, -52][level];
    connections[kind + 'Name'] = i ? 'Acaia Lunar' : 'Coffee Studio';
  }
  updateHeaderSignals({connections});
}
initHeaderSignals();
for (const index of [0, 1]) {
  updateSignal(index);
  $(`preview${index}`).onchange = () => { updateSignal(index); if (index) applyScalePreview(); };
}
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
