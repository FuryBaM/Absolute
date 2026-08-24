# Integration wasm board: browser host (HTTP mocks + VFS + in-process spawn)
# and the Node host with the same ABI. Isolated taskWorkers would copy the
# instance, so persistence is checked where the pool is size 0 — which is
# what the browser UI thread actually is.
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED SOURCE)
    set(SOURCE "${CMAKE_CURRENT_LIST_DIR}/../examples/integration/webapp.abs")
endif()
if(NOT DEFINED OUTPUT)
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/integration-webapp.wasm")
endif()
if(NOT DEFINED HOST_JS)
    set(HOST_JS "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-host.js")
endif()
if(NOT DEFINED BROWSER_HOST_JS)
    set(BROWSER_HOST_JS "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-browser-host.js")
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
    message(FATAL_ERROR "integration-webapp build failed:\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_dir}/run-integration-webapp-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const path = require('path');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const { instantiateBrowserAbsoluteWasm } = require(process.argv[3]);
const wasmPath = process.argv[4];
const browserTaskWorker = process.argv[5];

const httpMocks = {
  'https://board.test/api/health': 'board-ok',
  'https://board.test/api/settings': '{\"theme\":\"night\",\"revision\":7}',
  'https://board.test/api/catalog': '[\"shard-0.jsonl\",\"shard-1.jsonl\",\"shard-2.jsonl\",\"shard-3.jsonl\"]',
};

function assertMarker(logs, label) {
  const text = logs.join('');
  if (!text.includes('integration-webapp=ok')) {
    throw new Error(label + ' missing marker ' + JSON.stringify(logs));
  }
}

(async () => {
  const bytes = fs.readFileSync(wasmPath);
  let lastLogs = [];

  let browserHost = null;
  try {
    const inst = await instantiateBrowserAbsoluteWasm(bytes, {
      captureLogs: true,
      httpMocks,
    });
    lastLogs = inst.logs;
    browserHost = inst.host;
    if (typeof inst.exports.main !== 'function') {
      throw new Error('main export missing');
    }
    const code = inst.exports.main();
    if (code !== 0) {
      throw new Error('browser main ' + code + ' ' + JSON.stringify(inst.logs));
    }
    assertMarker(inst.logs, 'browser');
  } catch (e) {
    console.error('browser logs ' + JSON.stringify(lastLogs));
    throw e;
  } finally {
    try { if (browserHost) browserHost.shutdown(); } catch (_) {}
  }

  let nodeHost = null;
  try {
    const inst = await instantiateAbsoluteWasm(bytes, {
      captureLogs: true,
      httpMocks,
      taskWorkers: 0,
      forceTcpMocks: true,
    });
    lastLogs = inst.logs;
    nodeHost = inst.host;
    const code = inst.exports.main();
    if (code !== 0) {
      throw new Error('node main ' + code + ' ' + JSON.stringify(inst.logs));
    }
    assertMarker(inst.logs, 'node');
  } catch (e) {
    console.error('node logs ' + JSON.stringify(lastLogs));
    throw e;
  } finally {
    try { if (nodeHost) nodeHost.shutdown(); } catch (_) {}
  }

  if (!fs.existsSync(browserTaskWorker)) throw new Error('missing browser task worker');
  const src = fs.readFileSync(browserTaskWorker, 'utf8');
  if (!src.includes('STATUS_READY') || !src.includes('__indirect_function_table')) {
    throw new Error('browser task worker missing expected protocol');
  }
  const sessionWorker = path.join(path.dirname(browserTaskWorker), 'absolute-wasm-browser-session-worker.js');
  const sessionSrc = fs.readFileSync(sessionWorker, 'utf8');
  if (!sessionSrc.includes('createBrowserTaskPool') || !sessionSrc.includes('taskWorkers')) {
    throw new Error('session worker missing task pool wiring');
  }

  console.log('integration-webapp=ok');
})().catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${HOST_JS}" "${BROWSER_HOST_JS}" "${OUTPUT}"
        "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-browser-task-worker.js"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "integration-webapp run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "integration-webapp=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
