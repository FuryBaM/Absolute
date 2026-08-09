'use strict';

const assert = require('assert');
const fs = require('fs');
const os = require('os');
const path = require('path');
const target = require('../wasm-target');

assert.strictEqual(target.normalizeTarget('host'), '');
assert.strictEqual(target.normalizeTarget('native'), '');
assert.strictEqual(target.normalizeTarget(' wasm32-unknown-unknown '), 'wasm32-unknown-unknown');
assert(target.isWasmTarget('wasm32-unknown-unknown'));
assert(target.isWasmTarget('custom-wasm64-target'));
assert(!target.isWasmTarget('x86_64-pc-windows-msvc'));
assert.strictEqual(target.projectRuntime({}), 'node');
assert.strictEqual(target.projectRuntime({ runtime: 'Browser' }), 'browser');
assert.deepStrictEqual(target.projectRunArgs({ runArgs: ['--mode=full', 42] }), ['--mode=full', '42']);
assert(target.outputName('demo', 'wasm32-unknown-unknown').endsWith('.wasm'));

const root = fs.mkdtempSync(path.join(os.tmpdir(), 'absolute-wasm-host-'));
try {
    const host = path.join(root, 'tools', 'absolute-wasm-host.js');
    fs.mkdirSync(path.dirname(host), { recursive: true });
    fs.writeFileSync(host, 'module.exports = {};');
    assert.strictEqual(target.resolveWasmHost([root], path.join(root, 'missing-extension')), path.resolve(host));
} finally {
    fs.rmSync(root, { recursive: true, force: true });
}

console.log('absolute-extension wasm target smoke: OK');
