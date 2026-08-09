# Multi-thread Absolute tasks via Node worker pool (isolated wasm instances).
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
    message(FATAL_ERROR "wasm task-mt build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-task-mt-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const wasmPath = process.argv[3];

(async () => {
  const bytes = fs.readFileSync(wasmPath);
  let host = null;
  try {
    const inst = await instantiateAbsoluteWasm(bytes, {
      captureLogs: true,
      taskWorkers: 2,
      forceTcpMocks: true,
    });
    host = inst.host;
    if (typeof inst.exports.main !== 'function') {
      console.error('missing main', Object.keys(inst.exports));
      process.exit(2);
    }
    const code = inst.exports.main();
    if (code !== 0) {
      console.error('main returned', code, inst.logs);
      process.exit(3);
    }
    const text = inst.logs.join('');
    if (!text.includes('wasm-task-mt=ok')) {
      console.error('missing println', JSON.stringify(inst.logs));
      process.exit(4);
    }
    console.log('wasm-task-mt=ok');
  } finally {
    try { if (host && host.shutdown) host.shutdown(); } catch (_) {}
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
    message(FATAL_ERROR "wasm task-mt run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-task-mt=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
