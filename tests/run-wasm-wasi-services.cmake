# Link Absolute WASI runtime object and execute via Node WASI (or wasmtime).
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
if(NOT DEFINED NODE OR NOT EXISTS "${NODE}")
    message(FATAL_ERROR "NODE missing")
endif()
if(NOT DEFINED WASI_RUN_JS)
    set(WASI_RUN_JS "${CMAKE_CURRENT_LIST_DIR}/../tools/absolute-wasm-wasi-run.js")
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
    message(FATAL_ERROR "emit-object failed: ${OBJ_OUT}\n${OBJ_ERR}")
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

# Prefer Node WASI (available with Node 20+ experimental WASI).
execute_process(
    COMMAND "${NODE}" "${WASI_RUN_JS}" "${OUTPUT}"
        --env ABSOLUTE_WASI_TEST=1
        --arg wasi-fixture
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUT
    ERROR_VARIABLE RUN_ERR
)
if(NOT RUN_STATUS EQUAL 0)
    # Fallback: wasmtime if installed.
    find_program(WASMTIME_EXECUTABLE NAMES wasmtime)
    if(WASMTIME_EXECUTABLE)
        execute_process(
            COMMAND "${CMAKE_COMMAND}" -E env "ABSOLUTE_WASI_TEST=1"
                "${WASMTIME_EXECUTABLE}" run
                --env ABSOLUTE_WASI_TEST=1
                --invoke main
                "${OUTPUT}" wasi-fixture
            RESULT_VARIABLE RUN_STATUS
            OUTPUT_VARIABLE RUN_OUT
            ERROR_VARIABLE RUN_ERR
        )
    endif()
endif()
if(NOT RUN_STATUS EQUAL 0)
    message(FATAL_ERROR "wasi services run failed (${RUN_STATUS}):\n${RUN_OUT}\n${RUN_ERR}")
endif()
if(NOT RUN_OUT MATCHES "wasm-wasi-services=ok")
    message(FATAL_ERROR "unexpected wasi services output:\n${RUN_OUT}\n${RUN_ERR}")
endif()
