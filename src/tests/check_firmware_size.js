'use strict';

const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..', '..');
const slotLimits = {n16r8: 3145728, n8r4: 3342336};
const binPath = process.argv[2] || path.join(root, 'build-idf', 'n16r8', 'shotstopper.bin');
const normalized = binPath.replace(/\\/g, '/');
const explicitArch = process.argv[3] === '--arch' ? process.argv[4] : '';
const arch = slotLimits[explicitArch] ? explicitArch :
  normalized.includes('/n8r4/') ? 'n8r4' :
  normalized.includes('/n16r8/') ? 'n16r8' : null;

if (!arch) {
  console.error('Firmware path must identify n8r4 or n16r8');
  process.exit(2);
}
const sizePath = path.join(path.dirname(binPath), 'size.json');
const sdkconfigPath = path.join(path.dirname(binPath), 'sdkconfig');
if (!fs.existsSync(binPath) || !fs.existsSync(sizePath) || !fs.existsSync(sdkconfigPath)) {
  console.error(`Firmware resource artifacts missing: ${binPath}, ${sizePath}, ${sdkconfigPath}`);
  process.exit(127);
}
const optimization = fs.readFileSync(sdkconfigPath, 'utf8').match(
  /^CONFIG_COMPILER_OPTIMIZATION_(SIZE|PERF|DEBUG|NONE)=y$/gm) || [];
if (optimization.length !== 1) {
  console.error(`Firmware optimization config must select one supported level: ${sdkconfigPath}`);
  process.exit(2);
}
const qualifiedSizeProfile = optimization[0] === 'CONFIG_COMPILER_OPTIMIZATION_SIZE=y';

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
  if (!Number.isFinite(value) || value < 0) failures.push(`${metric} is missing or invalid`);
  else if (qualifiedSizeProfile && value > limit) failures.push(`${metric} ${value} > baseline budget ${limit}`);
  else console.log(qualifiedSizeProfile
    ? `${metric}: ${value} (baseline ${baseline}, delta ${value - baseline})`
    : `${metric}: ${value} (experimental; no versioned baseline)`);
}
if (failures.length) throw new Error(failures.join('; '));
console.log(qualifiedSizeProfile
  ? `${arch}: image and memory regions are within versioned budgets`
  : `${arch}: experimental optimization; -Os baseline comparisons do not apply`);
