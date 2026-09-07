#!/usr/bin/env node
'use strict';
// Exercise the production OTA client with a simulated HTTP transport and DOM.
const assert = require('assert/strict');
const fs = require('fs');
const path = require('path');
const vm = require('vm');
const crypto = require('crypto');
const jsDir = path.join(__dirname, '../web/js');
const runtime = fs.readFileSync(path.join(jsDir, 'runtime.js'), 'utf8');
const otaSource = runtime.slice(runtime.indexOf('const OTA_UPLOAD_TIMEOUT_MS='),
    runtime.indexOf('function statusPageOk('));
const parser = new Function(fs.readFileSync(path.join(jsDir, 'ota-image.js'), 'utf8')
    .replace(/export\s+/g, '') + ';return otaFileIdentity;')();

function fixture() {
  const image = Buffer.alloc(131104, 0x5a);
  image[0] = 0xe9; image[12] = 9; image[13] = 0; image[23] = 1;
  image.writeUInt32LE(0xabcd5432, 32);
  image.fill(0, 80, 112); image.write('shotstopper', 80);
  image.write('SHOTSTOPPER_FW_TAG_V1|arch=n16r8|ver=1.2.3|packed=123|END', 320);
  crypto.createHash('sha256').update(image.subarray(0, -32)).digest().copy(image, image.length - 32);
  return new Blob([image]);
}

async function harness(mode) {
  const file = fixture(), identity = await parser(file), items = new Map(), nodes = new Map();
  const messages = [], patches = [], payloads = [];
  let offset = 0, posts = 0, commits = 0;
  const session = {...identity, transferId: 'test-transfer'};
  const status = () => ({otaProtocolVersion: 2, available: true, safe: true,
    confirmed: true, runningIdentityValid: true, bootId: 7,
    running: {arch: identity.arch, version: identity.version, imageSha256: identity.imageSha256},
    state: offset === file.size ? 'staged' : 'receiving', sessionActive: true,
    transferId: session.transferId, expectedBytes: file.size, sha256: identity.sha256,
    sessionArch: identity.arch, sessionVersion: identity.version, nextOffset: offset,
    staged: {arch: identity.arch, version: identity.version, packed: identity.packed,
      imageSha256: identity.imageSha256}});
  const context = vm.createContext({console, crypto: crypto.webcrypto, localStorage: {
    getItem: key => items.get(key) || null,
    setItem: (key, value) => items.set(key, value), removeItem: key => items.delete(key)},
  $: key => {
    if (!nodes.has(key)) nodes.set(key, {files: [file], value: 0, classList: {add() {}, remove() {}}});
    return nodes.get(key);
  }, message: text => messages.push(text), clearFieldErrors() {}, showFieldError() {},
  formatCommandError: (text, error) => text + ' ' + error.message,
  refreshStatus() {}, confirm: () => true, controlsMutable: true});
  vm.runInContext(otaSource, context);
  context.parseFile = parser;
  vm.runInContext('otaFileIdentity=parseFile;applyOtaStatus=function(o){if(o)otaLastStatus=o};', context);
  context.send = async (url, body, progress, timeout, method = 'POST', headers = {}) => {
    if (method === 'POST' && url.endsWith('/session')) {
      posts++;
      const request = JSON.parse(body); payloads.push(request);
      assert.deepEqual(Object.keys(request).sort(), ['arch', 'sha256', 'size', 'transferId', 'version']);
      if (mode === 'refused') throw Object.assign(new Error('PENDING_VERIFY'), {status: 409});
      return status();
    }
    if (method === 'PATCH') {
      const at = Number(headers['X-OTA-Offset']); patches.push(at);
      assert.deepEqual(Buffer.from(await body.arrayBuffer()),
          Buffer.from(await file.slice(at, at + body.size).arrayBuffer()));
      if (mode === 'rewind' && patches.length === 2) {offset = 4096; throw new Error('Device unreachable');}
      if (mode === 'stalled' || mode === 'expired') throw new Error('Device unreachable');
      offset = at + body.size;
      if (mode === 'lost' && patches.length === 1) throw new Error('Device unreachable');
      return status();
    }
    if (method === 'POST' && url.endsWith('/flash')) {
      commits++;
      throw new Error('Device unreachable');
    }
    if (method === 'GET') {
      if (mode === 'expired' && patches.length) return {...status(), sessionActive: false, state: 'idle', transferId: ''};
      if (commits) {
        if (mode === 'wrong-commit') return {...status(), state: 'committed', restartPending: true, transferId: 'other'};
        if (mode === 'lost-commit') return {...status(), state: 'committed', restartPending: true};
        if (mode === 'booted-commit') return {...status(), state: 'idle', bootId: 8, transferId: ''};
      }
      return status();
    }
    throw new Error('Unexpected request');
  };
  vm.runInContext('otaSend=send;', context);
  return {context, session, identity, items, messages, patches, payloads, file,
    status, setOffset: value => {offset = value;}, commits: () => commits};
}

