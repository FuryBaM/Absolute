/**
 * Browser session worker: runs Absolute wasm off the UI thread.
 *
 * Messages (in):
 *   { type: 'instantiate', bytes, options }
 *   { type: 'call', id, exportName, args }
 *   { type: 'shutdown' }
 *
 * Messages (out):
 *   { type: 'ready' }
 *   { type: 'log', text }
 *   { type: 'instantiated', exports }
 *   { type: 'result', id, ok, value?, error? }
 *   { type: 'error', error }
 *
 * options:
 *   httpMocks, tcpMocks, wsMap (host:port -> ws URL),
 *   preferWebSocketTcp (default true when SAB + crossOriginIsolated)
 */

/* eslint-disable no-restricted-globals */

const OP_CONNECT = 1;
const OP_LISTEN = 2;
const OP_ACCEPT = 3;
const OP_SEND = 4;
const OP_RECEIVE = 5;
const OP_CLOSE = 6;
const OP_PORT = 7;
const OP_SHUTDOWN = 8;

let memory = null;
/** @type {WebAssembly.Instance | null} */
let instance = null;
/** @type {ReturnType<typeof createTcp> | null} */
let tcp = null;
/** @type {Worker | null} */
let tcpWorker = null;

function readCString(ptr) {
    const view = new Uint8Array(memory.buffer);
    let end = Number(ptr) >>> 0;
    while (end < view.length && view[end] !== 0) end += 1;
    return new TextDecoder().decode(view.subarray(Number(ptr) >>> 0, end));
}

function toBytes(value) {
    if (value instanceof Uint8Array) return value;
    return new TextEncoder().encode(String(value ?? ''));
}

function createMockTcp(options = {}) {
    const sockets = new Map();
    let nextId = 1;
    const mocks = options.tcpMocks || Object.create(null);
    const keyOf = (host, port) => `${host}:${port}`;
    const alloc = (rec) => {
        const id = nextId++;
        sockets.set(id, rec);
        return id;
    };
    return {
        connect(host, port) {
            const key = keyOf(host, port);
            if (!Object.prototype.hasOwnProperty.call(mocks, key)) return -1;
            const mock = mocks[key];
            if (mock.mode === 'echo' || mock.echo) {
                return alloc({ kind: 'echo', buffer: new Uint8Array(0), port });
            }
            if (mock.mode === 'script' && Array.isArray(mock.responses)) {
                return alloc({ kind: 'script', responses: mock.responses.slice(), port });
            }
            return -1;
        },
        listen() { return -1; },
        accept() { return -1; },
        send(handle, text) {
            const s = sockets.get(handle);
            if (!s) return 0;
            const data = toBytes(text || '');
            if (s.kind === 'echo') {
                const next = new Uint8Array(s.buffer.length + data.length);
                next.set(s.buffer, 0);
                next.set(data, s.buffer.length);
                s.buffer = next;
                return data.length;
            }
            if (s.kind === 'script') return data.length;
            return 0;
        },
        receive(handle, maxBytes) {
            const s = sockets.get(handle);
            if (!s) return null;
            if (s.kind === 'echo') {
                const n = Math.min(s.buffer.length, maxBytes);
                const out = s.buffer.subarray(0, n);
                s.buffer = s.buffer.subarray(n);
                return out;
            }
            if (s.kind === 'script') {
                if (!s.responses.length) return new Uint8Array(0);
                return toBytes(String(s.responses.shift())).subarray(0, maxBytes);
            }
            return null;
        },
        port(handle) {
            const s = sockets.get(handle);
            return s && typeof s.port === 'number' ? s.port : -1;
        },
        close(handle) { sockets.delete(handle); },
        shutdown() { sockets.clear(); },
        mode: 'mock',
    };
}

