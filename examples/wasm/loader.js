/**
 * Browser loader for Absolute wasm modules.
 * Modes:
 *   - main (default): instantiate on UI thread (mocks only; no Atomics.wait)
 *   - worker: session worker (+ optional WebSocket TCP when COOP/COEP / SAB)
 *
 * Serve with: node scripts/serve-wasm-demo.mjs
 * so /tools/* is available and COOP/COEP enable SharedArrayBuffer.
 */

const logEl = document.getElementById('log');
const fileEl = document.getElementById('file');
const runEl = document.getElementById('run');
const modeEl = document.getElementById('mode');
const statusEl = document.getElementById('status');

let instance = null;
let memory = null;
let session = null;
const decoder = new TextDecoder('utf-8');
const encoder = new TextEncoder();

function log(message, className = '') {
  logEl.textContent = message;
  logEl.className = className;
}

function appendLog(text) {
  logEl.textContent += text.endsWith('\n') ? text : `${text}\n`;
}

function setStatus(text) {
  if (statusEl) statusEl.textContent = text;
}

function defaultHttpMocks() {
  return window.__ABSOLUTE_HTTP_MOCKS || {
    'https://example.test/hello': 'hello-from-browser',
  };
}

function defaultTcpMocks() {
  return window.__ABSOLUTE_TCP_MOCKS || {
    '127.0.0.1:9': { mode: 'echo' },
  };
}

function defaultWsMap() {
  return window.__ABSOLUTE_WS_MAP || Object.create(null);
}

function readCString(ptr) {
  const view = new Uint8Array(memory.buffer);
  let end = Number(ptr) >>> 0;
  while (end < view.length && view[end] !== 0) end += 1;
  return decoder.decode(view.subarray(Number(ptr) >>> 0, end));
}

function toBytes(value) {
  if (value instanceof Uint8Array) return value;
  return encoder.encode(String(value ?? ''));
}

function createMockTcp(mocks) {
  const sockets = new Map();
  let nextId = 1;
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
    close(handle) { sockets.delete(handle); },
    port(handle) {
      const s = sockets.get(handle);
      return s && typeof s.port === 'number' ? s.port : -1;
    },
  };
}

function createMainImports() {
  const httpMocks = defaultHttpMocks();
  const tcp = createMockTcp(defaultTcpMocks());
  return {
    env: {
      absolute_log(ptr, len) {
        if (!memory) return;
        const view = new Uint8Array(memory.buffer, Number(ptr) >>> 0, Number(len) >>> 0);
        appendLog(decoder.decode(view));
      },
      absolute_http_get(urlPtr, outPtr, outCap) {
        if (!memory) return -1;
        const url = readCString(urlPtr);
        const mock = httpMocks[url];
        if (mock == null) return -1;
        const body = toBytes(mock);
        const view = new Uint8Array(memory.buffer);
        const n = Math.min(body.length, Number(outCap) | 0);
        view.set(body.subarray(0, n), Number(outPtr) >>> 0);
        return n;
      },
      absolute_tcp_connect(hostPtr, port) {
        if (!memory) return -1;
        return tcp.connect(readCString(hostPtr), Number(port) | 0);
      },
      absolute_tcp_listen() { return -1; },
      absolute_tcp_accept() { return -1; },
      absolute_tcp_send(handle, textPtr) {
        if (!memory) return 0;
        return tcp.send(Number(handle) | 0, readCString(textPtr));
      },
      absolute_tcp_receive(handle, outPtr, maxBytes) {
        if (!memory) return -1;
        const data = tcp.receive(Number(handle) | 0, Number(maxBytes) | 0);
        if (!data) return -1;
        const view = new Uint8Array(memory.buffer);
        const n = Math.min(data.length, Number(maxBytes) | 0);
        view.set(data.subarray(0, n), Number(outPtr) >>> 0);
        return n;
      },
      absolute_tcp_close(handle) { tcp.close(Number(handle) | 0); },
      absolute_tcp_port(handle) { return tcp.port(Number(handle) | 0); },
      absolute_task_pool_size() { return 0; },
      absolute_task_enqueue() { return -1; },
      absolute_task_await_job() {},
    },
  };
}

async function loadOnMain(bytes) {
  if (session) {
    await session.shutdown();
    session = null;
  }
  instance = null;
  memory = null;
  const result = await WebAssembly.instantiate(bytes, createMainImports());
  instance = result.instance;
  memory = instance.exports.memory;
  const names = Object.keys(instance.exports).filter((n) => !n.startsWith('__'));
  log(`Loaded on main thread.\nexports:\n${names.join('\n')}\n\n`, 'ok');
  setStatus(`mode=main crossOriginIsolated=${String(window.crossOriginIsolated)}`);
  runEl.disabled = false;
}

