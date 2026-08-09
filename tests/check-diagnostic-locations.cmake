if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED ASYNC_SOURCE)
    message(FATAL_ERROR "COMPILER, SOURCE, and ASYNC_SOURCE are required")
endif()

execute_process(
    COMMAND "${COMPILER}" "${SOURCE}"
    RESULT_VARIABLE compiler_result
    OUTPUT_VARIABLE compiler_stdout
    ERROR_VARIABLE compiler_stderr
)
if(compiler_result EQUAL 0)
    message(FATAL_ERROR "array-errors.abs unexpectedly compiled successfully")
endif()

set(diagnostics "${compiler_stdout}${compiler_stderr}")
foreach(pattern IN ITEMS
        "array-errors[.]abs:2:5: Semantic error: array initializer must be rectangular"
        "array-errors[.]abs:2:26: Semantic error: array literal must be rectangular"
        "array-errors[.]abs:3:26: Semantic error: array literal must be rectangular"
        "array-errors[.]abs:4:5: Semantic error: array initializer size does not match dimension 1"
        "array-errors[.]abs:5:5: Semantic error: array dimension 1 requires a size or an initializer"
        "array-errors[.]abs:7:25: Semantic error: object of type 'int32' is not an array")
    if(NOT diagnostics MATCHES "${pattern}")
        message(FATAL_ERROR "missing located diagnostic: ${pattern}\n${diagnostics}")
    endif()
endforeach()

execute_process(
    COMMAND "${COMPILER}" "${ASYNC_SOURCE}"
    RESULT_VARIABLE async_result
    OUTPUT_VARIABLE async_stdout
    ERROR_VARIABLE async_stderr
)
if(async_result EQUAL 0)
    message(FATAL_ERROR "async-lifetime-errors.abs unexpectedly compiled successfully")
endif()

set(async_diagnostics "${async_stdout}${async_stderr}")
foreach(pattern IN ITEMS
        "async-lifetime-errors[.]abs:9:34: Semantic error: async parameter 'value'"
        "async-lifetime-errors[.]abs:29:48: Semantic error: async parameter 'callback'"
        "async-lifetime-errors[.]abs:37:13: Semantic error: async function 'managedResult'"
        "async-lifetime-errors[.]abs:52:14: Semantic error: async function 'stringResult'")
    if(NOT async_diagnostics MATCHES "${pattern}")
        message(FATAL_ERROR "missing precise async diagnostic: ${pattern}\n${async_diagnostics}")
    endif()
endforeach()
