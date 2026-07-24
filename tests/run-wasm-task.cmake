if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC is missing: ${ABSOLUTEC}")
endif()
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "SOURCE is missing: ${SOURCE}")
endif()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT path is required")
endif()
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE is missing: ${NODE}")
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
    message(FATAL_ERROR "wasm task build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-task-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const wasmPath = process.argv[3];
const bytes = fs.readFileSync(wasmPath);
instantiateAbsoluteWasm(bytes, { captureLogs: true }).then(({ exports, logs }) => {
  if (typeof exports.main !== 'function') {
    console.error('missing main', Object.keys(exports));
    process.exit(2);
  }
  const code = exports.main();
  if (code !== 0) {
    console.error('main returned', code, logs);
    process.exit(5);
  }
  const text = logs.join('');
  if (!text.includes('wasm-task=ok')) {
    console.error('missing println output', JSON.stringify(logs));
    process.exit(6);
  }
  console.log('wasm-task=ok');
}).catch((error) => {
  console.error(error);
  process.exit(1);
});
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${HOST_JS}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "wasm task run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-task=ok")
    message(FATAL_ERROR "unexpected wasm task output:\n${RUN_OUT}")
endif()
