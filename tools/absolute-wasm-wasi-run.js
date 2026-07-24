#!/usr/bin/env node
'use strict';

/**
 * Run an Absolute module linked against the WASI runtime object
 * (absolute_wasm_runtime_wasi.o) using Node's experimental WASI preview1.
 *
 *   node tools/absolute-wasm-wasi-run.js path/to/module.wasm
 *   node tools/absolute-wasm-wasi-run.js module.wasm --arg hello --env FOO=bar
 *
 * Prefer wasmtime when available in CI; this runner keeps Windows/dev machines
 * unblocked without a separate wasmtime install.
 */

const fs = require('fs');
const path = require('path');
const { WASI } = require('wasi');

function usage() {
    console.error(`Usage:
  absolute-wasm-wasi-run <module.wasm> [--export name] [--arg value]... [--env KEY=VAL]...
`);
}

function parseArgs(argv) {
    const options = {
        module: null,
        exportName: 'main',
        args: ['absolute-wasm'],
        // Start empty: full process.env can exceed Absolute's wasm env table.
        // Opt in with --env KEY=VAL (and optionally --inherit-env).
        env: Object.create(null),
        inheritEnv: false,
    };
    const args = argv.slice();
    while (args.length) {
        const arg = args.shift();
        if (arg === '-h' || arg === '--help') {
            usage();
            process.exit(0);
        }
        if (arg === '--export') {
            options.exportName = args.shift() || 'main';
            continue;
        }
        if (arg === '--arg') {
            options.args.push(args.shift() || '');
            continue;
        }
        if (arg === '--inherit-env') {
            options.inheritEnv = true;
            continue;
        }
        if (arg === '--env') {
            const raw = args.shift() || '';
            const eq = raw.indexOf('=');
            if (eq <= 0) throw new Error('--env expects KEY=VAL');
            options.env[raw.slice(0, eq)] = raw.slice(eq + 1);
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
    if (options.inheritEnv) {
        options.env = { ...process.env, ...options.env };
    }
    return options;
}

async function instantiateWasiAbsoluteWasm(bytes, options = {}) {
    const wasi = new WASI({
        version: 'preview1',
        args: options.args || ['absolute-wasm'],
        env: options.env || process.env,
        returnOnExit: true,
    });
    const importObject = typeof wasi.getImportObject === 'function'
        ? wasi.getImportObject()
        : { wasi_snapshot_preview1: wasi.wasiImport };
    const result = await WebAssembly.instantiate(bytes, importObject);
    const instance = result.instance || result;
    // Reactor modules (--no-entry): Node requires wasi.initialize() + `_initialize` export
    // before wasi_snapshot_preview1 imports may be invoked.
    if (typeof wasi.initialize === 'function') {
        wasi.initialize(instance);
    } else if (typeof instance.exports._start === 'function' && typeof wasi.start === 'function') {
        wasi.start(instance);
    }
    return { instance, exports: instance.exports, wasi };
}

async function main() {
    const options = parseArgs(process.argv.slice(2));
    const modulePath = path.resolve(options.module);
    if (!fs.existsSync(modulePath)) {
        console.error(`module not found: ${modulePath}`);
        process.exit(1);
    }
    const bytes = fs.readFileSync(modulePath);
    const { exports } = await instantiateWasiAbsoluteWasm(bytes, {
        args: options.args,
        env: options.env,
    });
    const fn = exports[options.exportName];
    if (typeof fn !== 'function') {
        console.error(`export '${options.exportName}' not found; available:`,
            Object.keys(exports).filter((n) => !n.startsWith('__')));
        process.exit(2);
    }
    const code = fn();
    if (typeof code === 'number') process.exitCode = code | 0;
}

module.exports = {
    instantiateWasiAbsoluteWasm,
};

if (require.main === module) {
    main().catch((error) => {
        console.error(error);
        process.exit(1);
    });
}
