if(NOT DEFINED IR_FILE)
    message(FATAL_ERROR "IR_FILE is required")
endif()

file(READ "${IR_FILE}" ir)
string(REGEX MATCHALL "call void @llvm[.]assume" assume_calls "${ir}")
list(LENGTH assume_calls assume_count)
if(NOT assume_count EQUAL 1)
    message(FATAL_ERROR
        "expected only the safe unit-decrement loop to emit a range assumption, found ${assume_count}")
endif()

if(NOT ir MATCHES "position[.]after[.]loop[.]in[.]range = icmp sge i32[^\n]*, -1")
    message(FATAL_ERROR "expected a post-loop lower-bound fact of -1")
endif()
