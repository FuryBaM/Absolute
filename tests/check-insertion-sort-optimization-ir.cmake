if(NOT DEFINED IR_FILE)
    message(FATAL_ERROR "IR_FILE is required")
endif()

file(READ "${IR_FILE}" ir)

string(REGEX MATCHALL "llvm[.]loop[.]unroll[.]disable" unroll_disable_markers "${ir}")
list(LENGTH unroll_disable_markers unroll_disable_count)
if(NOT unroll_disable_count EQUAL 1)
    message(FATAL_ERROR
        "expected only the insertion-sort loop containing a nested loop to disable unrolling, found ${unroll_disable_count} markers")
endif()

if(NOT ir MATCHES "array[.]index[.]wide[^\n]*= sext i32")
    message(FATAL_ERROR "expected an i32 array index to be widened only for the bounds comparison")
endif()

if(NOT ir MATCHES "getelementptr inbounds i32, ptr [^,\n]+, i32 %")
    message(FATAL_ERROR "expected one-dimensional array GEPs to preserve their i32 source index")
endif()
