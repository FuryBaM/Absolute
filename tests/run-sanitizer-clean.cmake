if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "EXECUTABLE is required")
endif()
if(NOT DEFINED EXPECT)
    message(FATAL_ERROR "EXPECT is required")
endif()
if(NOT DEFINED TIMEOUT_SECONDS)
    set(TIMEOUT_SECONDS 30)
endif()

set(asan_options "abort_on_error=1:symbolize=0")
if(WIN32 OR DEFINED ENV{TERMUX_VERSION} OR
   "$ENV{PREFIX}" MATCHES "^/data/data/com\.termux/")
    string(APPEND asan_options ":detect_leaks=0")
else()
    string(APPEND asan_options ":detect_leaks=1")
endif()

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "ASAN_OPTIONS=${asan_options}"
        "${EXECUTABLE}"
    TIMEOUT "${TIMEOUT_SECONDS}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
set(output "${stdout}\n${stderr}")

if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
        "clean sanitizer case failed (result ${result}):\n${output}")
endif()
if(NOT output MATCHES "${EXPECT}")
    message(FATAL_ERROR
        "clean sanitizer output did not match '${EXPECT}':\n${output}")
endif()
if(output MATCHES
   "AddressSanitizer|LeakSanitizer|heap-use-after-free|double-free|memory leak detected|managed pointer\\(s\\) leaked")
    message(FATAL_ERROR
        "clean sanitizer case emitted a memory diagnostic:\n${output}")
endif()

message(STATUS "clean sanitizer case passed")
