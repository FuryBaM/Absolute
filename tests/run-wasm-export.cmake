# Build (or reuse) a wasm module and execute wasm_add/wasm_mul via Node.
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
if(NOT EXISTS "${HOST_JS}")
    message(FATAL_ERROR "HOST_JS is missing: ${HOST_JS}")
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
    message(FATAL_ERROR "wasm build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "wasm module was not produced: ${OUTPUT}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-export-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const wasmPath = process.argv[3];
const bytes = fs.readFileSync(wasmPath);
instantiateAbsoluteWasm(bytes, { captureLogs: true }).then(({ exports }) => {
  if (typeof exports.wasm_add !== 'function' || typeof exports.wasm_mul !== 'function') {
    console.error('missing exports', Object.keys(exports));
    process.exit(2);
  }
  const add = exports.wasm_add(20, 22);
  const mul = exports.wasm_mul(6, 7);
  if (add !== 42 || mul !== 42) {
    console.error('bad results', { add, mul });
    process.exit(3);
  }
  console.log('wasm-export=ok');
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
    message(FATAL_ERROR "wasm node run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-export=ok")
    message(FATAL_ERROR "unexpected wasm runner output:\n${RUN_OUT}")
endif()
