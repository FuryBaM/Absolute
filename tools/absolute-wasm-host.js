'use strict';

/**
 * Embedder helpers for Absolute wasm modules.
 *
 * env imports:
 *   absolute_log, absolute_http_get,
 *   absolute_tcp_connect/listen/accept/send/receive/close/port,
 *   absolute_task_pool_size / absolute_task_enqueue / absolute_task_await_job
 */

const http = require('http');
const https = require('https');
const path = require('path');
const { Worker } = require('worker_threads');

const OP_CONNECT = 1;
const OP_LISTEN = 2;
const OP_ACCEPT = 3;
const OP_SEND = 4;
const OP_RECEIVE = 5;
const OP_CLOSE = 6;
const OP_PORT = 7;
const OP_SHUTDOWN = 8;

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

/* Task pool SAB layout (must match absolute-wasm-task-worker.js). */
const TASK_STATUS_FREE = 0;
const TASK_STATUS_READY = 1;
const TASK_STATUS_RUNNING = 2;
const TASK_STATUS_DONE = 3;
const TASK_CONTEXT_MAX = 512;
const TASK_JOB_COUNT = 32;
const TASK_JOB_STRIDE = 64 + TASK_CONTEXT_MAX; // header + payload
const TASK_JOB_BASE = 64;
const TASK_WAKE_INDEX = 0; // i32 index in control header

function createTaskPool(wasmBytes, workerCount) {
    const count = Math.max(0, Math.min(16, workerCount | 0));
    if (count <= 0) {
        return {
            size: 0,
            enqueue() { return -1; },
            awaitJob() {},
            shutdown() {},
        };
    }

    const sabSize = TASK_JOB_BASE + TASK_JOB_COUNT * TASK_JOB_STRIDE;
    const sab = new SharedArrayBuffer(sabSize);
    const i32 = new Int32Array(sab);
    const u8 = new Uint8Array(sab);
    /** @type {WebAssembly.Memory | null} */
    let memory = null;

    const workers = [];
    for (let w = 0; w < count; w += 1) {
        const worker = new Worker(path.join(__dirname, 'absolute-wasm-task-worker.js'), {
            workerData: {
                wasmBytes: Buffer.from(wasmBytes),
                sab,
                jobCount: TASK_JOB_COUNT,
                jobStride: TASK_JOB_STRIDE,
                jobBase: TASK_JOB_BASE,
                contextMax: TASK_CONTEXT_MAX,
                wakeIndex: TASK_WAKE_INDEX,
            },
        });
        if (typeof worker.unref === 'function') worker.unref();
        worker.on('error', (error) => {
            if (typeof console !== 'undefined') console.error('absolute wasm task worker error', error);
        });
        workers.push(worker);
    }

    function slotI32Offset(jobIndex) {
        return (TASK_JOB_BASE + jobIndex * TASK_JOB_STRIDE) >> 2;
    }

    function slotBytesOffset(jobIndex) {
        return TASK_JOB_BASE + jobIndex * TASK_JOB_STRIDE + 64;
    }

    function bindMemory(mem) {
        memory = mem;
    }

    function enqueue(entry, contextPtr, contextBytes, core, priority) {
        if (!memory) return -1;
        // Cap the copy to stay inside wasm memory; workers only need arg slots.
        const requested = Math.min(Number(contextBytes) | 0, TASK_CONTEXT_MAX);
        const src = Number(contextPtr) >>> 0;
        const view = new Uint8Array(memory.buffer);
        const len = Math.min(requested, Math.max(0, view.length - src));
        for (let i = 0; i < TASK_JOB_COUNT; i += 1) {
            const base = slotI32Offset(i);
            // Only the main thread enqueues: fill the free slot, then publish READY.
            if (Atomics.load(i32, base) !== TASK_STATUS_FREE) continue;
            Atomics.store(i32, base + 1, Number(entry) | 0);
            Atomics.store(i32, base + 2, len);
            Atomics.store(i32, base + 3, Number(core) | 0);
            Atomics.store(i32, base + 4, Number(priority) | 0);
            Atomics.store(i32, base + 5, 0);
            u8.set(view.subarray(src, src + len), slotBytesOffset(i));
            Atomics.store(i32, base, TASK_STATUS_READY);
            Atomics.add(i32, TASK_WAKE_INDEX, 1);
            Atomics.notify(i32, TASK_WAKE_INDEX, workers.length);
            return i;
        }
        return -1; // pool full
    }

    function awaitJob(jobId, contextPtr, contextBytes) {
        if (!memory) return;
        const id = Number(jobId) | 0;
        if (id < 0 || id >= TASK_JOB_COUNT) return;
        const base = slotI32Offset(id);
        // Wait until worker marks done (3). Also accept free if already reaped.
        for (;;) {
            const status = Atomics.load(i32, base);
            if (status === TASK_STATUS_DONE) break;
            if (status === TASK_STATUS_FREE) return;
            const wait = Atomics.wait(i32, base, status, 30000);
            if (wait === 'timed-out' && Atomics.load(i32, base) !== TASK_STATUS_DONE) {
                // Leave slot for debugging; caller may see empty/partial context.
                return;
            }
        }
        // Codegen stores the task result in i64 slot 0 of the context. Copy only
        // that slot back — writing CONTEXT_MAX bytes would smash neighboring
        // heap objects (other task contexts / WasmTask headers).
        const dst = Number(contextPtr) >>> 0;
        if (dst) {
            const view = new Uint8Array(memory.buffer);
            view.set(u8.subarray(slotBytesOffset(id), slotBytesOffset(id) + 8), dst);
        }
        Atomics.store(i32, base, TASK_STATUS_FREE);
        Atomics.notify(i32, base, 1);
    }

    function shutdown() {
        for (const worker of workers) {
            try { worker.terminate(); } catch (_) { /* ignore */ }
        }
        workers.length = 0;
    }

    return {
        size: count,
        bindMemory,
        enqueue,
        awaitJob,
        shutdown,
    };
}

