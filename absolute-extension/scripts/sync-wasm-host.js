'use strict';

const fs = require('fs');
const path = require('path');

const source = path.resolve(__dirname, '..', '..', 'tools', 'absolute-wasm-host.js');
const destination = path.resolve(__dirname, '..', 'tools', 'absolute-wasm-host.js');
if (!fs.existsSync(source)) throw new Error(`Absolute WASM host was not found: ${source}`);
fs.mkdirSync(path.dirname(destination), { recursive: true });
fs.copyFileSync(source, destination);
console.log(`Bundled Absolute WASM host: ${destination}`);
