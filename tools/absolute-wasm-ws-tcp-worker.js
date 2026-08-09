/**
 * Nested Web Worker: WebSocket-backed TCP ops for Absolute wasm.
 * Same SAB protocol as tools/absolute-wasm-tcp-worker.js (Node net version).
 *
 * i32[0] status: 0=idle, 1=request, 2=done
 * i32[1] opcode
 * i32[2] arg0 (port / handle / maxBytes)
 * i32[3] arg1
 * i32[4] result
 * i32[5] payload length
 * bytes@64: host / send / recv
 *
 * Connect payload is either host string (resolved via wsMap in workerData)
 * or a full ws(s):// URL when host already looks like a URL.
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

/** @type {SharedArrayBuffer | null} */
let sab = null;
/** @type {Int32Array | null} */
let i32 = null;
/** @type {Uint8Array | null} */
let bytes = null;
/** @type {Record<string, string>} */
let wsMap = Object.create(null);

/** @type {Map<number, any>} */
const sockets = new Map();
let nextId = 1;

function readPayloadString(len) {
    return new TextDecoder().decode(bytes.subarray(0, len));
}

function writePayload(buffer) {
    const src = buffer instanceof Uint8Array ? buffer : new Uint8Array(buffer);
    const n = Math.min(src.length, bytes.length);
    bytes.set(src.subarray(0, n), 0);
    i32[5] = n;
    return n;
}

function allocId(record) {
    const id = nextId++;
    sockets.set(id, record);
    return id;
}

function resolveWsUrl(host, port) {
    const text = String(host || '');
    if (text.startsWith('ws://') || text.startsWith('wss://')) return text;
    const key = `${text}:${port}`;
    if (Object.prototype.hasOwnProperty.call(wsMap, key)) return wsMap[key];
    if (Object.prototype.hasOwnProperty.call(wsMap, text)) return wsMap[text];
    // Default guess: ws://host:port
    return `ws://${text}:${port}`;
}

function attachSocket(socket, preferredPort) {
    const id = allocId({
        kind: 'client',
        socket,
        port: preferredPort || 0,
        inbox: new Uint8Array(0),
        closed: false,
        waiters: [],
    });
    socket.addEventListener('message', (event) => {
        const rec = sockets.get(id);
        if (!rec) return;
        let chunk;
        if (typeof event.data === 'string') {
            chunk = new TextEncoder().encode(event.data);
        } else if (event.data instanceof ArrayBuffer) {
            chunk = new Uint8Array(event.data);
        } else if (event.data instanceof Blob) {
            // Sync path not available; ignore binary blobs without arrayBuffer await in sync protocol.
            return;
        } else {
            chunk = new Uint8Array(0);
        }
        const next = new Uint8Array(rec.inbox.length + chunk.length);
        next.set(rec.inbox, 0);
        next.set(chunk, rec.inbox.length);
        rec.inbox = next;
        flushWaiters(rec);
    });
    socket.addEventListener('error', () => {
        const rec = sockets.get(id);
        if (rec) {
            rec.closed = true;
            flushWaiters(rec);
        }
    });
    socket.addEventListener('close', () => {
        const rec = sockets.get(id);
        if (rec) {
            rec.closed = true;
            flushWaiters(rec);
        }
    });
    return id;
}

function flushWaiters(rec) {
    while (rec.waiters.length && (rec.inbox.length || rec.closed)) {
        const waiter = rec.waiters.shift();
        clearTimeout(waiter.timer);
        if (rec.inbox.length) {
            const n = Math.min(rec.inbox.length, waiter.maxBytes);
            const out = rec.inbox.subarray(0, n);
            rec.inbox = rec.inbox.subarray(n);
            waiter.resolve(out);
        } else {
            waiter.resolve(new Uint8Array(0));
        }
    }
}

function connect(host, port) {
    return new Promise((resolve) => {
        let settled = false;
        let url;
        try {
            url = resolveWsUrl(host, port);
        } catch (_) {
            resolve(-1);
            return;
        }
        let socket;
        try {
            socket = new WebSocket(url);
        } catch (_) {
            resolve(-1);
            return;
        }
        socket.binaryType = 'arraybuffer';
        const timer = setTimeout(() => {
            if (settled) return;
            settled = true;
            try { socket.close(); } catch (_) { /* ignore */ }
            resolve(-1);
        }, 10000);
        socket.addEventListener('open', () => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            resolve(attachSocket(socket, port));
        });
        socket.addEventListener('error', () => {
            if (settled) return;
            settled = true;
            clearTimeout(timer);
            resolve(-1);
        });
    });
}

