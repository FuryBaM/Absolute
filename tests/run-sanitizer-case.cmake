if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "EXECUTABLE is required")
endif()
if(NOT DEFINED EXPECT)
    message(FATAL_ERROR "EXPECT is required")
endif()

set(asan_options "abort_on_error=1:symbolize=0")
if(NOT WIN32)
    string(APPEND asan_options ":detect_leaks=1")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "ASAN_OPTIONS=${asan_options}"
        "${EXECUTABLE}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
set(output "${stdout}\n${stderr}")

if(result EQUAL 0)
    message(FATAL_ERROR "sanitizer case unexpectedly succeeded:\n${output}")
endif()
if(NOT output MATCHES "${EXPECT}")
    message(FATAL_ERROR
        "sanitizer output did not match '${EXPECT}' (result ${result}):\n${output}")
endif()

message(STATUS "sanitizer case matched '${EXPECT}'")
