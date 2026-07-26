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
    message(FATAL_ERROR "full wasm runtime build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-full-runtime.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const bytes = fs.readFileSync(process.argv[3]);
instantiateAbsoluteWasm(bytes, {
  captureLogs: true,
  args: ['--mode=full', '--verbose', 'payload'],
}).then(({ exports, logs, host }) => {
  try {
    if (typeof exports.main !== 'function') throw new Error('main export missing');
    const code = exports.main();
    if (code !== 0) throw new Error('main returned ' + code);
    const output = logs.join('');
    if (!output.includes('wasm-full-runtime=ok')) {
      throw new Error('success marker missing: ' + JSON.stringify(output));
    }
    console.log('wasm-full-runtime=ok');
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
    message(FATAL_ERROR "full wasm runtime run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-full-runtime=ok")
    message(FATAL_ERROR "unexpected full wasm runtime output:\n${RUN_OUT}")
endif()
