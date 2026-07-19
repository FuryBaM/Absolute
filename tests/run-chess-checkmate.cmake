if(NOT DEFINED LLI OR NOT DEFINED IR_FILE OR NOT DEFINED INPUT_FILE)
    message(FATAL_ERROR "LLI, IR_FILE, and INPUT_FILE are required")
endif()

execute_process(
    COMMAND "${LLI}" "${IR_FILE}"
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