function createWebSocketTcpBridge(wsMap) {
    if (typeof SharedArrayBuffer === 'undefined') {
        throw new Error('SharedArrayBuffer unavailable (need COOP/COEP)');
    }
    const sab = new SharedArrayBuffer(64 + 65536);
    const i32 = new Int32Array(sab, 0, 16);
    const bytes = new Uint8Array(sab, 64);
    const worker = new Worker(new URL('./absolute-wasm-ws-tcp-worker.js', import.meta.url));
    tcpWorker = worker;

    const ready = new Promise((resolve, reject) => {
        const timer = setTimeout(() => reject(new Error('ws tcp worker timeout')), 10000);
        worker.onmessage = (event) => {
            if (event.data && event.data.type === 'ready') {
                clearTimeout(timer);
                resolve();
            }
        };
        worker.onerror = (err) => {
            clearTimeout(timer);
            reject(err);
        };
        worker.postMessage({ type: 'init', sab, wsMap: wsMap || {} });
    });

    function call(op, arg0, arg1, payload) {
        const data = toBytes(payload || '');
        const n = Math.min(data.length, bytes.length);
        bytes.set(data.subarray(0, n), 0);
        i32[1] = op;
        i32[2] = arg0 | 0;
        i32[3] = arg1 | 0;
        i32[4] = -1;
        i32[5] = n;
        Atomics.store(i32, 0, 1);
        Atomics.notify(i32, 0, 1);
        while (Atomics.load(i32, 0) === 1) {
            const wait = Atomics.wait(i32, 0, 1, 15000);
            if (wait === 'timed-out') {
                Atomics.store(i32, 0, 0);
                Atomics.notify(i32, 0, 1);
                return { result: -1, payload: new Uint8Array(0) };
            }
        }
        const result = i32[4] | 0;
        const plen = i32[5] | 0;
        const out = bytes.slice(0, Math.max(0, plen));
        Atomics.store(i32, 0, 0);
        Atomics.notify(i32, 0, 1);
        return { result, payload: out };
    }

    return {
        mode: 'websocket',
        ready,
        connect(host, port) { return call(OP_CONNECT, port, 0, host).result; },
        listen() { return -1; },
        accept() { return -1; },
        send(handle, text) { return call(OP_SEND, handle, 0, text || '').result; },
        receive(handle, maxBytes) {
            const { result, payload } = call(OP_RECEIVE, handle, maxBytes, '');
            if (result < 0) return null;
            return payload;
        },
        port(handle) { return call(OP_PORT, handle, 0, '').result; },
        close(handle) { call(OP_CLOSE, handle, 0, ''); },
        shutdown() {
            try { call(OP_SHUTDOWN, 0, 0, ''); } catch (_) { /* ignore */ }
            try { worker.terminate(); } catch (_) { /* ignore */ }
            tcpWorker = null;
        },
    };
}

function createImports(options, tcpTable) {
    const httpMocks = options.httpMocks || Object.create(null);
    const httpCache = options.httpCache || Object.create(null);

    return {
        env: {
            absolute_log(ptr, len) {
                if (!memory) return;
                const view = new Uint8Array(memory.buffer, Number(ptr) >>> 0, Number(len) >>> 0);
                const text = new TextDecoder().decode(view);
                self.postMessage({ type: 'log', text });
            },
            absolute_http_get(urlPtr, outPtr, outCap) {
                if (!memory) return -1;
                const url = readCString(urlPtr);
                const cap = Number(outCap) | 0;
                const out = Number(outPtr) >>> 0;
                if (cap <= 0) return -1;
                let body = null;
                if (Object.prototype.hasOwnProperty.call(httpMocks, url)) body = toBytes(httpMocks[url]);
                else if (Object.prototype.hasOwnProperty.call(httpCache, url)) body = toBytes(httpCache[url]);
                else return -1;
                const view = new Uint8Array(memory.buffer);
                const n = Math.min(body.length, cap);
                view.set(body.subarray(0, n), out);
                return n;
            },
            absolute_tcp_connect(hostPtr, port) {
                if (!memory) return -1;
                return tcpTable.connect(readCString(hostPtr), Number(port) | 0);
            },
            absolute_tcp_listen(hostPtr, port, backlog) {
                if (!memory) return -1;
                const host = hostPtr ? readCString(hostPtr) : '127.0.0.1';
                return tcpTable.listen(host, Number(port) | 0, Number(backlog) | 0);
            },
            absolute_tcp_accept(handle) { return tcpTable.accept(Number(handle) | 0); },
            absolute_tcp_send(handle, textPtr) {
                if (!memory) return 0;
                return tcpTable.send(Number(handle) | 0, readCString(textPtr));
            },
            absolute_tcp_receive(handle, outPtr, maxBytes) {
                if (!memory) return -1;
                const data = tcpTable.receive(Number(handle) | 0, Number(maxBytes) | 0);
                if (!data) return -1;
                const view = new Uint8Array(memory.buffer);
                const n = Math.min(data.length, Number(maxBytes) | 0);
                view.set(data.subarray(0, n), Number(outPtr) >>> 0);
                return n;
            },
            absolute_tcp_close(handle) { tcpTable.close(Number(handle) | 0); },
            absolute_tcp_port(handle) { return tcpTable.port(Number(handle) | 0); },
            absolute_task_pool_size() { return 0; },
            absolute_task_enqueue() { return -1; },
            absolute_task_await_job() {},
        },
    };
}

