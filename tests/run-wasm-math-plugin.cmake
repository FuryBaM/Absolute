if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC is missing: ${ABSOLUTEC}")
endif()
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "SOURCE is missing: ${SOURCE}")
endif()
if(NOT DEFINED PLUGIN OR NOT EXISTS "${PLUGIN}")
    message(FATAL_ERROR "PLUGIN is missing: ${PLUGIN}")
endif()
if(NOT DEFINED OUTPUT OR NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "OUTPUT and NODE are required")
endif()
if(NOT DEFINED HOST_JS)
    set(HOST_JS "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-host.js")
endif()

execute_process(
    COMMAND "${ABSOLUTEC}" "${SOURCE}"
        --plugin "${PLUGIN}"
        --target wasm32-unknown-unknown
        --build-exe -o "${OUTPUT}"
    RESULT_VARIABLE BUILD_STATUS
    OUTPUT_VARIABLE BUILD_OUT
    ERROR_VARIABLE BUILD_ERR
)
if(NOT BUILD_STATUS EQUAL 0)
    message(FATAL_ERROR "math wasm build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(WASM_DIR "${OUTPUT}" DIRECTORY)
set(RUNNER "${WASM_DIR}/run-wasm-math-plugin.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const bytes = fs.readFileSync(process.argv[3]);
instantiateAbsoluteWasm(bytes, { captureLogs: true }).then(({ exports, logs }) => {
  if (typeof exports.math_wasm_probe !== 'function') {
    console.error('missing math_wasm_probe', Object.keys(exports));
    process.exit(2);
  }
  const result = exports.math_wasm_probe();
  if (result !== 42) {
    console.error('math_wasm_probe failed', result);
    process.exit(3);
  }
  if (typeof exports.main === 'function' && exports.main() !== 0) {
    console.error('math wasm main failed', logs);
    process.exit(4);
  }
  if (!logs.join('').includes('math-wasm=ok')) {
    console.error('missing math-wasm output', logs);
    process.exit(5);
  }
  console.log('math-wasm=ok');
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
    message(FATAL_ERROR "math wasm run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "math-wasm=ok")
    message(FATAL_ERROR "unexpected math wasm output:\n${RUN_OUT}")
endif()
