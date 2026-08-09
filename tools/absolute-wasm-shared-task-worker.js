'use strict';

/**
 * Shared-memory Absolute task worker (Node worker_threads).
 * Instantiates the same module with the host's shared WebAssembly.Memory and
 * runs entry(contextPtr) in-place — no context copy.
 *
 * workerData: { wasmBytes, memory, jobSab, jobCount, jobStride, jobBase, wakeIndex }
 *
 * Job slot i32 layout (same base as isolated pool, compact shared mode):
 *   [0] status 0 free / 1 ready / 2 running / 3 done
 *   [1] entry table index
 *   [2] contextPtr (i32 linear address)
 *   [3] core
 *   [4] priority
 *   [5] result code
 */

const { workerData, parentPort } = require('worker_threads');

const STATUS_FREE = 0;
const STATUS_READY = 1;
const STATUS_RUNNING = 2;
const STATUS_DONE = 3;

const {
    wasmBytes,
    memory,
    jobSab,
    jobCount,
    jobStride,
    jobBase,
    wakeIndex,
} = workerData;

const i32 = new Int32Array(jobSab);

let instance = null;
let table = null;

function makeImports() {
    return {
        env: {
            memory,
            absolute_time_unix_nanos() { return BigInt(Date.now()) * 1000000n; },
            absolute_time_monotonic_nanos() { return process.hrtime.bigint(); },
            absolute_random_entropy() { return BigInt(Date.now()) ^ 0xa5a5f00dn; },
            absolute_process_args_count() { return 1; },
            absolute_process_arg_copy() { return -1; },
            absolute_log() {},
            absolute_http_get() { return -1; },
            absolute_tcp_connect() { return -1; },
            absolute_tcp_listen() { return -1; },
            absolute_tcp_accept() { return -1; },
            absolute_tcp_send() { return 0; },
            absolute_tcp_receive() { return -1; },
            absolute_tcp_close() {},
            absolute_tcp_port() { return -1; },
            // Nested spawns on workers run synchronously (pool size 0).
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
    if (!table) throw new Error('shared task worker: missing function table');
    if (typeof instance.exports.__wasm_call_ctors === 'function') {
        // Data may already be live; ctors should be idempotent for Absolute runtime.
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
        if (typeof fn !== 'function' || !contextPtr) {
            resultCode = -1;
        } else {
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
    if (parentPort) parentPort.postMessage({ type: 'ready' });
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

loop().catch((error) => {
    // eslint-disable-next-line no-console
    console.error('absolute shared task worker failed', error);
});
