# Real localhost TCP: Node echo server + Absolute wasm client (worker-backed sockets).
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "SOURCE missing")
endif()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT required")
endif()
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED HOST_JS)
    set(HOST_JS "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-host.js")
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
    message(FATAL_ERROR "wasm net-real build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-net-real-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { Worker } = require('worker_threads');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const wasmPath = process.argv[3];

// Echo must NOT share the main thread: wasm host TCP uses Atomics.wait, which
// freezes the main event loop. A same-thread echo server would never reply.
function startEchoServerWorker() {
  return new Promise((resolve, reject) => {
    const worker = new Worker(`
      const { parentPort } = require('worker_threads');
      const net = require('net');
      const server = net.createServer((socket) => {
        socket.on('data', (chunk) => socket.write(chunk));
      });
      server.on('error', (err) => parentPort.postMessage({ error: String(err) }));
      server.listen(0, '127.0.0.1', () => {
        parentPort.postMessage({ port: server.address().port });
      });
    `, { eval: true });
    worker.once('message', (msg) => {
      if (msg && msg.error) reject(new Error(msg.error));
      else resolve({ worker, port: msg.port });
    });
    worker.once('error', reject);
  });
}

(async () => {
  const { worker, port } = await startEchoServerWorker();
  let host = null;
  try {
    const bytes = fs.readFileSync(wasmPath);
    const inst = await instantiateAbsoluteWasm(bytes, {
      captureLogs: true,
      allowNetwork: true,
    });
    host = inst.host;
    const exports = inst.exports;
    const logs = inst.logs;
    if (typeof exports.wasm_tcp_echo_port !== 'function') {
      console.error('missing wasm_tcp_echo_port', Object.keys(exports));
      process.exit(2);
    }
    const code = exports.wasm_tcp_echo_port(port);
    if (code !== 0) {
      console.error('echo port failed', code, logs);
      process.exit(3);
    }
    console.log('wasm-net-real=ok port=' + port);
  } finally {
    try { if (host && typeof host.shutdown === 'function') host.shutdown(); } catch (_) {}
    try { worker.terminate(); } catch (_) {}
  }
})().catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${HOST_JS}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "wasm net-real run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-net-real=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
