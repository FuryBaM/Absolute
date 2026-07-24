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
 *   preferWebSocketTcp (default true when SAB + crossOriginIsolated),
 *   taskWorkers (N nested task workers; requires SharedArrayBuffer)
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

/* Task pool SAB layout (match absolute-wasm-host.js / browser-task-worker). */
const TASK_STATUS_FREE = 0;
const TASK_STATUS_READY = 1;
const TASK_STATUS_DONE = 3;
const TASK_CONTEXT_MAX = 512;
const TASK_JOB_COUNT = 32;
const TASK_JOB_STRIDE = 64 + TASK_CONTEXT_MAX;
const TASK_JOB_BASE = 64;
const TASK_WAKE_INDEX = 0;

let memory = null;
/** @type {WebAssembly.Instance | null} */
let instance = null;
/** @type {ReturnType<typeof createTcp> | null} */
let tcp = null;
/** @type {Worker | null} */
let tcpWorker = null;
/** @type {ReturnType<typeof createBrowserTaskPool> | null} */
let taskPool = null;

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

function prepareSharedMemoryImport(module, imports) {
    const memImport = WebAssembly.Module.imports(module).find(
        (entry) => entry.module === 'env' && entry.name === 'memory' && entry.kind === 'memory',
    );
    if (!memImport) return null;
    if (typeof SharedArrayBuffer === 'undefined') {
        throw new Error('shared-memory module requires SharedArrayBuffer (COOP/COEP)');
    }
    const type = memImport.type || {};
    const initial = Number(type.initial ?? type.minimum ?? 256) || 256;
    let maximum = type.maximum != null ? Number(type.maximum) : 256;
    if (!Number.isFinite(maximum) || maximum < initial) maximum = Math.max(initial, 256);
    const memoryObj = new WebAssembly.Memory({ initial, maximum, shared: true });
    if (!imports.env) imports.env = {};
    imports.env.memory = memoryObj;
    return memoryObj;
}

/**
 * @param {Uint8Array|ArrayBuffer} wasmBytes
 * @param {number} workerCount
 * @param {{ sharedMemory?: WebAssembly.Memory|null }} [poolOptions]
 */
function createBrowserTaskPool(wasmBytes, workerCount, poolOptions = {}) {
    const count = Math.max(0, Math.min(16, workerCount | 0));
    if (count <= 0 || typeof SharedArrayBuffer === 'undefined') {
        return {
            size: 0,
            mode: 'none',
            enqueue() { return -1; },
            awaitJob() {},
            shutdown() {},
            ready: Promise.resolve(),
        };
    }

    const sharedMemory = poolOptions.sharedMemory || null;
    const sharedMode = !!(sharedMemory && sharedMemory.buffer instanceof SharedArrayBuffer);
    const jobStride = sharedMode ? 64 : TASK_JOB_STRIDE;
    const sabSize = TASK_JOB_BASE + TASK_JOB_COUNT * jobStride;
    const sab = new SharedArrayBuffer(sabSize);
    const i32 = new Int32Array(sab);
    const u8 = sharedMode ? null : new Uint8Array(sab);
    let boundMemory = sharedMemory;
    const workers = [];
    const bytesCopy = wasmBytes instanceof ArrayBuffer
        ? wasmBytes.slice(0)
        : wasmBytes.buffer.slice(wasmBytes.byteOffset, wasmBytes.byteOffset + wasmBytes.byteLength);

    const workerUrl = sharedMode
        ? new URL('./absolute-wasm-browser-shared-task-worker.js', import.meta.url)
        : new URL('./absolute-wasm-browser-task-worker.js', import.meta.url);

    const readyPromises = [];
    for (let w = 0; w < count; w += 1) {
        const worker = new Worker(workerUrl);
        readyPromises.push(new Promise((resolve, reject) => {
            const timer = setTimeout(() => reject(new Error('task worker ready timeout')), 15000);
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
        }));
        const copy = bytesCopy.slice(0);
        if (sharedMode) {
            // Structured-clone shared WebAssembly.Memory (same SAB).
            worker.postMessage({
                type: 'init',
                wasmBytes: copy,
                memory: sharedMemory,
                jobSab: sab,
                jobCount: TASK_JOB_COUNT,
                jobStride,
                jobBase: TASK_JOB_BASE,
                wakeIndex: TASK_WAKE_INDEX,
            }, [copy]);
        } else {
            worker.postMessage({
                type: 'init',
                wasmBytes: copy,
                sab,
                jobCount: TASK_JOB_COUNT,
                jobStride: TASK_JOB_STRIDE,
                jobBase: TASK_JOB_BASE,
                contextMax: TASK_CONTEXT_MAX,
                wakeIndex: TASK_WAKE_INDEX,
            }, [copy]);
        }
        workers.push(worker);
    }

    function slotI32Offset(jobIndex) {
        return (TASK_JOB_BASE + jobIndex * jobStride) >> 2;
    }

    function slotBytesOffset(jobIndex) {
        return TASK_JOB_BASE + jobIndex * jobStride + 64;
    }

    return {
        size: count,
        mode: sharedMode ? 'shared' : 'isolated',
        ready: Promise.all(readyPromises),
        bindMemory(mem) {
            if (!sharedMode) boundMemory = mem;
        },
        enqueue(entry, contextPtr, contextBytes) {
            if (sharedMode) {
                for (let i = 0; i < TASK_JOB_COUNT; i += 1) {
                    const base = slotI32Offset(i);
                    if (Atomics.load(i32, base) !== TASK_STATUS_FREE) continue;
                    Atomics.store(i32, base + 1, Number(entry) | 0);
                    Atomics.store(i32, base + 2, Number(contextPtr) | 0);
                    Atomics.store(i32, base + 3, 0);
                    Atomics.store(i32, base + 4, 0);
                    Atomics.store(i32, base + 5, 0);
                    Atomics.store(i32, base, TASK_STATUS_READY);
                    Atomics.add(i32, TASK_WAKE_INDEX, 1);
                    Atomics.notify(i32, TASK_WAKE_INDEX, workers.length);
                    return i;
                }
                return -1;
            }
            if (!boundMemory) return -1;
            const requested = Math.min(Number(contextBytes) | 0, TASK_CONTEXT_MAX);
            const src = Number(contextPtr) >>> 0;
            const view = new Uint8Array(boundMemory.buffer);
            const len = Math.min(requested, Math.max(0, view.length - src));
            for (let i = 0; i < TASK_JOB_COUNT; i += 1) {
                const base = slotI32Offset(i);
                if (Atomics.load(i32, base) !== TASK_STATUS_FREE) continue;
                Atomics.store(i32, base + 1, Number(entry) | 0);
                Atomics.store(i32, base + 2, len);
                Atomics.store(i32, base + 3, 0);
                Atomics.store(i32, base + 4, 0);
                Atomics.store(i32, base + 5, 0);
                u8.set(view.subarray(src, src + len), slotBytesOffset(i));
                Atomics.store(i32, base, TASK_STATUS_READY);
                Atomics.add(i32, TASK_WAKE_INDEX, 1);
                Atomics.notify(i32, TASK_WAKE_INDEX, workers.length);
                return i;
            }
            return -1;
        },
        awaitJob(jobId, contextPtr) {
            const id = Number(jobId) | 0;
            if (id < 0 || id >= TASK_JOB_COUNT) return;
            const base = slotI32Offset(id);
            for (;;) {
                const status = Atomics.load(i32, base);
                if (status === TASK_STATUS_DONE) break;
                if (status === TASK_STATUS_FREE) return;
                const wait = Atomics.wait(i32, base, status, 30000);
                if (wait === 'timed-out' && Atomics.load(i32, base) !== TASK_STATUS_DONE) return;
            }
            if (!sharedMode && boundMemory) {
                const dst = Number(contextPtr) >>> 0;
                if (dst) {
                    const view = new Uint8Array(boundMemory.buffer);
                    view.set(u8.subarray(slotBytesOffset(id), slotBytesOffset(id) + 8), dst);
                }
            }
            Atomics.store(i32, base, TASK_STATUS_FREE);
            Atomics.notify(i32, base, 1);
        },
        shutdown() {
            for (const worker of workers) {
                try { worker.terminate(); } catch (_) { /* ignore */ }
            }
            workers.length = 0;
        },
    };
}