function send(handle, text) {
    const rec = sockets.get(handle);
    if (!rec || rec.kind !== 'client' || rec.closed) return Promise.resolve(0);
    const data = new TextEncoder().encode(text || '');
    try {
        rec.socket.send(data);
        return Promise.resolve(data.length);
    } catch (_) {
        return Promise.resolve(0);
    }
}

function receive(handle, maxBytes) {
    const rec = sockets.get(handle);
    if (!rec || rec.kind !== 'client') return Promise.resolve(null);
    if (rec.inbox.length) {
        const n = Math.min(rec.inbox.length, maxBytes);
        const out = rec.inbox.subarray(0, n);
        rec.inbox = rec.inbox.subarray(n);
        return Promise.resolve(out);
    }
    if (rec.closed) return Promise.resolve(new Uint8Array(0));
    return new Promise((resolve) => {
        const waiter = {
            maxBytes,
            resolve,
            timer: setTimeout(() => {
                const idx = rec.waiters.indexOf(waiter);
                if (idx >= 0) rec.waiters.splice(idx, 1);
                if (rec.inbox.length) {
                    const n = Math.min(rec.inbox.length, maxBytes);
                    const out = rec.inbox.subarray(0, n);
                    rec.inbox = rec.inbox.subarray(n);
                    resolve(out);
                } else {
                    resolve(new Uint8Array(0));
                }
            }, 5000),
        };
        rec.waiters.push(waiter);
    });
}

function close(handle) {
    const rec = sockets.get(handle);
    if (!rec) return;
    if (rec.kind === 'client') {
        rec.closed = true;
        while (rec.waiters.length) {
            const waiter = rec.waiters.shift();
            clearTimeout(waiter.timer);
            waiter.resolve(new Uint8Array(0));
        }
        try { rec.socket.close(); } catch (_) { /* ignore */ }
    }
    sockets.delete(handle);
}

async function handleRequest() {
    const op = i32[1];
    const arg0 = i32[2];
    const arg1 = i32[3];
    const len = i32[5];
    let result = -1;

    if (op === OP_CONNECT) {
        result = await connect(readPayloadString(len), arg0);
    } else if (op === OP_LISTEN || op === OP_ACCEPT) {
        result = -1; // not supported over WebSocket map
    } else if (op === OP_SEND) {
        result = await send(arg0, readPayloadString(len));
    } else if (op === OP_RECEIVE) {
        const data = await receive(arg0, arg1);
        if (!data) result = -1;
        else result = writePayload(data);
    } else if (op === OP_CLOSE) {
        close(arg0);
        result = 0;
    } else if (op === OP_PORT) {
        const rec = sockets.get(arg0);
        result = rec && typeof rec.port === 'number' ? rec.port : -1;
    } else if (op === OP_SHUTDOWN) {
        for (const id of [...sockets.keys()]) close(id);
        result = 0;
    }

    i32[4] = result;
    Atomics.store(i32, 0, 2);
    Atomics.notify(i32, 0, 1);
}

async function loop() {
    for (;;) {
        while (Atomics.load(i32, 0) !== 1) {
            Atomics.wait(i32, 0, Atomics.load(i32, 0));
        }
        try {
            await handleRequest();
        } catch (_) {
            i32[4] = -1;
            Atomics.store(i32, 0, 2);
            Atomics.notify(i32, 0, 1);
        }
        while (Atomics.load(i32, 0) === 2) {
            Atomics.wait(i32, 0, 2);
        }
    }
}

self.onmessage = (event) => {
    const msg = event.data || {};
    if (msg.type === 'init') {
        sab = msg.sab;
        i32 = new Int32Array(sab, 0, 16);
        bytes = new Uint8Array(sab, 64);
        wsMap = msg.wsMap || Object.create(null);
        loop();
        self.postMessage({ type: 'ready' });
    }
};
