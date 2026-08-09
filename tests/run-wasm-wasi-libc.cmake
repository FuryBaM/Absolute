# Coexistence: Absolute WASI runtime + selective wasi-libc objects (not full -lc).
# Full libc.a cannot link: wasm-ld errors on duplicate malloc/exit/printf/…
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED WASI_RUNTIME OR NOT EXISTS "${WASI_RUNTIME}")
    message(FATAL_ERROR "WASI_RUNTIME missing")
endif()
if(NOT DEFINED WASI_LIBC OR NOT EXISTS "${WASI_LIBC}")
    message(FATAL_ERROR "WASI_LIBC (libc.a) missing — bootstrap wasi-sysroot first")
endif()
if(NOT DEFINED WASI_SYSROOT OR NOT IS_DIRECTORY "${WASI_SYSROOT}")
    get_filename_component(WASI_SYSROOT "${WASI_LIBC}/../.." ABSOLUTE)
endif()
if(NOT DEFINED SOURCE)
    set(SOURCE "${CMAKE_CURRENT_LIST_DIR}/wasm-smoke.abs")
endif()
if(NOT DEFINED PROBE)
    set(PROBE "${CMAKE_CURRENT_LIST_DIR}/wasi-libc-probe.c")
endif()
if(NOT DEFINED OUTPUT)
    set(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/wasm-wasi-libc.wasm")
endif()
if(NOT DEFINED WASI_RUN_JS)
    set(WASI_RUN_JS "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-wasi-run.js")
endif()

get_filename_component(_absolute_repo_root
    "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
set(_absolute_wasm_tool_hints
    "${_absolute_repo_root}/.absolute/toolchains/llvm-18.1.8/bin")
if(DEFINED WASM_LD AND EXISTS "${WASM_LD}")
    get_filename_component(_absolute_wasm_ld_dir "${WASM_LD}" DIRECTORY)
    list(APPEND _absolute_wasm_tool_hints "${_absolute_wasm_ld_dir}")
endif()

find_program(ABSOLUTE_WASM_CLANG NAMES clang clang.exe
    HINTS ${_absolute_wasm_tool_hints})
if(NOT ABSOLUTE_WASM_CLANG)
    message(FATAL_ERROR "clang required to compile wasi-libc probe")
endif()
if(NOT DEFINED WASM_LD OR NOT EXISTS "${WASM_LD}")
    find_program(WASM_LD NAMES wasm-ld wasm-ld.exe
        HINTS ${_absolute_wasm_tool_hints})
endif()
if(NOT WASM_LD)
    message(FATAL_ERROR "wasm-ld not found")
endif()

include("${CMAKE_CURRENT_LIST_DIR}/../cmake/AbsoluteWasiLibcExtras.cmake")

get_filename_component(_out_dir "${OUTPUT}" DIRECTORY)
if(NOT DEFINED WASI_LIBC_KIT)
    set(WASI_LIBC_KIT "STRTOD")
endif()
string(TOUPPER "${WASI_LIBC_KIT}" _kit_name)
set(_kit_var "ABSOLUTE_WASI_LIBC_KIT_${_kit_name}")
if(NOT DEFINED ${_kit_var})
    message(FATAL_ERROR "Unknown WASI_LIBC_KIT=${WASI_LIBC_KIT} (try STRTOL or STRTOD)")
endif()
set(_extract_dir "${_out_dir}/wasi-libc-kit-${_kit_name}")
absolute_wasi_extract_kit("${WASI_LIBC}" "${_extract_dir}" ${_kit_var} _kit_objs)
absolute_wasi_find_builtins(_wasi_builtins)
if(NOT _wasi_builtins)
    message(FATAL_ERROR
        "libclang_rt.builtins-wasm32.a not found. Run scripts/windows/bootstrap-wasi-sysroot.ps1\n"
        "or download wasi-sdk builtins under .absolute/toolchains/wasi-builtins-*/")
endif()

execute_process(
    COMMAND "${ABSOLUTEC}" "${SOURCE}"
        --target wasm32-unknown-unknown
        --emit-object -o "${OUTPUT}.abs.o"
    RESULT_VARIABLE OBJ_STATUS
    OUTPUT_VARIABLE OBJ_OUT
    ERROR_VARIABLE OBJ_ERR
)
if(NOT OBJ_STATUS EQUAL 0)
    message(FATAL_ERROR "Absolute emit-object failed:\n${OBJ_OUT}\n${OBJ_ERR}")
endif()

set(_probe_defs "")
if(_kit_name STREQUAL "STRTOL")
    list(APPEND _probe_defs "-DWASI_PROBE_NO_STRTOD=1")
endif()
execute_process(
    COMMAND "${ABSOLUTE_WASM_CLANG}"
        --target=wasm32-wasi
        "--sysroot=${WASI_SYSROOT}"
        -O2
        ${_probe_defs}
        -c "${PROBE}"
        -o "${OUTPUT}.probe.o"
    RESULT_VARIABLE PROBE_STATUS
    OUTPUT_VARIABLE PROBE_OUT
    ERROR_VARIABLE PROBE_ERR
)
if(NOT PROBE_STATUS EQUAL 0)
    message(FATAL_ERROR "probe compile failed:\n${PROBE_OUT}\n${PROBE_ERR}")
endif()

# Absolute runtime FIRST. Selective kit objects only (never full -lc).
# --allow-undefined keeps unresolved wasi-libc internals as imports; the
# strtod kit is closed so env imports should be empty after link.
set(_link_cmd
    "${WASM_LD}" --no-entry --export-all --allow-undefined
    "${OUTPUT}.abs.o"
    "${OUTPUT}.probe.o"
    "${WASI_RUNTIME}"
)
list(APPEND _link_cmd ${_kit_objs})
list(APPEND _link_cmd "${_wasi_builtins}")
list(APPEND _link_cmd -o "${OUTPUT}")
execute_process(
    COMMAND ${_link_cmd}
    RESULT_VARIABLE LINK_STATUS
    OUTPUT_VARIABLE LINK_OUT
    ERROR_VARIABLE LINK_ERR
)
if(NOT LINK_STATUS EQUAL 0)
    message(FATAL_ERROR "wasi-libc coexistence link failed:\n${LINK_OUT}\n${LINK_ERR}")
endif()

set(RUNNER "${_out_dir}/run-wasm-wasi-libc-runner.js")
file(WRITE "${RUNNER}" "
const fs = require('fs');
const { instantiateWasiAbsoluteWasm } = require(process.argv[2]);
(async () => {
  const bytes = fs.readFileSync(process.argv[3]);
  const { exports } = await instantiateWasiAbsoluteWasm(bytes, {
    args: ['absolute-wasi-libc'],
    env: {},
  });
  if (typeof exports.main !== 'function') throw new Error('missing main');
  const code = exports.main();
  if (code !== 0) throw new Error('main ' + code);
  if (typeof exports.wasi_libc_strtol !== 'function') throw new Error('missing wasi_libc_strtol');
  const mem = exports.memory;
  function put(s) {
    const text = new TextEncoder().encode(s + '\\0');
    const ptr = Number(exports.malloc(BigInt(text.length)));
    new Uint8Array(mem.buffer, ptr, text.length).set(text);
    return ptr;
  }
  const i = exports.wasi_libc_strtol(put('12345')) | 0;
  if (i !== 12345) throw new Error('strtol ' + i);
  let msg = 'wasm-wasi-libc=ok strtol=' + i;
  if (typeof exports.wasi_libc_strtod === 'function') {
    const f = exports.wasi_libc_strtod(put('42.5'));
    if (Math.abs(f - 42.5) > 1e-6) throw new Error('strtod ' + f);
    msg += ' strtod=' + f;
  }
  console.log(msg);
})().catch((e) => { console.error(e); process.exit(1); });
")

execute_process(
    COMMAND "${NODE}" "${WASI_RUN_JS}" "--help"
    RESULT_VARIABLE _help_status
    OUTPUT_QUIET ERROR_QUIET
)
execute_process(
    COMMAND "${NODE}" "${RUNNER}" "${WASI_RUN_JS}" "${OUTPUT}"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "wasi-libc run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-wasi-libc=ok")
    message(FATAL_ERROR "unexpected output:\n${RUN_OUT}")
endif()
