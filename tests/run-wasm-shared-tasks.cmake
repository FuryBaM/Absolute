# Shared-memory module + in-place task pool (one shared heap across workers).
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED SOURCE)
    set(SOURCE "${CMAKE_CURRENT_LIST_DIR}/wasm-task-mt.abs")
endif()
if(NOT DEFINED OUTPUT)
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wasm-shared-tasks.wasm")
endif()
if(NOT DEFINED SHARED_RUNTIME OR NOT EXISTS "${SHARED_RUNTIME}")
    message(FATAL_ERROR "SHARED_RUNTIME missing")
endif()
if(NOT DEFINED HOST_JS)
    set(HOST_JS "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-host.js")
endif()
if(NOT DEFINED WASM_LD OR NOT EXISTS "${WASM_LD}")
    find_program(WASM_LD NAMES wasm-ld wasm-ld.exe
        HINTS
            "${CMAKE_BINARY_DIR}/../.absolute/toolchains/llvm-18.1.8/bin"
            "$ENV{ProgramFiles}/LLVM/bin")
endif()
if(NOT WASM_LD AND DEFINED ENV{ABSOLUTE_WASM_LD} AND EXISTS "$ENV{ABSOLUTE_WASM_LD}")
    set(WASM_LD "$ENV{ABSOLUTE_WASM_LD}")
endif()
if(NOT WASM_LD)
    message(FATAL_ERROR "wasm-ld not found")
endif()

execute_process(
    COMMAND "${ABSOLUTEC}" "${SOURCE}"
        --target wasm32-unknown-unknown
        --emit-object -o "${OUTPUT}.o"
    RESULT_VARIABLE OBJ_STATUS
    OUTPUT_VARIABLE OBJ_OUT
    ERROR_VARIABLE OBJ_ERR
)
if(NOT OBJ_STATUS EQUAL 0)
    message(FATAL_ERROR "emit-object failed:\n${OBJ_OUT}\n${OBJ_ERR}")
endif()

execute_process(
    COMMAND "${WASM_LD}"
        --shared-memory
        --import-memory
        --max-memory=16777216
        --no-entry
        --export-all
        "${OUTPUT}.o"
        "${SHARED_RUNTIME}"
        -o "${OUTPUT}"
    RESULT_VARIABLE LINK_STATUS
    OUTPUT_VARIABLE LINK_OUT
    ERROR_VARIABLE LINK_ERR
)
if(NOT LINK_STATUS EQUAL 0)
    message(FATAL_ERROR "shared tasks link failed:\n${LINK_OUT}\n${LINK_ERR}")
endif()

get_filename_component(_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_dir}/run-wasm-shared-tasks-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateAbsoluteWasm } = require(process.argv[2]);

(async () => {
  const bytes = fs.readFileSync(process.argv[3]);
  let host = null;
  try {
    const inst = await instantiateAbsoluteWasm(bytes, {
      captureLogs: true,
      taskWorkers: 2,
      forceTcpMocks: true,
    });
    host = inst.host;
    if (!inst.sharedMemory) throw new Error('expected sharedMemory');
    if (inst.taskPoolMode !== 'shared') {
      throw new Error('expected taskPoolMode=shared got ' + inst.taskPoolMode);
    }
    const code = inst.exports.main();
    if (code !== 0) throw new Error('main ' + code + ' ' + JSON.stringify(inst.logs));
    if (!inst.logs.join('').includes('wasm-task-mt=ok')) {
      throw new Error('marker missing ' + JSON.stringify(inst.logs));
    }
    console.log('wasm-shared-tasks=ok mode=' + inst.taskPoolMode);
  } finally {
    try { if (host) host.shutdown(); } catch (_) {}
  }
})().catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${HOST_JS}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "shared tasks run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-shared-tasks=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