function createMockTcpTable(options = {}) {
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
        shutdown() {},
    };
}

function createRealTcpBridge() {
    const sab = new SharedArrayBuffer(64 + 65536);
    const i32 = new Int32Array(sab, 0, 16);
    const bytes = new Uint8Array(sab, 64);
    const worker = new Worker(path.join(__dirname, 'absolute-wasm-tcp-worker.js'), {
        workerData: { sab },
    });
    // Allow the host process to exit when no other work remains; call shutdown()
    // for an orderly close of sockets + worker.
    if (typeof worker.unref === 'function') worker.unref();
    worker.on('error', (error) => {
        if (typeof console !== 'undefined') console.error('absolute wasm tcp worker error', error);
    });

    function call(op, arg0, arg1, payload) {
        const data = Buffer.from(payload || '', 'utf8');
        const n = Math.min(data.length, bytes.length);
        bytes.set(data.subarray(0, n), 0);
        i32[1] = op;
        i32[2] = arg0 | 0;
        i32[3] = arg1 | 0;
        i32[4] = -1;
        i32[5] = n;
        Atomics.store(i32, 0, 1); // request
        Atomics.notify(i32, 0, 1);
        // Wait until worker marks done (2).
        while (Atomics.load(i32, 0) === 1) {
            const wait = Atomics.wait(i32, 0, 1, 15000);
            if (wait === 'timed-out') {
                Atomics.store(i32, 0, 0);
                Atomics.notify(i32, 0, 1);
                return { result: -1, payload: Buffer.alloc(0) };
            }
        }
        const result = i32[4] | 0;
        const plen = i32[5] | 0;
        const out = Buffer.from(bytes.subarray(0, Math.max(0, plen)));
        Atomics.store(i32, 0, 0); // idle for next call
        Atomics.notify(i32, 0, 1);
        return { result, payload: out };
    }

    return {
        connect(host, port) {
            return call(OP_CONNECT, port, 0, host).result;
        },
        listen(host, port, backlog) {
            return call(OP_LISTEN, port, backlog || 16, host || '127.0.0.1').result;
        },
        accept(handle) {
            return call(OP_ACCEPT, handle, 0, '').result;
        },
        send(handle, text) {
            return call(OP_SEND, handle, 0, text || '').result;
        },
        receive(handle, maxBytes) {
            const { result, payload } = call(OP_RECEIVE, handle, maxBytes, '');
            if (result < 0) return null;
            return payload;
        },
        port(handle) {
            return call(OP_PORT, handle, 0, '').result;
        },
        close(handle) {
            call(OP_CLOSE, handle, 0, '');
        },
        shutdown() {
            try {
                call(OP_SHUTDOWN, 0, 0, '');
            } catch (_) { /* ignore */ }
            try {
                worker.terminate();
            } catch (_) { /* ignore */ }
        },
    };
}

function createTcpTable(options = {}) {
    const mocks = options.tcpMocks || Object.create(null);
    const hasMocks = Object.keys(mocks).length > 0;
    if (hasMocks || options.forceTcpMocks) {
        return createMockTcpTable(options);
    }
    if (options.allowNetwork === false) {
        return createMockTcpTable(options);
    }
    // Default: real OS sockets via worker_threads (Node only).
    try {
        return createRealTcpBridge();
    } catch (_) {
        return createMockTcpTable(options);
    }
}

