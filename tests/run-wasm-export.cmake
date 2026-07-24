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
file(WRITE "${RUNNER}" [=[
const fs = require('fs');
const wasmPath = process.argv[2];
const bytes = fs.readFileSync(wasmPath);
if (bytes.length < 4 || bytes[0] !== 0x00 || bytes[1] !== 0x61 || bytes[2] !== 0x73 || bytes[3] !== 0x6d) {
  console.error('not a wasm module:', wasmPath, 'size=', bytes.length);
  process.exit(4);
}
WebAssembly.instantiate(bytes, {}).then(({ instance }) => {
  const exp = instance.exports;
  if (typeof exp.wasm_add !== 'function' || typeof exp.wasm_mul !== 'function') {
    console.error('missing exports', Object.keys(exp));
    process.exit(2);
  }
  const add = exp.wasm_add(20, 22);
  const mul = exp.wasm_mul(6, 7);
  if (add !== 42 || mul !== 42) {
    console.error('bad results', { add, mul });
    process.exit(3);
  }
  console.log('wasm-export=ok');
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
    message(FATAL_ERROR "wasm node run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-export=ok")
    message(FATAL_ERROR "unexpected wasm runner output:\n${RUN_OUT}")
endif()