async function loadInWorker(bytes) {
  instance = null;
  memory = null;
  if (session) {
    await session.shutdown();
    session = null;
  }
  const { AbsoluteWasmBrowserSession } = await import('/tools/absolute-wasm-browser-session-client.js');
  session = await AbsoluteWasmBrowserSession.create({
    workerUrl: new URL('/tools/absolute-wasm-browser-session-worker.js', location.origin),
    onLog: (text) => appendLog(text),
  });
  const params = new URLSearchParams(location.search);
  const taskWorkers = Math.max(0, Number(params.get('taskWorkers') || window.__ABSOLUTE_TASK_WORKERS || 2) | 0);
  const options = {
    httpMocks: defaultHttpMocks(),
    tcpMocks: defaultTcpMocks(),
    wsMap: defaultWsMap(),
    preferWebSocketTcp: Object.keys(defaultWsMap()).length > 0,
    // Nested task workers need SAB (COOP/COEP page from serve-wasm-demo.mjs).
    taskWorkers: typeof SharedArrayBuffer !== 'undefined' ? taskWorkers : 0,
  };
  // Prefer mocks when present so demos work offline.
  if (Object.keys(options.tcpMocks).length) {
    options.preferWebSocketTcp = false;
  }
  const info = await session.instantiate(bytes, options);
  log(
    `Loaded in Worker.\nexports:\n${(info.exports || []).join('\n')}\ntcpMode=${info.tcpMode}\ntaskWorkers=${info.taskWorkers}\ntaskPoolMode=${info.taskPoolMode}\nsharedMemory=${info.sharedMemory}\n\n`,
    'ok',
  );
  setStatus(
    `mode=worker tcp=${info.tcpMode} tasks=${info.taskWorkers}/${info.taskPoolMode || 'none'} sharedMem=${info.sharedMemory} crossOriginIsolated=${String(window.crossOriginIsolated)}`,
  );
  runEl.disabled = false;
}

async function loadBytes(bytes) {
  const mode = modeEl && modeEl.value === 'worker' ? 'worker' : 'main';
  logEl.textContent = '';
  if (mode === 'worker') await loadInWorker(bytes);
  else await loadOnMain(bytes);
}

fileEl.addEventListener('change', async () => {
  const file = fileEl.files && fileEl.files[0];
  if (!file) return;
  try {
    await loadBytes(await file.arrayBuffer());
  } catch (error) {
    instance = null;
    session = null;
    memory = null;
    runEl.disabled = true;
    log(String(error && error.stack ? error.stack : error), 'err');
  }
});

if (modeEl) {
  modeEl.addEventListener('change', () => {
    setStatus(`selected mode=${modeEl.value}; reload a .wasm to apply`);
  });
}

runEl.addEventListener('click', async () => {
  try {
    if (session) {
      let line = '';
      if (session.exports.includes('wasm_add')) {
        line += `wasm_add(20, 22) => ${await session.call('wasm_add', [20, 22])}\n`;
      }
      if (session.exports.includes('wasm_box_sum')) {
        line += `wasm_box_sum(20, 22) => ${await session.call('wasm_box_sum', [20, 22])}\n`;
      }
      if (session.exports.includes('wasm_task_add')) {
        line += `wasm_task_add(20, 22) => ${await session.call('wasm_task_add', [20, 22])}\n`;
      }
      if (session.exports.includes('main')) {
        line += `main() => ${await session.call('main')}\n`;
      }
      appendLog(line);
      logEl.className = 'ok';
      return;
    }
    if (!instance) return;
    let line = '';
    if (typeof instance.exports.wasm_add === 'function') {
      line += `wasm_add(20, 22) => ${instance.exports.wasm_add(20, 22)}\n`;
    }
    if (typeof instance.exports.wasm_box_sum === 'function') {
      line += `wasm_box_sum(20, 22) => ${instance.exports.wasm_box_sum(20, 22)}\n`;
    }
    if (typeof instance.exports.wasm_task_add === 'function') {
      line += `wasm_task_add(20, 22) => ${instance.exports.wasm_task_add(20, 22)}\n`;
    }
    if (typeof instance.exports.main === 'function') {
      line += `main() => ${instance.exports.main()}\n`;
    }
    appendLog(line);
    logEl.className = 'ok';
  } catch (error) {
    log(String(error && error.stack ? error.stack : error), 'err');
  }
});

setStatus(`crossOriginIsolated=${String(window.crossOriginIsolated)} (use serve-wasm-demo.mjs for SAB)`);

const defaultUrl = new URLSearchParams(location.search).get('module') || 'module.wasm';
const defaultMode = new URLSearchParams(location.search).get('mode');
if (modeEl && (defaultMode === 'worker' || defaultMode === 'main')) {
  modeEl.value = defaultMode;
}
fetch(defaultUrl)
  .then((response) => (response.ok ? response.arrayBuffer() : Promise.reject(new Error(`HTTP ${response.status}`))))
  .then((buffer) => loadBytes(buffer))
  .catch(() => {
    /* optional default module */
  });
