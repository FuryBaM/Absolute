if(NOT DEFINED LLI OR NOT DEFINED IR_FILE OR NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "LLI, IR_FILE, and INPUT_FILE are required")
endif()

# The JIT resolves runtime symbols out of the shared runtime, the same way the
# other lli-based tests do. A program that holds a string needs it now: the
# storage behind one is reference counted by the runtime.
set(lli_arguments)
if(DEFINED RUNTIME_LIB AND RUNTIME_LIB)
    list(APPEND lli_arguments "--dlopen=${RUNTIME_LIB}")
endif()

execute_process(
    COMMAND "${LLI}" ${lli_arguments} "${IR_FILE}"
    INPUT_FILE "${INPUT_FILE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error
)

if(NOT result EQUAL 0)
    message(FATAL_ERROR "Chess example failed (${result}):\n${output}\n${error}")
endif()
if(NOT output MATCHES "CHECKMATE\\. White wins\\.")
    message(FATAL_ERROR "Chess example did not recognize checkmate:\n${output}")
endif()

message(STATUS "Absolute Chess recognized Scholar's Mate")
