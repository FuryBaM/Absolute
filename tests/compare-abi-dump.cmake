# Runs the ABI dump and compares it with the checked-in layout.
#
# A mismatch is not necessarily a mistake: the V1 ABI may legitimately grow a
# new type. It does mean the change is one that every already-built plugin
# feels, so it has to be an explicit decision recorded by updating the golden
# file, not a side effect noticed later.

if(NOT DEFINED DUMP OR NOT DEFINED GOLDEN)
    message(FATAL_ERROR "compare-abi-dump requires -DDUMP= and -DGOLDEN=")
endif()

execute_process(
    COMMAND "${DUMP}"
    OUTPUT_VARIABLE actual
    RESULT_VARIABLE code
    ERROR_VARIABLE errors)
if(NOT code EQUAL 0)
    message(FATAL_ERROR "ABI dump failed (${code}): ${errors}")
endif()

file(READ "${GOLDEN}" expected)
string(REPLACE "\r\n" "\n" actual "${actual}")
string(REPLACE "\r\n" "\n" expected "${expected}")
if(NOT actual STREQUAL expected)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/plugin-abi-v1-actual.txt" "${actual}")
    message(FATAL_ERROR
        "V1 plugin ABI layout changed.\n"
        "Every plugin built against the previous layout is affected.\n"
        "If the change is intended, update ${GOLDEN} in the same commit.\n"
        "Observed layout written next to the build tree as "
        "plugin-abi-v1-actual.txt.")
endif()

message(STATUS "plugin-abi-v1=ok")
