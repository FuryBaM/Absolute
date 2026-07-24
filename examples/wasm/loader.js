/**
 * Minimal browser loader for Absolute wasm modules (zero imports).
 * Works with export-only modules and modules linked against absolute_wasm_shim.
 */

const logEl = document.getElementById('log');
const fileEl = document.getElementById('file');
const runEl = document.getElementById('run');

let instance = null;

function log(message, className = '') {
  logEl.textContent = message;
  logEl.className = className;
}

async function loadBytes(bytes) {
  const result = await WebAssembly.instantiate(bytes, {});
  instance = result.instance;
  const names = Object.keys(instance.exports).filter((n) => !n.startsWith('__'));
  log(`Loaded exports:\n${names.join('\n')}`, 'ok');
  runEl.disabled = typeof instance.exports.wasm_add !== 'function';
}

fileEl.addEventListener('change', async () => {
  const file = fileEl.files && fileEl.files[0];
  if (!file) return;
  try {
    const buffer = await file.arrayBuffer();
    await loadBytes(buffer);
  } catch (error) {
    instance = null;
    runEl.disabled = true;
    log(String(error && error.stack ? error.stack : error), 'err');
  }
});

runEl.addEventListener('click', () => {
  if (!instance || typeof instance.exports.wasm_add !== 'function') return;
  try {
    const value = instance.exports.wasm_add(20, 22);
    let extra = '';
    if (typeof instance.exports.main === 'function') {
      extra = `\nmain() => ${instance.exports.main()}`;
    }
    log(`wasm_add(20, 22) => ${value}${extra}`, value === 42 ? 'ok' : 'err');
  } catch (error) {
    log(String(error && error.stack ? error.stack : error), 'err');
  }
});

// Optional auto-load when served next to a default module name.
const defaultUrl = new URLSearchParams(location.search).get('module') || 'module.wasm';
fetch(defaultUrl)
  .then((response) => (response.ok ? response.arrayBuffer() : Promise.reject(new Error(`HTTP ${response.status}`))))
  .then(loadBytes)
  .catch(() => {
    /* optional */
  });
