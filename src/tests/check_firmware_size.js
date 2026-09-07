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
actual.image = fs.statSync(binPath).size;
const failures = [];
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
