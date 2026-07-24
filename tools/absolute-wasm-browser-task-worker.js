/**
 * Browser nested Worker for Absolute task pool.
 * Same SAB job layout as tools/absolute-wasm-task-worker.js (Node).
 *
 * Init message:
 *   { type: 'init', wasmBytes, sab, jobCount, jobStride, jobBase, contextMax, wakeIndex }
 */

/* eslint-disable no-restricted-globals */

const STATUS_FREE = 0;
const STATUS_READY = 1;
const STATUS_RUNNING = 2;
const STATUS_DONE = 3;

let i32 = null;
let u8 = null;
let jobCount = 0;
let jobStride = 0;
let jobBase = 0;
let contextMax = 512;
let wakeIndex = 0;
let wasmBytes = null;

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
        throw new Error('browser task worker: module missing memory/table/malloc');
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

self.onmessage = (event) => {
    const msg = event.data || {};
    if (msg.type !== 'init') return;
    wasmBytes = msg.wasmBytes instanceof ArrayBuffer
        ? new Uint8Array(msg.wasmBytes)
        : new Uint8Array(msg.wasmBytes);
    i32 = new Int32Array(msg.sab);
    u8 = new Uint8Array(msg.sab);
    jobCount = msg.jobCount | 0;
    jobStride = msg.jobStride | 0;
    jobBase = msg.jobBase | 0;
    contextMax = msg.contextMax | 0;
    wakeIndex = msg.wakeIndex | 0;
    loop().catch((error) => {
        // eslint-disable-next-line no-console
        console.error('absolute browser task worker failed', error);
    });
    self.postMessage({ type: 'ready' });
};
