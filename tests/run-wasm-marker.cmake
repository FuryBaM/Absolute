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
if(NOT DEFINED MARKER)
    message(FATAL_ERROR "MARKER is required")
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
    message(FATAL_ERROR "wasm build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-marker-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const bytes = fs.readFileSync(process.argv[3]);
instantiateAbsoluteWasm(bytes, { captureLogs: true }).then(({ exports, logs, host }) => {
  try {
    if (typeof exports.main !== 'function') throw new Error('main export missing');
    const code = exports.main();
    if (code !== 0) throw new Error('main returned ' + code);
    if (!logs.join('').includes('${MARKER}')) {
      throw new Error('marker missing: ' + JSON.stringify(logs));
    }
    console.log('${MARKER}');
  } finally {
    try { host.shutdown(); } catch (_) {}
  }
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
    message(FATAL_ERROR "wasm run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "${MARKER}")
    message(FATAL_ERROR "unexpected wasm output:\n${RUN_OUT}")
endif()
