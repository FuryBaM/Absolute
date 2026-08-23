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
# LeakSanitizer is unavailable on Windows, on Android's ASan runtime, and on
# Darwin (arm64 in particular). Asking for it there makes the runtime fail at
# startup, which abort_on_error turns into a non-zero exit, so every clean case
# fails for a reason unrelated to what it is testing. The other ASan checks —
# use-after-free, double-free — are unaffected and stay on.
# A caller may also turn the leak check off for a case whose leaks are known
# and are not what the case is about, which is how the ownership corpus runs a
# program that builds strings: a string has no lifetime in this language yet
# (docs/known-defects.md), and those bytes would drown the use-after-free and
# double-free this script exists to catch.
if(DEFINED DETECT_LEAKS AND NOT DETECT_LEAKS)
    string(APPEND asan_options ":detect_leaks=0")
elseif(WIN32 OR APPLE OR CMAKE_HOST_APPLE OR DEFINED ENV{TERMUX_VERSION} OR
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
