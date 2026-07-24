/**
 * Browser loader for Absolute wasm modules.
 * Provides env.absolute_log so println/assert text appears in the page log.
 */

const logEl = document.getElementById('log');
const fileEl = document.getElementById('file');
const runEl = document.getElementById('run');

let instance = null;
let memory = null;
const decoder = new TextDecoder('utf-8');

function log(message, className = '') {
  logEl.textContent = message;
  logEl.className = className;
}

function readCString(ptr) {
  const view = new Uint8Array(memory.buffer);
  let end = Number(ptr) >>> 0;
  while (end < view.length && view[end] !== 0) end += 1;
  return decoder.decode(view.subarray(Number(ptr) >>> 0, end));
}

function createImports() {
  return {
    env: {
      absolute_log(ptr, len) {
        if (!memory) return;
        const view = new Uint8Array(memory.buffer, Number(ptr) >>> 0, Number(len) >>> 0);
        logEl.textContent += decoder.decode(view);
      },
      absolute_http_get(urlPtr, outPtr, outCap) {
        if (!memory) return -1;
        const url = readCString(urlPtr);
        const mock = window.__ABSOLUTE_HTTP_MOCKS && window.__ABSOLUTE_HTTP_MOCKS[url];
        if (mock == null) return -1;
        const body = new TextEncoder().encode(String(mock));
        const view = new Uint8Array(memory.buffer);
        const n = Math.min(body.length, Number(outCap) | 0);
        view.set(body.subarray(0, n), Number(outPtr) >>> 0);
        return n;
      },
      // TCP host table is Node-oriented; browser stubs keep instantiate working.
      absolute_tcp_connect() { return -1; },
      absolute_tcp_listen() { return -1; },
      absolute_tcp_accept() { return -1; },
      absolute_tcp_send() { return 0; },
      absolute_tcp_receive() { return -1; },
      absolute_tcp_close() {},
      absolute_tcp_port() { return -1; },
    },
  };
}

async function loadBytes(bytes) {
  const imports = createImports();
  const result = await WebAssembly.instantiate(bytes, imports);
  instance = result.instance;
  memory = instance.exports.memory;
  const names = Object.keys(instance.exports).filter((n) => !n.startsWith('__'));
  log(`Loaded exports:\n${names.join('\n')}\n\n`, 'ok');
  runEl.disabled = typeof instance.exports.wasm_add !== 'function' &&
    typeof instance.exports.wasm_box_sum !== 'function' &&
    typeof instance.exports.wasm_task_add !== 'function';
}

fileEl.addEventListener('change', async () => {
  const file = fileEl.files && fileEl.files[0];
  if (!file) return;
  try {
    logEl.textContent = '';
    await loadBytes(await file.arrayBuffer());
  } catch (error) {
    instance = null;
    memory = null;
    runEl.disabled = true;
    log(String(error && error.stack ? error.stack : error), 'err');
  }
});

runEl.addEventListener('click', () => {
  if (!instance) return;
  try {
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
    logEl.textContent += line;
    logEl.className = 'ok';
  } catch (error) {
    log(String(error && error.stack ? error.stack : error), 'err');
  }
});

const defaultUrl = new URLSearchParams(location.search).get('module') || 'module.wasm';
fetch(defaultUrl)
  .then((response) => (response.ok ? response.arrayBuffer() : Promise.reject(new Error(`HTTP ${response.status}`))))
  .then((buffer) => {
    logEl.textContent = '';
    return loadBytes(buffer);
  })
  .catch(() => {
    /* optional default module */
  });
