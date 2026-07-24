'use strict';

/**
 * Embedder helpers for Absolute wasm modules (env.absolute_log import).
 *
 * Usage (Node):
 *   const { instantiateAbsoluteWasm } = require('./absolute-wasm-host.js');
 *   const { instance, logs } = await instantiateAbsoluteWasm(bytes);
 */

function createAbsoluteImports(options = {}) {
    const capture = options.captureLogs === true;
    const logs = [];
    const decoder = new TextDecoder('utf-8');
    /** @type {WebAssembly.Memory | null} */
    let memory = null;

    const env = {
        /**
         * @param {number} ptr
         * @param {number} len
         */
        absolute_log(ptr, len) {
            if (!memory) {
                // Called before memory is wired (should not happen for Absolute modules).
                return;
            }
            const start = Number(ptr) >>> 0;
            const length = Number(len) >>> 0;
            const view = new Uint8Array(memory.buffer, start, length);
            const text = decoder.decode(view);
            if (capture) logs.push(text);
            else if (typeof options.onLog === 'function') options.onLog(text);
            else if (typeof process !== 'undefined' && process.stdout && process.stdout.write) {
                process.stdout.write(text.endsWith('\n') ? text : `${text}\n`);
            } else if (typeof console !== 'undefined' && console.log) {
                console.log(text.endsWith('\n') ? text.slice(0, -1) : text);
            }
        },
    };

    return {
        imports: { env },
        logs,
        /**
         * @param {WebAssembly.Instance} instance
         */
        bind(instance) {
            memory = instance.exports.memory || null;
            if (!memory) {
                throw new Error('Absolute wasm module did not export memory');
            }
        },
    };
}

/**
 * @param {Buffer|ArrayBuffer|Uint8Array} bytes
 * @param {{ captureLogs?: boolean, onLog?: (text: string) => void }} [options]
 */
async function instantiateAbsoluteWasm(bytes, options = {}) {
    const host = createAbsoluteImports(options);
    const result = await WebAssembly.instantiate(bytes, host.imports);
    const instance = result.instance || result;
    host.bind(instance);
    return { instance, exports: instance.exports, logs: host.logs, host };
}

module.exports = {
    createAbsoluteImports,
    instantiateAbsoluteWasm,
};