(async () => {
  for (const mode of ['normal', 'lost', 'rewind']) {
    const h = await harness(mode);
    await vm.runInContext('otaUpload()', h.context);
    assert.equal(h.payloads.length, 1);
    assert.match(h.messages.at(-1), /Firmware verified/);
    assert.deepEqual(h.patches, mode === 'rewind' ? [0, 65536, 4096, 69632] : [0, 65536, 131072]);
    const saved = JSON.parse(h.items.get('ssOtaSession'));
    assert.equal(saved.imageSha256, h.identity.imageSha256);
    assert.notEqual(saved.imageSha256, saved.sha256);
  }
  for (const mode of ['refused', 'stalled', 'expired']) {
    const h = await harness(mode);
    await vm.runInContext('otaUpload()', h.context);
    if (mode === 'stalled') {
      assert.equal(h.patches.length, 3);
      assert.match(h.messages.at(-1), /upload paused/);
    } else {
      assert.doesNotMatch(h.messages.at(-1), /upload paused/);
      assert.equal(h.patches.length, mode === 'refused' ? 0 : 1);
    }
  }
  for (const mode of ['lost-commit', 'wrong-commit', 'booted-commit']) {
    const h = await harness(mode);
    h.setOffset(h.file.size);
    h.context.initial = h.status();
    h.items.set('ssOtaSession', JSON.stringify(h.session));
    vm.runInContext('otaLastStatus=initial;', h.context);
    await vm.runInContext('otaFlash()', h.context);
    assert.equal(h.commits(), 1);
    assert.match(h.messages.at(-1), mode === 'wrong-commit' ? /unverified/ :
      mode === 'booted-commit' ? /OTA confirmed/ : /Waiting for the controller to restart/);
    if (mode === 'booted-commit') assert.equal(h.items.has('ssOtaCommit'), false);
    else assert.equal(h.items.has('ssOtaCommit'), true);
  }
  const h = await harness('normal');
  h.context.expected = {...h.session, bootId: 7};
  for (const [change, pattern] of [
    [{bootId: 7}, /not yet verified/],
    [{bootId: 8, confirmed: false, confirmBlockReason: 'ble-active'}, /confirmation pending \(ble-active\)/],
    [{bootId: 8, running: {...h.status().running, imageSha256: 'a'.repeat(64)}}, /another image/],
    [{bootId: undefined}, /unverified/],
    [{bootId: 8}, /OTA confirmed/],
  ]) {
    h.context.remote = {...h.status(), ...change};
    assert.match(vm.runInContext('otaCheckBoot(expected,remote)', h.context), pattern);
  }
  // Reload/unlock uses the persisted expectation when Admin applies OTA status.
  h.items.set('ssOtaCommit', JSON.stringify({...h.session, bootId: 7}));
  h.context.remote = {...h.status(), bootId: 8};
  assert.match(vm.runInContext('otaStatusText(remote)', h.context), /OTA confirmed/);
  assert.equal(h.items.has('ssOtaCommit'), false);
  console.log('OTA Web resilience: strict sessions, recovery, commit and boot identity OK');
})().catch(error => {console.error(error); process.exitCode = 1;});
