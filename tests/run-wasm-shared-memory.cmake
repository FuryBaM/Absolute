# Build Absolute module with shared-memory runtime and verify SharedArrayBuffer memory.
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED SOURCE)
    set(SOURCE "${CMAKE_CURRENT_LIST_DIR}/wasm-smoke.abs")
endif()
if(NOT DEFINED OUTPUT)
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wasm-shared-memory.wasm")
endif()
if(NOT DEFINED SHARED_RUNTIME OR NOT EXISTS "${SHARED_RUNTIME}")
    message(FATAL_ERROR "SHARED_RUNTIME object missing")
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
    message(FATAL_ERROR "shared-memory link failed:\n${LINK_OUT}\n${LINK_ERR}")
endif()

get_filename_component(_dir "${OUTPUT}" DIRECTORY)
set(RUNNER "${_dir}/run-wasm-shared-memory-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { Worker } = require('worker_threads');
const { instantiateAbsoluteWasm } = require(process.argv[2]);

(async () => {
  const bytes = fs.readFileSync(process.argv[3]);
  const mod = await WebAssembly.compile(bytes);
  const memImp = WebAssembly.Module.imports(mod).find(
    (i) => i.module === 'env' && i.name === 'memory' && i.kind === 'memory'
  );
  if (!memImp) throw new Error('expected env.memory import');

  const { exports, memory, sharedMemory, logs, host } = await instantiateAbsoluteWasm(bytes, {
    captureLogs: true,
    forceTcpMocks: true,
  });
  try {
    if (!sharedMemory) throw new Error('sharedMemory flag false');
    if (!(memory && memory.buffer instanceof SharedArrayBuffer)) {
      throw new Error('memory.buffer is not SharedArrayBuffer');
    }
    if (typeof exports.absolute_wasm_shared_memory_enabled === 'function') {
      const flag = exports.absolute_wasm_shared_memory_enabled();
      if (flag !== 1) throw new Error('shared flag export ' + flag);
    }
    const code = exports.main();
    if (code !== 0) throw new Error('main ' + code + ' ' + JSON.stringify(logs));
    if (!logs.join('').includes('wasm-smoke=ok')) throw new Error('marker missing');

    // Concurrent writes into the shared buffer from a helper worker.
    const sab = memory.buffer;
    const markerAt = 64; // low addresses are wasm internals; use a free-ish scratch region
    const view = new Int32Array(sab, markerAt, 1);
    Atomics.store(view, 0, 0);
    const worker = new Worker(\`
      const { workerData } = require('worker_threads');
      const view = new Int32Array(workerData.sab, workerData.offset, 1);
      for (let i = 0; i < 1000; i++) Atomics.add(view, 0, 1);
    \`, { eval: true, workerData: { sab, offset: markerAt } });
    for (let i = 0; i < 1000; i++) Atomics.add(view, 0, 1);
    await new Promise((resolve, reject) => {
      worker.on('exit', resolve);
      worker.on('error', reject);
    });
    const total = Atomics.load(view, 0);
    if (total !== 2000) throw new Error('concurrent SAB total ' + total);

    console.log('wasm-shared-memory=ok pages=' + memory.buffer.byteLength / 65536);
  } finally {
    try { host.shutdown(); } catch (_) {}
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
    message(FATAL_ERROR "shared-memory run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-shared-memory=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
