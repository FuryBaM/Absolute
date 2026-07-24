'use strict';

/**
 * Absolute wasm task worker.
 *
 * Pulls jobs from a SharedArrayBuffer queue (see createTaskPool in
 * absolute-wasm-host.js), runs the entry via __indirect_function_table on an
 * isolated module instance, writes context bytes back, marks the slot done.
 *
 * Layout of each job slot (from JOB_BASE, stride JOB_STRIDE):
 *   i32[0] status: 0 free, 1 ready, 2 running, 3 done
 *   i32[1] entry function table index
 *   i32[2] context byte length
 *   i32[3] core
 *   i32[4] priority
 *   i32[5] result code (0 ok, -1 fail)
 *   bytes at +64: context payload (CONTEXT_MAX)
 */

const { workerData } = require('worker_threads');

const STATUS_FREE = 0;
const STATUS_READY = 1;
const STATUS_RUNNING = 2;
const STATUS_DONE = 3;

const {
    wasmBytes,
    sab,
    jobCount,
    jobStride,
    jobBase,
    contextMax,
    wakeIndex,
} = workerData;

const i32 = new Int32Array(sab);
const u8 = new Uint8Array(sab);

let instance = null;
let memory = null;
let table = null;
let mallocFn = null;
let freeFn = null;

function makeImports() {
    return {
        env: {
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
    memory = instance.exports.memory;
    table = instance.exports.__indirect_function_table;
    mallocFn = instance.exports.malloc;
    freeFn = instance.exports.free;
    if (!memory || !table || typeof mallocFn !== 'function') {
        throw new Error('task worker: module missing memory/table/malloc');
    }
    if (typeof instance.exports.__wasm_call_ctors === 'function') {
        instance.exports.__wasm_call_ctors();
    }
}

function slotI32Offset(jobIndex) {
    return (jobBase + jobIndex * jobStride) >> 2;
}

function slotBytesOffset(jobIndex) {
    return jobBase + jobIndex * jobStride + 64;
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
    const len = Math.min(Atomics.load(i32, base + 2) | 0, contextMax);
    const payloadOff = slotBytesOffset(jobIndex);
    const payload = u8.subarray(payloadOff, payloadOff + len);

    let resultCode = 0;
    try {
        const fn = table.get(entry);
        if (typeof fn !== 'function') {
            resultCode = -1;
        } else {
            // Absolute wasm malloc is declared as malloc(i64).
            let ctxPtr = 0;
            try {
                ctxPtr = Number(mallocFn(BigInt(len > 0 ? len : 8)));
            } catch (_) {
                try {
                    ctxPtr = Number(mallocFn(len > 0 ? len : 8));
                } catch (__) {
                    ctxPtr = 0;
                }
            }
            if (!ctxPtr) {
                resultCode = -1;
            } else {
                try {
                    const view = new Uint8Array(memory.buffer);
                    view.set(payload, ctxPtr);
                    fn(ctxPtr);
                    // Result is i64 slot 0; only that is required on the host.
                    payload.set(view.subarray(ctxPtr, ctxPtr + Math.min(len, 8)), 0);
                    resultCode = 0;
                } catch (_) {
                    resultCode = -1;
                } finally {
                    if (typeof freeFn === 'function') {
                        try { freeFn(BigInt(ctxPtr)); } catch (_) {
                            try { freeFn(ctxPtr); } catch (__) { /* ignore */ }
                        }
                    }
                }
            }
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

loop().catch((error) => {
    // eslint-disable-next-line no-console
    console.error('absolute wasm task worker failed', error);
});
