'use strict';

/**
 * Embedder helpers for Absolute wasm modules.
 *
 * Imports:
 *   env.absolute_log(ptr, len)           — console UTF-8
 *   env.absolute_http_get(url, out, cap) — host HTTP GET into wasm memory
 *
 * Usage:
 *   const { instantiateAbsoluteWasm } = require('./absolute-wasm-host.js');
 *   const { exports, logs } = await instantiateAbsoluteWasm(bytes, { captureLogs: true });
 */

const http = require('http');
const https = require('https');

function readCString(memory, ptr) {
    const view = new Uint8Array(memory.buffer);
    let end = Number(ptr) >>> 0;
    while (end < view.length && view[end] !== 0) end += 1;
    return new TextDecoder('utf-8').decode(view.subarray(Number(ptr) >>> 0, end));
}

function httpGetSync(url, maxBytes) {
    // Prefer mock table for deterministic tests.
    return new Promise((resolve, reject) => {
        let parsed;
        try {
            parsed = new URL(url);
        } catch (error) {
            reject(error);
            return;
        }
        const lib = parsed.protocol === 'http:' ? http : https;
        const request = lib.get(url, { timeout: 10000 }, (response) => {
            if (response.statusCode && response.statusCode >= 300 && response.statusCode < 400 && response.headers.location) {
                httpGetSync(response.headers.location, maxBytes).then(resolve, reject);
                response.resume();
                return;
            }
            const chunks = [];
            let total = 0;
            response.on('data', (chunk) => {
                if (total >= maxBytes) return;
                const slice = chunk.length + total > maxBytes
                    ? chunk.subarray(0, maxBytes - total)
                    : chunk;
                chunks.push(slice);
                total += slice.length;
            });
            response.on('end', () => resolve(Buffer.concat(chunks, total)));
            response.on('error', reject);
        });
        request.on('error', reject);
        request.on('timeout', () => {
            request.destroy();
            reject(new Error('timeout'));
        });
    });
}

function createAbsoluteImports(options = {}) {
    const capture = options.captureLogs === true;
    const logs = [];
    const decoder = new TextDecoder('utf-8');
    /** @type {WebAssembly.Memory | null} */
    let memory = null;
    const mocks = options.httpMocks || Object.create(null);

    const env = {
        absolute_log(ptr, len) {
            if (!memory) return;
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

        /**
         * Sync-looking import; Absolute calls it from wasm. We cannot truly block
         * the wasm thread on async I/O, so this uses a pre-seeded mock table or
         * returns -1 unless options.allowNetwork is set with a deasync-free
         * strategy. For tests, always prefer httpMocks.
         *
         * Signature: (url_ptr, out_ptr, out_cap) -> i32 bytes_written_or_-1
         */
        absolute_http_get(urlPtr, outPtr, outCap) {
            if (!memory) return -1;
            const url = readCString(memory, urlPtr);
            const cap = Number(outCap) | 0;
            const out = Number(outPtr) >>> 0;
            if (cap <= 0) return -1;

            let body;
            if (Object.prototype.hasOwnProperty.call(mocks, url)) {
                body = Buffer.from(String(mocks[url]), 'utf8');
            } else if (options.allowNetwork) {
                // Best-effort: only works if the host pre-resolved via prepareHttp.
                if (options._httpCache && Object.prototype.hasOwnProperty.call(options._httpCache, url)) {
                    body = options._httpCache[url];
                } else {
                    return -1;
                }
            } else {
                return -1;
            }

            const view = new Uint8Array(memory.buffer);
            const n = Math.min(body.length, cap);
            view.set(body.subarray(0, n), out);
            return n;
        },
    };

    return {
        imports: { env },
        logs,
        bind(instance) {
            memory = instance.exports.memory || null;
            if (!memory) throw new Error('Absolute wasm module did not export memory');
        },
    };
}

/**
 * Optionally prefetch URLs so absolute_http_get can serve them synchronously.
 * @param {string[]} urls
 * @param {number} [maxBytes]
 */
async function prepareHttp(urls, maxBytes = 65536) {
    const cache = Object.create(null);
    for (const url of urls || []) {
        cache[url] = await httpGetSync(url, maxBytes);
    }
    return cache;
}

async function instantiateAbsoluteWasm(bytes, options = {}) {
    if (options.allowNetwork && options.prefetchUrls && options.prefetchUrls.length) {
        options._httpCache = await prepareHttp(options.prefetchUrls, options.maxHttpBytes || 65536);
    }
    const host = createAbsoluteImports(options);
    const result = await WebAssembly.instantiate(bytes, host.imports);
    const instance = result.instance || result;
    host.bind(instance);
    return { instance, exports: instance.exports, logs: host.logs, host };
}

module.exports = {
    createAbsoluteImports,
    instantiateAbsoluteWasm,
    prepareHttp,
    httpGetSync,
};
