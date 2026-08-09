'use strict';

const fs = require('fs');
const path = require('path');

const source = path.resolve(__dirname, '..', '..', 'tools', 'absolute-wasm-host.js');
const destination = path.resolve(__dirname, '..', 'tools', 'absolute-wasm-host.js');
if (!fs.existsSync(source)) throw new Error(`Absolute WASM host was not found: ${source}`);
fs.mkdirSync(path.dirname(destination), { recursive: true });
fs.copyFileSync(source, destination);
console.log(`Bundled Absolute WASM host: ${destination}`);

const debugSourceDirectory = path.resolve(__dirname, '..', '..', 'debugging');
const debugDestinationDirectory = path.resolve(__dirname, '..', 'debugging');
const debugFiles = [
    'Absolute.natvis',
    'absolute_gdb.py',
    'absolute_debug_types.h'
];
fs.mkdirSync(debugDestinationDirectory, { recursive: true });
for (const file of debugFiles) {
    const debugSource = path.join(debugSourceDirectory, file);
    const debugDestination = path.join(debugDestinationDirectory, file);
    if (!fs.existsSync(debugSource))
        throw new Error(`Absolute debugger asset was not found: ${debugSource}`);
    fs.copyFileSync(debugSource, debugDestination);
    console.log(`Bundled Absolute debugger asset: ${debugDestination}`);
}
