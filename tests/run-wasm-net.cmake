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
    message(FATAL_ERROR "wasm net build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-net-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);
const wasmPath = process.argv[3];
instantiateAbsoluteWasm(fs.readFileSync(wasmPath), {
  captureLogs: true,
  tcpMocks: {
    '127.0.0.1:9': { mode: 'echo' },
  },
}).then(({ exports, logs }) => {
  if (typeof exports.wasm_tcp_echo === 'function') {
    const code = exports.wasm_tcp_echo();
    if (code !== 0) {
      console.error('echo failed', code);
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
  if (!logs.join('').includes('wasm-net=ok')) {
    console.error('missing println', logs);
    process.exit(6);
  }
  console.log('wasm-net=ok');
}).catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${HOST_JS}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "wasm net run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
