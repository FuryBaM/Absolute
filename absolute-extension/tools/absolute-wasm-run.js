'use strict';

const fs = require('fs');

async function main() {
    const [, , hostPath, wasmPath, ...args] = process.argv;
    if (!hostPath || !wasmPath) {
        console.error('Usage: node absolute-wasm-run.js <absolute-wasm-host.js> <module.wasm> [args...]');
        process.exitCode = 2;
        return;
    }
    const { instantiateAbsoluteWasm } = require(hostPath);
    const bytes = fs.readFileSync(wasmPath);
    const result = await instantiateAbsoluteWasm(bytes, {
        executable: wasmPath,
        args,
        allowNetwork: true
    });
    try {
        if (typeof result.exports.main !== 'function') throw new Error('main export missing');
        const code = result.exports.main();
        process.exitCode = Number(code) | 0;
    } finally {
        try { result.host.shutdown(); } catch (_) { }
    }
}

main().catch(error => {
    console.error(error && error.stack ? error.stack : String(error));
    process.exitCode = 1;
});
