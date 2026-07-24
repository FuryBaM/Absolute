/**
 * Browser nested Worker: shared-memory Absolute tasks (in-place contextPtr).
 * Mirrors tools/absolute-wasm-shared-task-worker.js for the web.
 *
 * Init:
 *   { type:'init', wasmBytes, memory, jobSab, jobCount, jobStride, jobBase, wakeIndex }
 *
 * Job slot (i32, compact):
 *   [0] status 0/1/2/3
 *   [1] entry
 *   [2] contextPtr
 *   [3] core
 *   [4] priority
 *   [5] result
 */

/* eslint-disable no-restricted-globals */

const STATUS_READY = 1;
const STATUS_RUNNING = 2;
const STATUS_DONE = 3;

let i32 = null;
let jobCount = 0;
let jobStride = 64;
let jobBase = 64;
let wakeIndex = 0;
let wasmBytes = null;
/** @type {WebAssembly.Memory | null} */
let sharedMemory = null;

let instance = null;
let table = null;

function makeImports() {
    return {
        env: {
            memory: sharedMemory,
            absolute_log() {},
            absolute_http_get() { return -1; },
            absolute_tcp_connect() { return -1; },
            absolute_tcp_listen() { return -1; },
            absolute_tcp_accept() { return -1; },
            absolute_tcp_send() { return 0; },
            absolute_tcp_receive() { return -1; },
            absolute_tcp_close() {},
            absolute_tcp_port() { return -1; },
            absolute_task_pool_size() { return 0; },
            absolute_task_enqueue() { return -1; },
            absolute_task_await_job() {},
        },
    };
}

async function ensureInstance() {
    if (instance) return;
    const result = await WebAssembly.instantiate(wasmBytes, makeImports());
    instance = result.instance || result;
    table = instance.exports.__indirect_function_table;
    if (!table) throw new Error('browser shared task worker: missing function table');
    if (typeof instance.exports.__wasm_call_ctors === 'function') {
        try { instance.exports.__wasm_call_ctors(); } catch (_) { /* ignore */ }
    }
}

function slotI32Offset(jobIndex) {
    return (jobBase + jobIndex * jobStride) >> 2;
}

function tryClaimJob() {
    for (let i = 0; i < jobCount; i += 1) {
        const base = slotI32Offset(i);
        if (Atomics.compareExchange(i32, base, STATUS_READY, STATUS_RUNNING) === STATUS_READY) {
            return i;
        }
    }
    return -1;
}

function runJob(jobIndex) {
    const base = slotI32Offset(jobIndex);
    const entry = Atomics.load(i32, base + 1) | 0;
    const contextPtr = Atomics.load(i32, base + 2) | 0;
    let resultCode = 0;
    try {
        const fn = table.get(entry);
        if (typeof fn !== 'function' || !contextPtr) resultCode = -1;
        else {
            fn(contextPtr);
            resultCode = 0;
        }
    } catch (_) {
        resultCode = -1;
    }
    Atomics.store(i32, base + 5, resultCode);
    Atomics.store(i32, base, STATUS_DONE);
    Atomics.notify(i32, base, 1);
}

async function loop() {
    await ensureInstance();
    for (;;) {
        let job = tryClaimJob();
        if (job < 0) {
            const wake = Atomics.load(i32, wakeIndex);
            Atomics.wait(i32, wakeIndex, wake);
            job = tryClaimJob();
        }
        if (job >= 0) runJob(job);
    }
}

self.onmessage = (event) => {
    const msg = event.data || {};
    if (msg.type !== 'init') return;
    wasmBytes = msg.wasmBytes instanceof ArrayBuffer
        ? new Uint8Array(msg.wasmBytes)
        : new Uint8Array(msg.wasmBytes);
    sharedMemory = msg.memory;
    i32 = new Int32Array(msg.jobSab);
    jobCount = msg.jobCount | 0;
    jobStride = msg.jobStride | 0;
    jobBase = msg.jobBase | 0;
    wakeIndex = msg.wakeIndex | 0;
    loop().catch((error) => {
        // eslint-disable-next-line no-console
        console.error('absolute browser shared task worker failed', error);
    });
    self.postMessage({ type: 'ready' });
};
