# Node unit test for the browser host (mocks only; no DOM).
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "SOURCE missing")
endif()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT required")
endif()
if(NOT DEFINED BROWSER_HOST_JS)
    set(BROWSER_HOST_JS "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-browser-host.js")
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
    message(FATAL_ERROR "browser-host build failed (${BUILD_STATUS}):\n${BUILD_OUT}\n${BUILD_ERR}")
endif()

get_filename_component(_wasm_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_wasm_dir}/run-wasm-browser-host-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateBrowserAbsoluteWasm } = require(process.argv[2]);
const wasmPath = process.argv[3];

(async () => {
  const bytes = fs.readFileSync(wasmPath);
  const { exports, logs, host } = await instantiateBrowserAbsoluteWasm(bytes, {
    captureLogs: true,
    httpMocks: { 'https://example.test/hello': 'hello-browser' },
    tcpMocks: { '127.0.0.1:9': { mode: 'echo' } },
  });
  try {
    if (typeof exports.main === 'function') {
      const code = exports.main();
      if (code !== 0) {
        console.error('main failed', code, logs);
        process.exit(3);
      }
    }
    const text = logs.join('');
    if (!text.includes('wasm-http=ok')) {
      console.error('missing wasm-http=ok', JSON.stringify(logs));
      process.exit(4);
    }
    console.log('wasm-browser-host=ok');
  } finally {
    try { host.shutdown(); } catch (_) {}
  }
})().catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${BROWSER_HOST_JS}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "browser-host run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-browser-host=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