async function pickTcp(options) {
    const mocks = options.tcpMocks || Object.create(null);
    const hasMocks = Object.keys(mocks).length > 0;
    const wantWs = options.preferWebSocketTcp !== false
        && options.wsMap
        && Object.keys(options.wsMap).length > 0
        && typeof SharedArrayBuffer !== 'undefined'
        && (typeof crossOriginIsolated === 'undefined' || crossOriginIsolated);

    if (wantWs && !hasMocks) {
        try {
            const bridge = createWebSocketTcpBridge(options.wsMap);
            await bridge.ready;
            return bridge;
        } catch (error) {
            self.postMessage({ type: 'log', text: `[tcp] websocket bridge unavailable: ${error}\n` });
        }
    }
    return createMockTcp(options);
}

async function instantiate(bytes, options = {}) {
    if (tcp) {
        try { tcp.shutdown(); } catch (_) { /* ignore */ }
    }
    tcp = await pickTcp(options);
    const imports = createImports(options, tcp);
    const result = await WebAssembly.instantiate(bytes, imports);
    instance = result.instance || result;
    memory = instance.exports.memory || null;
    if (!memory) throw new Error('module did not export memory');
    const names = Object.keys(instance.exports).filter((n) => !n.startsWith('__'));
    self.postMessage({
        type: 'instantiated',
        exports: names,
        tcpMode: tcp.mode || 'mock',
        crossOriginIsolated: typeof crossOriginIsolated !== 'undefined' ? crossOriginIsolated : null,
    });
}

function callExport(exportName, args) {
    if (!instance) throw new Error('not instantiated');
    const fn = instance.exports[exportName];
    if (typeof fn !== 'function') throw new Error(`missing export ${exportName}`);
    return fn(...(args || []));
}

function shutdown() {
    try { if (tcp) tcp.shutdown(); } catch (_) { /* ignore */ }
    tcp = null;
    instance = null;
    memory = null;
    if (tcpWorker) {
        try { tcpWorker.terminate(); } catch (_) { /* ignore */ }
        tcpWorker = null;
    }
}

self.onmessage = async (event) => {
    const msg = event.data || {};
    try {
        if (msg.type === 'instantiate') {
            const bytes = msg.bytes instanceof ArrayBuffer
                ? new Uint8Array(msg.bytes)
                : new Uint8Array(msg.bytes);
            await instantiate(bytes, msg.options || {});
        } else if (msg.type === 'call') {
            try {
                const value = callExport(msg.exportName || 'main', msg.args || []);
                self.postMessage({ type: 'result', id: msg.id, ok: true, value });
            } catch (error) {
                self.postMessage({
                    type: 'result',
                    id: msg.id,
                    ok: false,
                    error: String(error && error.stack ? error.stack : error),
                });
            }
        } else if (msg.type === 'shutdown') {
            shutdown();
            self.postMessage({ type: 'shutdown-done' });
        }
    } catch (error) {
        self.postMessage({ type: 'error', error: String(error && error.stack ? error.stack : error) });
    }
};

self.postMessage({ type: 'ready' });