function createImports(options, tcpTable, pool) {
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
            absolute_task_pool_size() { return pool && pool.size > 0 ? pool.size : 0; },
            absolute_task_enqueue(entry, contextPtr, contextBytes, core, priority) {
                void core;
                void priority;
                if (!pool || pool.size <= 0) return -1;
                return pool.enqueue(entry, contextPtr, contextBytes);
            },
            absolute_task_await_job(jobId, contextPtr, contextBytes) {
                void contextBytes;
                if (!pool) return;
                pool.awaitJob(jobId, contextPtr);
            },
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
    if (taskPool) {
        try { taskPool.shutdown(); } catch (_) { /* ignore */ }
        taskPool = null;
    }
    tcp = await pickTcp(options);

    const module = await WebAssembly.compile(bytes);
    const imports = createImports(options, tcp, null);
    let importedMemory = null;
    try {
        importedMemory = prepareSharedMemoryImport(module, imports);
    } catch (error) {
        self.postMessage({ type: 'log', text: `[memory] ${error}\n` });
    }

    const wantTasks = (Number(options.taskWorkers) || 0) > 0
        && typeof SharedArrayBuffer !== 'undefined';
    if (wantTasks) {
        try {
            taskPool = createBrowserTaskPool(bytes, Number(options.taskWorkers) || 0, {
                sharedMemory: importedMemory,
            });
            // Wire pool into imports after creation (pool_size / enqueue).
            const poolImports = createImports(options, tcp, taskPool);
            imports.env.absolute_task_pool_size = poolImports.env.absolute_task_pool_size;
            imports.env.absolute_task_enqueue = poolImports.env.absolute_task_enqueue;
            imports.env.absolute_task_await_job = poolImports.env.absolute_task_await_job;
            await taskPool.ready;
        } catch (error) {
            self.postMessage({ type: 'log', text: `[tasks] pool unavailable: ${error}\n` });
            taskPool = null;
        }
    }

    const result = await WebAssembly.instantiate(module, imports);
    instance = result.instance || result;
    memory = instance.exports.memory || importedMemory || null;
    if (!memory) throw new Error('module did not provide memory');
    if (taskPool && typeof taskPool.bindMemory === 'function') {
        taskPool.bindMemory(memory);
    }
    if (taskPool && taskPool.size > 0) {
        await new Promise((r) => setTimeout(r, 50));
    }
    const names = Object.keys(instance.exports).filter((n) => !n.startsWith('__'));
    const shared = !!(memory.buffer
        && typeof SharedArrayBuffer !== 'undefined'
        && memory.buffer instanceof SharedArrayBuffer);
    self.postMessage({
        type: 'instantiated',
        exports: names,
        tcpMode: tcp.mode || 'mock',
        taskWorkers: taskPool ? taskPool.size : 0,
        taskPoolMode: taskPool ? taskPool.mode : 'none',
        sharedMemory: shared,
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
    try { if (taskPool) taskPool.shutdown(); } catch (_) { /* ignore */ }
    taskPool = null;
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
