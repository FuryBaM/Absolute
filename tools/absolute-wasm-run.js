#!/usr/bin/env node
'use strict';

/**
 * Run an Absolute .wasm module: call main() if present, print host logs.
 *
 *   node tools/absolute-wasm-run.js path/to/module.wasm
 *   node tools/absolute-wasm-run.js module.wasm --export wasm_add --args 20,22
 */

const fs = require('fs');
const path = require('path');
const { instantiateAbsoluteWasm } = require('./absolute-wasm-host.js');

function usage() {
    console.error(`Usage:
  absolute-wasm-run <module.wasm> [--export name] [--args a,b,c] [--http-mock url=body]
`);
}

function parseArgs(argv) {
    const options = {
        module: null,
        exportName: 'main',
        args: [],
        httpMocks: Object.create(null),
    };
    const args = argv.slice();
    while (args.length) {
        const arg = args.shift();
        if (arg === '-h' || arg === '--help') {
            usage();
            process.exit(0);
        }
        if (arg === '--export') {
            options.exportName = args.shift();
            continue;
        }
        if (arg === '--args') {
            const raw = args.shift() || '';
            options.args = raw.split(',').filter(Boolean).map((part) => {
                if (/^-?\d+$/.test(part)) return Number(part);
                return part;
            });
            continue;
        }
        if (arg === '--http-mock') {
            const raw = args.shift() || '';
            const eq = raw.indexOf('=');
            if (eq <= 0) throw new Error('--http-mock expects url=body');
            options.httpMocks[raw.slice(0, eq)] = raw.slice(eq + 1);
            continue;
        }
        if (arg.startsWith('-')) throw new Error(`unknown option ${arg}`);
        if (options.module) throw new Error('only one module path is supported');
        options.module = arg;
    }
    if (!options.module) {
        usage();
        process.exit(2);
    }
    return options;
}

async function main() {
    const options = parseArgs(process.argv.slice(2));
    const modulePath = path.resolve(options.module);
    if (!fs.existsSync(modulePath)) {
        console.error(`module not found: ${modulePath}`);
        process.exit(1);
    }
    const bytes = fs.readFileSync(modulePath);
    const { exports, logs } = await instantiateAbsoluteWasm(bytes, {
        captureLogs: false,
        httpMocks: options.httpMocks,
    });
    const fn = exports[options.exportName];
    if (typeof fn !== 'function') {
        console.error(`export '${options.exportName}' not found; available:`,
            Object.keys(exports).filter((n) => !n.startsWith('__')));
        process.exit(2);
    }
    const result = fn(...options.args);
    if (typeof result === 'number' && options.exportName === 'main') {
        process.exitCode = result | 0;
    } else if (result !== undefined) {
        console.log(String(result));
    }
    void logs;
}

main().catch((error) => {
    console.error(error);
    process.exit(1);
});