function createAbsoluteImports(options = {}) {
    const capture = options.captureLogs === true;
    const logs = [];
    const decoder = new TextDecoder('utf-8');
    /** @type {WebAssembly.Memory | null} */
    let memory = null;
    const mocks = options.httpMocks || Object.create(null);
    // Prefer explicit mocks for unit tests; real sockets when allowNetwork/realTcp.
    const tcpOptions = {
        ...options,
        forceTcpMocks: options.forceTcpMocks || (options.tcpMocks && Object.keys(options.tcpMocks).length > 0),
        allowNetwork: options.allowNetwork !== false && !options.forceTcpMocks,
    };
    if (options.tcpMocks && Object.keys(options.tcpMocks).length > 0) {
        tcpOptions.allowNetwork = false;
        tcpOptions.forceTcpMocks = true;
    }
    const tcp = createTcpTable(tcpOptions);
    /** @type {ReturnType<typeof createTaskPool> | null} */
    let taskPool = options._taskPool || null;

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

        absolute_task_pool_size() {
            return taskPool ? taskPool.size : 0;
        },
        absolute_task_enqueue(entry, contextPtr, contextBytes, core, priority) {
            if (!taskPool || taskPool.size <= 0) return -1;
            return taskPool.enqueue(entry, contextPtr, contextBytes, core, priority);
        },
        absolute_task_await_job(jobId, contextPtr, contextBytes) {
            if (!taskPool) return;
            taskPool.awaitJob(jobId, contextPtr, contextBytes);
        },
    };

    return {
        imports: { env },
        logs,
        setTaskPool(pool) {
            taskPool = pool;
            if (taskPool && memory && typeof taskPool.bindMemory === 'function') {
                taskPool.bindMemory(memory);
            }
        },
        shutdown() {
            try {
                if (taskPool && typeof taskPool.shutdown === 'function') taskPool.shutdown();
            } catch (_) { /* ignore */ }
            try {
                if (tcp && typeof tcp.shutdown === 'function') tcp.shutdown();
            } catch (_) { /* ignore */ }
        },
        bind(instance, importedMemory) {
            memory = (instance && instance.exports && instance.exports.memory)
                || importedMemory
                || null;
            if (!memory) throw new Error('Absolute wasm module did not provide memory');
            if (taskPool && typeof taskPool.bindMemory === 'function') {
                taskPool.bindMemory(memory);
            }
        },
    };
}

/**
 * If the module imports env.memory (shared-memory builds), create a matching
 * WebAssembly.Memory and attach it to imports.env.memory.
 */
function prepareSharedMemoryImport(module, imports) {
    const memImport = WebAssembly.Module.imports(module).find(
        (entry) => entry.module === 'env' && entry.name === 'memory' && entry.kind === 'memory',
    );
    if (!memImport) return null;
    const type = memImport.type || {};
    const initial = Number(type.initial ?? type.minimum ?? 256) || 256;
    let maximum = type.maximum != null ? Number(type.maximum) : 256;
    if (!Number.isFinite(maximum) || maximum < initial) maximum = Math.max(initial, 256);
    // Shared-memory Absolute modules always expect a SharedArrayBuffer-backed Memory.
    const memory = new WebAssembly.Memory({
        initial,
        maximum,
        shared: true,
    });
    if (!imports.env) imports.env = {};
    imports.env.memory = memory;
    return memory;
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
    const taskWorkers = Number(options.taskWorkers) || 0;
    let taskPool = null;
    if (taskWorkers > 0) {
        taskPool = createTaskPool(bytes, taskWorkers);
        options = { ...options, _taskPool: taskPool };
    }
    const host = createAbsoluteImports(options);
    if (taskPool) host.setTaskPool(taskPool);
    const module = await WebAssembly.compile(bytes);
    const importedMemory = prepareSharedMemoryImport(module, host.imports);
    const result = await WebAssembly.instantiate(module, host.imports);
    const instance = result.instance || result;
    host.bind(instance, importedMemory);
    // Give workers a moment to compile/instantiate before the first spawn.
    if (taskPool && taskPool.size > 0) {
        await new Promise((resolve) => setTimeout(resolve, 50));
    }
    return {
        instance,
        exports: instance.exports,
        logs: host.logs,
        host,
        memory: importedMemory || instance.exports.memory || null,
        sharedMemory: !!(importedMemory || (instance.exports.memory
            && instance.exports.memory.buffer
            && typeof SharedArrayBuffer !== 'undefined'
            && instance.exports.memory.buffer instanceof SharedArrayBuffer)),
    };
}

module.exports = {
    createAbsoluteImports,
    instantiateAbsoluteWasm,
    prepareSharedMemoryImport,
    prepareHttp,
    httpGetSync,
    createTcpTable,
    createTaskPool,
};
