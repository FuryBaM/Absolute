# Build with ABSOLUTE_WASM_LIBS pointing at the WASI runtime object and run via
# wasmtime if available; otherwise skip gracefully.
if(NOT DEFINED ABSOLUTEC OR NOT EXISTS "${ABSOLUTEC}")
    message(FATAL_ERROR "ABSOLUTEC missing")
endif()
if(NOT DEFINED SOURCE OR NOT EXISTS "${SOURCE}")
    message(FATAL_ERROR "SOURCE missing")
endif()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "OUTPUT required")
endif()
if(NOT DEFINED WASI_RUNTIME OR NOT EXISTS "${WASI_RUNTIME}")
    message(FATAL_ERROR "WASI_RUNTIME object missing: ${WASI_RUNTIME}")
endif()

find_program(WASMTIME_EXECUTABLE NAMES wasmtime)
if(NOT WASMTIME_EXECUTABLE)
    message(STATUS "wasmtime not found; skipping WASI engine run (build still verified)")
endif()

# Force the WASI console runtime object instead of the default host-import one.
set(ENV{ABSOLUTE_WASM_LIBS} "${WASI_RUNTIME}")
# Disable default host runtime link by replacing: BuildWasmModule always links
# ABSOLUTE_WASM_SHIM_OBJECT first; for WASI we need ONLY the wasi object.
# Work around: link with ABSOLUTE_WASM_LIBS containing wasi object AND empty shim
# is not possible. Instead build object then wasm-ld manually.

execute_process(
    COMMAND "${ABSOLUTEC}" "${SOURCE}"
        --target wasm32-unknown-unknown
        --emit-object -o "${OUTPUT}.o"
    RESULT_VARIABLE OBJ_STATUS
    OUTPUT_VARIABLE OBJ_OUT
    ERROR_VARIABLE OBJ_ERR
)
if(NOT OBJ_STATUS EQUAL 0)
    message(FATAL_ERROR "emit-object failed: ${OBJ_OUT}\n${OBJ_ERR}")
endif()

find_program(WASM_LD NAMES wasm-ld wasm-ld.exe
    HINTS
        "${CMAKE_BINARY_DIR}/../.absolute/toolchains/llvm-18.1.8/bin"
        "$ENV{ProgramFiles}/LLVM/bin")
if(NOT WASM_LD)
    # Prefer the same wasm-ld absolutec uses when available via env.
    if(DEFINED ENV{ABSOLUTE_WASM_LD} AND EXISTS "$ENV{ABSOLUTE_WASM_LD}")
        set(WASM_LD "$ENV{ABSOLUTE_WASM_LD}")
    endif()
endif()
if(NOT WASM_LD)
    message(FATAL_ERROR "wasm-ld not found for WASI link")
endif()

execute_process(
    COMMAND "${WASM_LD}" --no-entry --export-all
        "${OUTPUT}.o" "${WASI_RUNTIME}"
        -o "${OUTPUT}"
    RESULT_VARIABLE LINK_STATUS
    OUTPUT_VARIABLE LINK_OUT
    ERROR_VARIABLE LINK_ERR
)
if(NOT LINK_STATUS EQUAL 0)
    message(FATAL_ERROR "wasi wasm-ld failed: ${LINK_OUT}\n${LINK_ERR}")
endif()

if(WASMTIME_EXECUTABLE)
    execute_process(
        COMMAND "${WASMTIME_EXECUTABLE}" run --invoke main "${OUTPUT}"
        RESULT_VARIABLE RUN_STATUS
        OUTPUT_VARIABLE RUN_OUT
        ERROR_VARIABLE RUN_ERR
    )
    if(NOT RUN_STATUS EQUAL 0)
        message(FATAL_ERROR "wasmtime run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
    endif()
    if(NOT RUN_OUT MATCHES "wasm-smoke=ok")
        message(FATAL_ERROR "wasmtime output missing marker:\n${RUN_OUT}")
    endif()
    message(STATUS "wasmtime ok: ${RUN_OUT}")
else()
    message(STATUS "Built WASI module ${OUTPUT} (install wasmtime to execute)")
endif()
