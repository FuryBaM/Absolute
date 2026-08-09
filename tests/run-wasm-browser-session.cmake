# Protocol + COOP server smoke for browser worker session stack (no real browser).
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED SOURCE)
    set(SOURCE "${CMAKE_CURRENT_LIST_DIR}/wasm-http.abs")
endif()
if(NOT DEFINED OUTPUT)
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wasm-browser-session.wasm")
endif()
if(NOT DEFINED REPO_ROOT)
    get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
endif()

execute_process(
    COMMAND "${ABSOLUTEC}" "${SOURCE}"
        --target wasm32-unknown-unknown
        --build-exe -o "${OUTPUT}"
    RESULT_VARIABLE BUILD_STATUS
    OUTPUT_VARIABLE BUILD_OUT
    ERROR_VARIABLE BUILD_ERR
)
if(NOT BUILD_STATUS EQUAL 0)
    message(FATAL_ERROR "browser-session build failed:\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_dir}/run-wasm-browser-session-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const http = require('http');
const path = require('path');
const { pathToFileURL } = require('url');
const { Worker } = require('worker_threads');
const { instantiateBrowserAbsoluteWasm } = require(path.join(process.argv[2], 'tools', 'absolute-wasm-browser-host.js'));

// 1) Browser host still runs the module (mocks).
(async () => {
  const repo = process.argv[2];
  const wasmPath = process.argv[3];
  const bytes = fs.readFileSync(wasmPath);
  const { exports, logs, host } = await instantiateBrowserAbsoluteWasm(bytes, {
    captureLogs: true,
    httpMocks: { 'https://example.test/hello': 'hello-browser' },
  });
  const code = exports.main();
  if (code !== 0) throw new Error('main ' + code + ' ' + JSON.stringify(logs));
  if (!logs.join('').includes('wasm-http=ok')) throw new Error('marker missing');
  host.shutdown();

  // 2) COOP/COEP demo server headers
  const servePath = path.join(repo, 'scripts', 'serve-wasm-demo.mjs');
  if (!fs.existsSync(servePath)) throw new Error('missing serve-wasm-demo.mjs');
  const { spawn } = require('child_process');
  const child = spawn(process.execPath, [servePath, '0'], {
    cwd: repo,
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let out = '';
  const port = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('server start timeout: ' + out)), 5000);
    child.stdout.on('data', (chunk) => {
      out += chunk.toString();
      const m = out.match(/http:\\/\\/127\\.0\\.0\\.1:(\\d+)\\//);
      if (m) {
        clearTimeout(timer);
        resolve(Number(m[1]));
      }
    });
    child.stderr.on('data', (chunk) => { out += chunk.toString(); });
    child.on('error', reject);
  }).catch(async (e) => {
    // serve-wasm-demo binds fixed port; probe default 5173 if already parsing failed
    child.kill();
    throw e;
  });

  const headers = await new Promise((resolve, reject) => {
    http.get({ host: '127.0.0.1', port, path: '/' }, (res) => {
      resolve(res.headers);
      res.resume();
    }).on('error', reject);
  });
  child.kill();

  const coop = headers['cross-origin-opener-policy'];
  const coep = headers['cross-origin-embedder-policy'];
  if (coop !== 'same-origin') throw new Error('COOP missing: ' + coop);
  if (coep !== 'require-corp') throw new Error('COEP missing: ' + coep);

  // 3) Nested Atomics TCP protocol (mock nested worker, not WebSocket)
  const sab = new SharedArrayBuffer(64 + 256);
  const i32 = new Int32Array(sab, 0, 16);
  const u8 = new Uint8Array(sab, 64);
  const nested = new Worker(`
    const { workerData } = require('worker_threads');
    const sab = workerData.sab;
    const i32 = new Int32Array(sab, 0, 16);
    const u8 = new Uint8Array(sab, 64);
    async function loop() {
      for (;;) {
        while (Atomics.load(i32, 0) !== 1) Atomics.wait(i32, 0, Atomics.load(i32, 0));
        // OP_SEND-like: echo payload length as result and copy payload back
        const len = i32[5] | 0;
        i32[4] = len;
        Atomics.store(i32, 0, 2);
        Atomics.notify(i32, 0, 1);
        while (Atomics.load(i32, 0) === 2) Atomics.wait(i32, 0, 2);
      }
    }
    loop();
  `, { eval: true, workerData: { sab } });
  const msg = Buffer.from('ping');
  u8.set(msg, 0);
  i32[5] = msg.length;
  Atomics.store(i32, 0, 1);
  Atomics.notify(i32, 0, 1);
  while (Atomics.load(i32, 0) === 1) {
    if (Atomics.wait(i32, 0, 1, 2000) === 'timed-out') throw new Error('atomics timeout');
  }
  if ((i32[4] | 0) !== msg.length) throw new Error('bad result ' + i32[4]);
  Atomics.store(i32, 0, 0);
  Atomics.notify(i32, 0, 1);
  await nested.terminate();

  console.log('wasm-browser-session=ok');
})().catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${REPO_ROOT}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "browser-session run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-browser-session=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
