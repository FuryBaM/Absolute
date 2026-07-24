'use strict';

/**
 * Embedder helpers for Absolute wasm modules.
 *
 * env imports:
 *   absolute_log, absolute_http_get,
 *   absolute_tcp_connect/listen/accept/send/receive/close/port
 */

const http = require('http');
const https = require('https');
const net = require('net');

function readCString(memory, ptr) {
    const view = new Uint8Array(memory.buffer);
    let end = Number(ptr) >>> 0;
    while (end < view.length && view[end] !== 0) end += 1;
    return new TextDecoder('utf-8').decode(view.subarray(Number(ptr) >>> 0, end));
}

function httpGetSync(url, maxBytes) {
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

function createTcpTable(options = {}) {
    /** @type {Map<number, any>} */
    const sockets = new Map();
    let nextId = 1;
    const mocks = options.tcpMocks || Object.create(null);

    function mockKey(host, port) {
        return `${host}:${port}`;
    }

    function allocId(record) {
        const id = nextId++;
        sockets.set(id, record);
        return id;
    }

    return {
        connect(host, port) {
            const key = mockKey(host, port);
            if (Object.prototype.hasOwnProperty.call(mocks, key)) {
                const mock = mocks[key];
                if (mock.mode === 'echo' || mock.echo) {
                    return allocId({ kind: 'mock-echo', buffer: Buffer.alloc(0), port });
                }
                if (mock.mode === 'script' && Array.isArray(mock.responses)) {
                    return allocId({
                        kind: 'mock-script',
                        responses: mock.responses.slice(),
                        port,
                    });
                }
            }
            if (!options.allowNetwork) return -1;
            // Real connect is async; for determinism require mocks in tests.
            return -1;
        },

        listen(host, port, backlog) {
            void backlog;
            const key = mockKey(host || '127.0.0.1', port);
            if (Object.prototype.hasOwnProperty.call(mocks, key) && mocks[key].mode === 'listen-echo') {
                return allocId({
                    kind: 'mock-listen-echo',
                    port,
                    pending: [],
                });
            }
            if (!options.allowNetwork) return -1;
            return -1;
        },

        accept(handle) {
            const server = sockets.get(handle);
            if (!server || server.kind !== 'mock-listen-echo') return -1;
            // Create a peer echo client for each accept.
            return allocId({ kind: 'mock-echo', buffer: Buffer.alloc(0), port: server.port });
        },

        send(handle, text) {
            const socket = sockets.get(handle);
            if (!socket) return 0;
            const data = Buffer.from(String(text || ''), 'utf8');
            if (socket.kind === 'mock-echo') {
                socket.buffer = Buffer.concat([socket.buffer, data]);
                return data.length;
            }
            if (socket.kind === 'mock-script') {
                socket.lastSend = data;
                return data.length;
            }
            return 0;
        },

        receive(handle, maxBytes) {
            const socket = sockets.get(handle);
            if (!socket) return null;
            if (socket.kind === 'mock-echo') {
                const n = Math.min(socket.buffer.length, maxBytes);
                const out = socket.buffer.subarray(0, n);
                socket.buffer = socket.buffer.subarray(n);
                return out;
            }
            if (socket.kind === 'mock-script') {
                if (!socket.responses.length) return Buffer.alloc(0);
                const next = Buffer.from(String(socket.responses.shift()), 'utf8');
                return next.subarray(0, maxBytes);
            }
            return null;
        },

        port(handle) {
            const socket = sockets.get(handle);
            return socket && typeof socket.port === 'number' ? socket.port : -1;
        },

        close(handle) {
            sockets.delete(handle);
        },
    };
}

function createAbsoluteImports(options = {}) {
    const capture = options.captureLogs === true;
    const logs = [];
    const decoder = new TextDecoder('utf-8');
    /** @type {WebAssembly.Memory | null} */
    let memory = null;
    const mocks = options.httpMocks || Object.create(null);
    const tcp = createTcpTable(options);

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

        absolute_http_get(urlPtr, outPtr, outCap) {
            if (!memory) return -1;
            const url = readCString(memory, urlPtr);
            const cap = Number(outCap) | 0;
            const out = Number(outPtr) >>> 0;
            if (cap <= 0) return -1;
            let body;
            if (Object.prototype.hasOwnProperty.call(mocks, url)) {
                body = Buffer.from(String(mocks[url]), 'utf8');
            } else if (options._httpCache && Object.prototype.hasOwnProperty.call(options._httpCache, url)) {
                body = options._httpCache[url];
            } else {
                return -1;
            }
            const view = new Uint8Array(memory.buffer);
            const n = Math.min(body.length, cap);
            view.set(body.subarray(0, n), out);
            return n;
        },

        absolute_tcp_connect(hostPtr, port) {
            if (!memory) return -1;
            return tcp.connect(readCString(memory, hostPtr), Number(port) | 0);
        },
        absolute_tcp_listen(hostPtr, port, backlog) {
            if (!memory) return -1;
            const host = hostPtr ? readCString(memory, hostPtr) : '127.0.0.1';
            return tcp.listen(host, Number(port) | 0, Number(backlog) | 0);
        },
        absolute_tcp_accept(handle) {
            return tcp.accept(Number(handle) | 0);
        },
        absolute_tcp_send(handle, textPtr) {
            if (!memory) return 0;
            return tcp.send(Number(handle) | 0, readCString(memory, textPtr));
        },
        absolute_tcp_receive(handle, outPtr, maxBytes) {
            if (!memory) return -1;
            const data = tcp.receive(Number(handle) | 0, Number(maxBytes) | 0);
            if (!data) return -1;
            const view = new Uint8Array(memory.buffer);
            const out = Number(outPtr) >>> 0;
            const n = Math.min(data.length, Number(maxBytes) | 0);
            view.set(data.subarray(0, n), out);
            return n;
        },
        absolute_tcp_close(handle) {
            tcp.close(Number(handle) | 0);
        },
        absolute_tcp_port(handle) {
            return tcp.port(Number(handle) | 0);
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
    createTcpTable,
};
