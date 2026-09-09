'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..', '..');
const slotLimits = {n16r8: 3145728, n8r4: 3342336};
const binPath = process.argv[2] || path.join(root, 'build-idf', 'n16r8', 'shotstopper.bin');
const normalized = binPath.replace(/\\/g, '/');
const arch = normalized.includes('/n8r4/') ? 'n8r4' :
  normalized.includes('/n16r8/') ? 'n16r8' : null;

if (!arch) {
  console.error('Firmware path must identify n8r4 or n16r8');
  process.exit(2);
}
const sizePath = path.join(path.dirname(binPath), 'size.json');
if (!fs.existsSync(binPath) || !fs.existsSync(sizePath)) {
  console.error(`Firmware resource artifacts missing: ${binPath}, ${sizePath}`);
  process.exit(127);
}

const config = JSON.parse(fs.readFileSync(
  path.join(root, 'config', 'resource-baselines.json'), 'utf8'));
const actual = JSON.parse(fs.readFileSync(sizePath, 'utf8'));
if (Array.isArray(actual.layout)) {
  const regions = Object.fromEntries(actual.layout.map(region => [region.name, region]));
  actual.used_diram = regions.DIRAM?.used;
  actual.flash_code = regions['Flash Code']?.parts['.text']?.size;
  actual.flash_rodata = regions['Flash Data']?.parts['.rodata']?.size;
}
actual.image = fs.statSync(binPath).size;
const failures = [];
const map = fs.readFileSync(path.join(path.dirname(binPath), 'shotstopper.map'), 'utf8');
function symbolAddress(name) {
  const match = map.match(new RegExp('^\\s*(0x[0-9a-fA-F]+)\\s+' + name + '(?:\\s|$)', 'm'));
  if (!match) throw new Error(`Required memory symbol missing: ${name}`);
  return Number.parseInt(match[1], 16);
}
const externalBssBytes = symbolAddress('_ext_ram_bss_end') - symbolAddress('_ext_ram_bss_start');
if (externalBssBytes < 0 || externalBssBytes > config.maximumExternalBssBytes) {
  failures.push(`external BSS ${externalBssBytes} > budget ${config.maximumExternalBssBytes}`);
}
for (const name of ['localBuzzer', 'taskProfiler']) {
  const address = symbolAddress(name);
  if (address < 0x3fc80000 || address >= 0x3fd00000) {
    failures.push(`${name} must remain in internal SRAM`);
  }
}
console.log(`external BSS: ${externalBssBytes} (budget ${config.maximumExternalBssBytes})`);
if (actual.image > slotLimits[arch]) {
  failures.push(`image ${actual.image} > OTA slot ${slotLimits[arch]}`);
}
for (const [metric, baseline] of Object.entries(config.targets[arch])) {
  const value = actual[metric];
  const limit = baseline + config.allowedGrowthBytes[metric];
  if (!Number.isFinite(value)) failures.push(`${metric} is missing`);
  else if (value > limit) failures.push(`${metric} ${value} > baseline budget ${limit}`);
  else console.log(`${metric}: ${value} (baseline ${baseline}, delta ${value - baseline})`);
}
if (failures.length) throw new Error(failures.join('; '));
console.log(`${arch}: image and memory regions are within versioned budgets`);
