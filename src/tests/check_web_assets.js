'use strict';

// Contract chunks are concatenated before compilation so they share fixtures
// while remaining independently discoverable by domain.
const fs = require('fs');
const path = require('path');
const Module = require('module');

const directory = path.join(__dirname, 'web_contracts');
const source = fs.readdirSync(directory)
  .filter((name) => name.endsWith('.inc.js'))
  .sort()
  .map((name) => fs.readFileSync(path.join(directory, name), 'utf8'))
  .join('\n');
const contracts = new Module(__filename, module);
contracts.filename = __filename;
contracts.paths = module.paths;
contracts._compile(source, __filename);
