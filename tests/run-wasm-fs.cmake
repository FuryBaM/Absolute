if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC is missing")
endif()
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "SOURCE is missing")
endif()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT is required")
endif()
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE is missing")
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
    message(FATAL_ERROR "wasm fs build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-fs-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const wasmPath = process.argv[3];
instantiateAbsoluteWasm(fs.readFileSync(wasmPath), { captureLogs: true }).then(({ exports, logs }) => {
  if (typeof exports.wasm_fs_roundtrip === 'function') {
    const code = exports.wasm_fs_roundtrip();
    if (code !== 0) {
      console.error('wasm_fs_roundtrip failed', code);
      process.exit(2);
    }
  }
  if (typeof exports.main === 'function') {
    const code = exports.main();
    if (code !== 0) {
      console.error('main failed', code, logs);
      process.exit(5);
    }
  }
  const text = logs.join('');
  if (!text.includes('wasm-fs=ok')) {
    console.error('missing println', JSON.stringify(logs));
    process.exit(6);
  }
  console.log('wasm-fs=ok');
}).catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${HOST_JS}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "wasm fs run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
