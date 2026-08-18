# Runs a program built with --sanitize=thread and checks the one thing that
# matters for the case: EXPECT_RACE=ON means the program has a deliberate race
# and ThreadSanitizer must say so; EXPECT_RACE=OFF means it must not.
#
# Both directions are needed. A clean run only means something if the
# instrumentation is known to be live, and the only way to know that is a
# program whose race it does report.
if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "EXECUTABLE is required")
endif()
if(NOT DEFINED TIMEOUT_SECONDS)
    set(TIMEOUT_SECONDS 300)
endif()
if(NOT DEFINED EXPECT_RACE)
    set(EXPECT_RACE OFF)
endif()

# halt_on_error stays off so a run that races reports every race it finds
# rather than the first; exitcode is what turns any report into a failure.
set(tsan_options "halt_on_error=0:exitcode=66:history_size=4")

execute_process(
    COMMAND "${CMAKE_COMMAND}" -E env
        "TSAN_OPTIONS=${tsan_options}"
        "${EXECUTABLE}"
    TIMEOUT "${TIMEOUT_SECONDS}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
set(output "${stdout}\n${stderr}")
string(REGEX MATCH "WARNING: ThreadSanitizer" reported "${output}")

if(EXPECT_RACE)
    if(NOT reported)
        message(FATAL_ERROR
            "the deliberate race was not reported, so the instrumentation is "
            "not doing anything and every clean case above is meaningless "
            "(result ${result}):\n${output}")
    endif()
    if(NOT output MATCHES "data race")
        message(FATAL_ERROR
            "ThreadSanitizer reported something other than a data race:\n${output}")
    endif()
    message(STATUS "thread sanitizer reported the deliberate race")
    return()
endif()

if(reported)
    message(FATAL_ERROR
        "thread sanitizer reported a race:\n${output}")
endif()
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR
        "thread sanitizer case failed (result ${result}):\n${output}")
endif()
if(DEFINED EXPECT AND NOT output MATCHES "${EXPECT}")
    message(FATAL_ERROR
        "thread sanitizer output did not match '${EXPECT}':\n${output}")
endif()
message(STATUS "thread sanitizer case passed")
