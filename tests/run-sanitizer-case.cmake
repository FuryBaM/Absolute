if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "EXECUTABLE is required")
endif()
if(NOT DEFINED EXPECT)
    message(FATAL_ERROR "EXPECT is required")
endif()
if(NOT DEFINED TIMEOUT_SECONDS)
    set(TIMEOUT_SECONDS 20)
endif()

set(asan_options "abort_on_error=1:symbolize=0")
if(NOT DEFINED DETECT_LEAKS)
    if(WIN32 OR DEFINED ENV{TERMUX_VERSION} OR
       "$ENV{PREFIX}" MATCHES "^/data/data/com\.termux/")
        set(DETECT_LEAKS OFF)
    else()
        set(DETECT_LEAKS ON)
    endif()
endif()
if(DETECT_LEAKS)
    string(APPEND asan_options ":detect_leaks=1")
else()
    string(APPEND asan_options ":detect_leaks=0")
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

if("${result}" STREQUAL "0")
    message(FATAL_ERROR "sanitizer case unexpectedly succeeded:\n${output}")
endif()
if(NOT output MATCHES "${EXPECT}")
    message(FATAL_ERROR
        "sanitizer output did not match '${EXPECT}' (result ${result}):\n${output}")
endif()

if("${result}" MATCHES "[Tt]imeout")
    message(STATUS
        "sanitizer case matched '${EXPECT}' before the platform runtime timed out")
else()
    message(STATUS "sanitizer case matched '${EXPECT}'")
endif()
