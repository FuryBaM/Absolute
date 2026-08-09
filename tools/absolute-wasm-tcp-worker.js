'use strict';

/**
 * Background TCP for Absolute wasm host imports.
 * Protocol over SharedArrayBuffer (see absolute-wasm-host.js):
 *   i32[0]  status: 0=idle, 1=request, 2=done
 *   i32[1]  opcode
 *   i32[2]  arg0 (port / handle / maxBytes)
 *   i32[3]  arg1 (backlog)
 *   i32[4]  result (handle id or byte count or -1)
 *   i32[5]  data length for string/bytes payload
 *   bytes from offset 64: UTF-8 host / send text / recv buffer
 */

const { workerData } = require('worker_threads');
const net = require('net');

const OP_CONNECT = 1;
const OP_LISTEN = 2;
const OP_ACCEPT = 3;
const OP_SEND = 4;
const OP_RECEIVE = 5;
const OP_CLOSE = 6;
const OP_PORT = 7;
const OP_SHUTDOWN = 8;

const sab = workerData.sab;
const i32 = new Int32Array(sab, 0, 16);
const bytes = new Uint8Array(sab, 64);

/** @type {Map<number, any>} */
const sockets = new Map();
let nextId = 1;

function readPayloadString(len) {
    return Buffer.from(bytes.subarray(0, len)).toString('utf8');
}

function writePayload(buffer) {
    const n = Math.min(buffer.length, bytes.length);
    bytes.set(buffer.subarray(0, n), 0);
    i32[5] = n;
    return n;
}

function allocId(record) {
    const id = nextId++;
    sockets.set(id, record);
    return id;
}

function attachClient(socket, preferredPort) {
    const id = allocId({
        kind: 'client',
        socket,
        port: socket.localPort || preferredPort || 0,
        inbox: Buffer.alloc(0),
        closed: false,
        waiters: [],
    });
    socket.on('data', (chunk) => {
        const rec = sockets.get(id);
        if (!rec) return;
        rec.inbox = Buffer.concat([rec.inbox, chunk]);
        flushWaiters(rec);
    });
    socket.on('error', () => {
        const rec = sockets.get(id);
        if (rec) {
            rec.closed = true;
            flushWaiters(rec);
        }
    });
    socket.on('close', () => {
        const rec = sockets.get(id);
        if (rec) {
            rec.closed = true;
            flushWaiters(rec);
        }
    });
    socket.on('end', () => {
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
            waiter.resolve(Buffer.alloc(0));
        }
    }
}

function connect(host, port) {
    return new Promise((resolve) => {
        let settled = false;
        const socket = net.connect({ host, port }, () => {
            if (settled) return;
            settled = true;
            socket.setTimeout(0);
            resolve(attachClient(socket, port));
        });
        socket.on('error', () => {
            if (settled) return;
            settled = true;
            resolve(-1);
        });
        socket.setTimeout(10000, () => {
            if (settled) return;
            settled = true;
            socket.destroy();
            resolve(-1);
        });
    });
}

function listen(host, port, backlog) {
    return new Promise((resolve) => {
        const server = net.createServer();
        server.on('error', () => resolve(-1));
        server.listen({ host: host || '127.0.0.1', port, backlog: backlog || 16 }, () => {
            const address = server.address();
            const id = allocId({
                kind: 'server',
                server,
                port: typeof address === 'object' && address ? address.port : port,
                pending: [],
            });
            server.on('connection', (socket) => {
                const rec = sockets.get(id);
                if (!rec) {
                    socket.destroy();
                    return;
                }
                rec.pending.push(attachClient(socket, 0));
            });
            resolve(id);
        });
    });
}

function accept(handle) {
    const rec = sockets.get(handle);
    if (!rec || rec.kind !== 'server') return Promise.resolve(-1);
    if (rec.pending.length) return Promise.resolve(rec.pending.shift());
    return new Promise((resolve) => {
        const start = Date.now();
        const timer = setInterval(() => {
            if (rec.pending.length) {
                clearInterval(timer);
                resolve(rec.pending.shift());
            } else if (Date.now() - start > 5000) {
                clearInterval(timer);
                resolve(-1);
            }
        }, 5);
    });
}

function send(handle, text) {
    const rec = sockets.get(handle);
    if (!rec || rec.kind !== 'client' || rec.closed) return Promise.resolve(0);
    return new Promise((resolve) => {
        const data = Buffer.from(text, 'utf8');
        rec.socket.write(data, (error) => {
            resolve(error ? 0 : data.length);
        });
    });
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
    if (rec.closed) return Promise.resolve(Buffer.alloc(0));
    return new Promise((resolve) => {
        const waiter = {
            maxBytes,
            resolve,
            timer: setTimeout(() => {
                const idx = rec.waiters.indexOf(waiter);
                if (idx >= 0) rec.waiters.splice(idx, 1);
                // Final race: data may have arrived as we timed out.
                if (rec.inbox.length) {
                    const n = Math.min(rec.inbox.length, maxBytes);
                    const out = rec.inbox.subarray(0, n);
                    rec.inbox = rec.inbox.subarray(n);
                    resolve(out);
                } else {
                    resolve(Buffer.alloc(0));
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
        while (rec.waiters && rec.waiters.length) {
            const waiter = rec.waiters.shift();
            clearTimeout(waiter.timer);
            waiter.resolve(Buffer.alloc(0));
        }
        try { rec.socket.destroy(); } catch (_) { /* ignore */ }
    } else if (rec.kind === 'server') {
        try { rec.server.close(); } catch (_) { /* ignore */ }
    }
    sockets.delete(handle);
}

function portOf(handle) {
    const rec = sockets.get(handle);
    return rec && typeof rec.port === 'number' ? rec.port : -1;
}

async function handleRequest() {
    const op = i32[1];
    const arg0 = i32[2];
    const arg1 = i32[3];
    const len = i32[5];
    let result = -1;

    if (op === OP_CONNECT) {
        result = await connect(readPayloadString(len), arg0);
    } else if (op === OP_LISTEN) {
        result = await listen(readPayloadString(len), arg0, arg1);
    } else if (op === OP_ACCEPT) {
        result = await accept(arg0);
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
        result = portOf(arg0);
    } else if (op === OP_SHUTDOWN) {
        for (const id of [...sockets.keys()]) close(id);
        result = 0;
    }

    i32[4] = result;
    Atomics.store(i32, 0, 2); // done
    Atomics.notify(i32, 0, 1);
}

async function loop() {
    for (;;) {
        // Sleep while idle (0). Main sets 1 when a request is ready.
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
        // Wait until main acknowledges (resets status to 0).
        while (Atomics.load(i32, 0) === 2) {
            Atomics.wait(i32, 0, 2);
        }
    }
}

loop();
