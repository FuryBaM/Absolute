/**
 * ESM entry for browser demos (re-exports the CJS implementation via a thin wrapper).
 * Browsers cannot `require()`; this module duplicates the public API in ESM form.
 */

function readCString(memory, ptr) {
    const view = new Uint8Array(memory.buffer);
    let end = Number(ptr) >>> 0;
    while (end < view.length && view[end] !== 0) end += 1;
    return new TextDecoder('utf-8').decode(view.subarray(Number(ptr) >>> 0, end));
}

function toBytes(value) {
    if (value instanceof Uint8Array) return value;
    return new TextEncoder().encode(String(value ?? ''));
}

function createMockTcpTable(options = {}) {
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
                    return allocId({ kind: 'mock-echo', buffer: new Uint8Array(0), port });
                }
                if (mock.mode === 'script' && Array.isArray(mock.responses)) {
                    return allocId({
                        kind: 'mock-script',
                        responses: mock.responses.slice(),
                        port,
                    });
                }
            }
            return -1;
        },
        listen(host, port, backlog) {
            void backlog;
            const key = mockKey(host || '127.0.0.1', port);
            if (Object.prototype.hasOwnProperty.call(mocks, key) && mocks[key].mode === 'listen-echo') {
                return allocId({ kind: 'mock-listen-echo', port, pending: [] });
            }
            return -1;
        },
        accept(handle) {
            const server = sockets.get(handle);
            if (!server || server.kind !== 'mock-listen-echo') return -1;
            return allocId({ kind: 'mock-echo', buffer: new Uint8Array(0), port: server.port });
        },
        send(handle, text) {
            const socket = sockets.get(handle);
            if (!socket) return 0;
            const data = toBytes(text || '');
            if (socket.kind === 'mock-echo') {
                const next = new Uint8Array(socket.buffer.length + data.length);
                next.set(socket.buffer, 0);
                next.set(data, socket.buffer.length);
                socket.buffer = next;
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
                if (!socket.responses.length) return new Uint8Array(0);
                const next = toBytes(String(socket.responses.shift()));
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
        shutdown() {},
    };
}

export async function prefetchHttp(urls, maxBytes = 65536) {
    const cache = Object.create(null);
    for (const url of urls || []) {
        const response = await fetch(url);
        if (!response.ok) throw new Error(`HTTP ${response.status} for ${url}`);
        const buffer = new Uint8Array(await response.arrayBuffer());
        cache[url] = buffer.subarray(0, Math.min(buffer.length, maxBytes));
    }
    return cache;
}

export function createBrowserAbsoluteImports(options = {}) {
    const capture = options.captureLogs === true;
    const logs = [];
    const decoder = new TextDecoder('utf-8');
    const encoder = new TextEncoder();
    const launchArgs = [
        String(options.executable || 'absolutec-wasm'),
        ...((options.args || []).map((value) => String(value))),
    ];
    let memory = null;
    const mocks = options.httpMocks || Object.create(null);
    const httpCache = options._httpCache || options.httpCache || Object.create(null);
    const tcp = createMockTcpTable(options);
    const onLog = typeof options.onLog === 'function' ? options.onLog : null;

    const env = {
        absolute_time_unix_nanos() { return BigInt(Date.now()) * 1000000n; },
        absolute_time_monotonic_nanos() { return BigInt(Math.floor(performance.now() * 1000000)); },
        absolute_random_entropy() {
            const values = new BigUint64Array(1);
            crypto.getRandomValues(values);
            return values[0];
        },
        absolute_process_args_count() { return launchArgs.length; },
        absolute_process_arg_copy(index, outputPtr, capacity) {
            const i = Number(index) | 0;
            const cap = Number(capacity) | 0;
            if (!memory || i < 0 || i >= launchArgs.length || cap <= 0) return -1;
            const bytes = encoder.encode(launchArgs[i]);
            const count = Math.min(bytes.length, cap - 1);
            const view = new Uint8Array(memory.buffer);
            const output = Number(outputPtr) >>> 0;
            view.set(bytes.subarray(0, count), output);
            view[output + count] = 0;
            return count;
        },
        absolute_log(ptr, len) {
            if (!memory) return;
            const view = new Uint8Array(memory.buffer, Number(ptr) >>> 0, Number(len) >>> 0);
            const text = decoder.decode(view);
            if (capture) logs.push(text);
            else if (onLog) onLog(text);
            else console.log(text.endsWith('\n') ? text.slice(0, -1) : text);
        },
        absolute_http_get(urlPtr, outPtr, outCap) {
            if (!memory) return -1;
            const url = readCString(memory, urlPtr);
            const cap = Number(outCap) | 0;
            const out = Number(outPtr) >>> 0;
            if (cap <= 0) return -1;
            let body = null;
            if (Object.prototype.hasOwnProperty.call(mocks, url)) body = toBytes(mocks[url]);
            else if (Object.prototype.hasOwnProperty.call(httpCache, url)) body = toBytes(httpCache[url]);
            else return -1;
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
        absolute_tcp_accept(handle) { return tcp.accept(Number(handle) | 0); },
        absolute_tcp_send(handle, textPtr) {
            if (!memory) return 0;
            return tcp.send(Number(handle) | 0, readCString(memory, textPtr));
        },
        absolute_tcp_receive(handle, outPtr, maxBytes) {
            if (!memory) return -1;
            const data = tcp.receive(Number(handle) | 0, Number(maxBytes) | 0);
            if (!data) return -1;
            const view = new Uint8Array(memory.buffer);
            const n = Math.min(data.length, Number(maxBytes) | 0);
            view.set(data.subarray(0, n), Number(outPtr) >>> 0);
            return n;
        },
        absolute_tcp_close(handle) { tcp.close(Number(handle) | 0); },
        absolute_tcp_port(handle) { return tcp.port(Number(handle) | 0); },
        absolute_task_pool_size() { return 0; },
        absolute_task_enqueue() { return -1; },
        absolute_task_await_job() {},
    };

    return {
        imports: { env },
        logs,
        shutdown() { try { tcp.shutdown(); } catch (_) {} },
        bind(instance) {
            memory = instance.exports.memory || null;
            if (!memory) throw new Error('Absolute wasm module did not export memory');
        },
    };
}

export async function instantiateBrowserAbsoluteWasm(bytes, options = {}) {
    if (options.prefetchUrls && options.prefetchUrls.length) {
        options = {
            ...options,
            _httpCache: {
                ...(options._httpCache || options.httpCache || Object.create(null)),
                ...(await prefetchHttp(options.prefetchUrls, options.maxHttpBytes || 65536)),
            },
        };
    }
    const host = createBrowserAbsoluteImports(options);
    const result = await WebAssembly.instantiate(bytes, host.imports);
    const instance = result.instance || result;
    host.bind(instance);
    return { instance, exports: instance.exports, logs: host.logs, host };
}

export {
    createBrowserAbsoluteImports as createAbsoluteImports,
    instantiateBrowserAbsoluteWasm as instantiateAbsoluteWasm,
    createMockTcpTable,
};
