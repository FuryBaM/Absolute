if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "Wasm IR file is missing: ${INPUT}")
endif()

file(READ "${INPUT}" IR)

string(FIND "${IR}" "target triple = \"wasm32-unknown-unknown\"" TRIPLE_POS)
if(TRIPLE_POS EQUAL -1)
    # Accept any wasm32-* triple Absolute may normalize to.
    if(NOT IR MATCHES "target triple = \"wasm32[^\"]*\"")
        message(FATAL_ERROR "Expected wasm32 target triple in IR:\n${IR}")
    endif()
endif()

string(FIND "${IR}" "@wasm_add" ADD_POS)
if(ADD_POS EQUAL -1)
    message(FATAL_ERROR "Expected exported wasm_add symbol in IR")
endif()
