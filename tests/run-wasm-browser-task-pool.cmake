# Browser task-pool path: same runtime ABI as Node taskWorkers; verifies multi-spawn
# under the Absolute host pool (Node worker_threads stand-in for browser nested Workers).
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED SOURCE)
    set(SOURCE "${CMAKE_CURRENT_LIST_DIR}/wasm-task-mt.abs")
endif()
if(NOT DEFINED OUTPUT)
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wasm-browser-task-pool.wasm")
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
    message(FATAL_ERROR "browser-task-pool build failed:\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_dir}/run-wasm-browser-task-pool-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const path = require('path');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const browserTaskWorker = process.argv[4];

(async () => {
  const bytes = fs.readFileSync(process.argv[3]);
  // 1) Functional pool (Node host — same Absolute imports the browser session uses).
  let host = null;
  try {
    const inst = await instantiateAbsoluteWasm(bytes, {
      captureLogs: true,
      taskWorkers: 2,
      forceTcpMocks: true,
    });
    host = inst.host;
    const code = inst.exports.main();
    if (code !== 0) throw new Error('main ' + code + ' ' + JSON.stringify(inst.logs));
    if (!inst.logs.join('').includes('wasm-task-mt=ok')) throw new Error('marker missing');
  } finally {
    try { if (host) host.shutdown(); } catch (_) {}
  }

  // 2) Browser task worker script exists and is a valid Worker module source.
  if (!fs.existsSync(browserTaskWorker)) throw new Error('missing browser task worker');
  const src = fs.readFileSync(browserTaskWorker, 'utf8');
  if (!src.includes('STATUS_READY') || !src.includes('__indirect_function_table')) {
    throw new Error('browser task worker missing expected protocol');
  }

  // 3) Session worker wires taskWorkers option.
  const sessionWorker = path.join(path.dirname(browserTaskWorker), 'absolute-wasm-browser-session-worker.js');
  const sessionSrc = fs.readFileSync(sessionWorker, 'utf8');
  if (!sessionSrc.includes('createBrowserTaskPool') || !sessionSrc.includes('taskWorkers')) {
    throw new Error('session worker missing task pool wiring');
  }

  console.log('wasm-browser-task-pool=ok');
})().catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${HOST_JS}" "${OUTPUT}"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-browser-task-worker.js"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "browser-task-pool run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-browser-task-pool=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
