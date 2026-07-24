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

execute_process(
    COMMAND "${ABSOLUTEC}" "${SOURCE}"
        --target wasm32-unknown-unknown
        --build-exe -o "${OUTPUT}"
    RESULT_VARIABLE BUILD_STATUS
    OUTPUT_VARIABLE BUILD_OUT
    ERROR_VARIABLE BUILD_ERR
)
if(NOT BUILD_STATUS EQUAL 0)
    message(FATAL_ERROR "wasm managed build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()
if(NOT EXISTS "${OUTPUT}")
    message(FATAL_ERROR "wasm module was not produced: ${OUTPUT}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-managed-runner.js")
file(WRITE "${RUNNER}" [=[
const fs = require('fs');
const wasmPath = process.argv[2];
const bytes = fs.readFileSync(wasmPath);
WebAssembly.instantiate(bytes, {}).then(({ instance }) => {
  const exp = instance.exports;
  if (typeof exp.wasm_box_sum !== 'function') {
    console.error('missing wasm_box_sum', Object.keys(exp));
    process.exit(2);
  }
  const sum = exp.wasm_box_sum(20, 22);
  if (sum !== 42) {
    console.error('wasm_box_sum failed', sum);
    process.exit(3);
  }
  if (typeof exp.main === 'function') {
    const code = exp.main();
    if (code !== 0) {
      console.error('main returned', code);
      process.exit(5);
    }
  }
  console.log('wasm-managed=ok');
}).catch((error) => {
  console.error(error);
  process.exit(1);
});
]=])

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "wasm managed run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-managed=ok")
    message(FATAL_ERROR "unexpected wasm managed output:\n${RUN_OUT}")
endif()
