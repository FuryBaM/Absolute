/**
 * Main-thread client for tools/absolute-wasm-browser-session-worker.js
 *
 * Usage (module page served with COOP/COEP for SharedArrayBuffer):
 *
 *   const session = await AbsoluteWasmBrowserSession.create({
 *     workerUrl: new URL('./absolute-wasm-browser-session-worker.js', import.meta.url),
 *     onLog: (t) => console.log(t),
 *   });
 *   await session.instantiate(bytes, { httpMocks: {...}, tcpMocks: {...} });
 *   const code = await session.call('main');
 *   session.shutdown();
 */

export class AbsoluteWasmBrowserSession {
    constructor(worker, options = {}) {
        this._worker = worker;
        this._onLog = typeof options.onLog === 'function' ? options.onLog : null;
        this._pending = new Map();
        this._nextId = 1;
        this._ready = false;
        this._tcpMode = null;
        this._exports = [];
        this._worker.onmessage = (event) => this._onMessage(event.data || {});
        this._worker.onerror = (error) => {
            if (this._onLog) this._onLog(String(error.message || error));
        };
    }

    static create(options = {}) {
        const url = options.workerUrl
            || new URL('./absolute-wasm-browser-session-worker.js', import.meta.url);
        const worker = new Worker(url, { type: 'module' });
        const session = new AbsoluteWasmBrowserSession(worker, options);
        return session._waitReady().then(() => session);
    }

    _waitReady() {
        if (this._ready) return Promise.resolve();
        return new Promise((resolve, reject) => {
            this._readyWait = { resolve, reject };
            setTimeout(() => {
                if (!this._ready) reject(new Error('session worker ready timeout'));
            }, 10000);
        });
    }

    _onMessage(msg) {
        if (msg.type === 'ready') {
            this._ready = true;
            if (this._readyWait) {
                this._readyWait.resolve();
                this._readyWait = null;
            }
            return;
        }
        if (msg.type === 'log') {
            if (this._onLog) this._onLog(msg.text || '');
            return;
        }
        if (msg.type === 'instantiated') {
            this._exports = msg.exports || [];
            this._tcpMode = msg.tcpMode || null;
            if (this._instantiateWait) {
                this._instantiateWait.resolve(msg);
                this._instantiateWait = null;
            }
            return;
        }
        if (msg.type === 'result') {
            const pending = this._pending.get(msg.id);
            if (!pending) return;
            this._pending.delete(msg.id);
            if (msg.ok) pending.resolve(msg.value);
            else pending.reject(new Error(msg.error || 'call failed'));
            return;
        }
        if (msg.type === 'error') {
            if (this._instantiateWait) {
                this._instantiateWait.reject(new Error(msg.error || 'worker error'));
                this._instantiateWait = null;
            }
            if (this._onLog) this._onLog(msg.error || 'worker error');
            return;
        }
        if (msg.type === 'shutdown-done') {
            if (this._shutdownWait) {
                this._shutdownWait.resolve();
                this._shutdownWait = null;
            }
        }
    }

    instantiate(bytes, options = {}) {
        const buffer = bytes instanceof ArrayBuffer
            ? bytes
            : (bytes.buffer ? bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength) : bytes);
        return new Promise((resolve, reject) => {
            this._instantiateWait = { resolve, reject };
            this._worker.postMessage({ type: 'instantiate', bytes: buffer, options }, [buffer]);
        });
    }

    call(exportName = 'main', args = []) {
        const id = this._nextId++;
        return new Promise((resolve, reject) => {
            this._pending.set(id, { resolve, reject });
            this._worker.postMessage({ type: 'call', id, exportName, args });
        });
    }

    get exports() { return this._exports.slice(); }
    get tcpMode() { return this._tcpMode; }

    shutdown() {
        return new Promise((resolve) => {
            this._shutdownWait = { resolve };
            try {
                this._worker.postMessage({ type: 'shutdown' });
            } catch (_) {
                resolve();
                return;
            }
            setTimeout(() => {
                try { this._worker.terminate(); } catch (_) { /* ignore */ }
                resolve();
            }, 1000);
        });
    }
}

export default AbsoluteWasmBrowserSession;
