if(NOT DEFINED IR_FILE)
    message(FATAL_ERROR "IR_FILE is required")
endif()

file(READ "${IR_FILE}" ir)

string(REGEX MATCHALL "llvm[.]loop[.]unroll[.]count[^\n]*i32 4" unroll_markers "${ir}")
list(LENGTH unroll_markers unroll_count)
if(NOT unroll_count EQUAL 1)
    message(FATAL_ERROR
        "expected only the insertion-sort loop containing a nested loop to request unroll count 4, found ${unroll_count} markers")
endif()

if(NOT ir MATCHES "call void @llvm[.]assume[^\n]*position[.]after[.]loop[.]in[.]range")
    message(FATAL_ERROR "expected the unit-decrement loop to emit its proven post-loop lower bound")
endif()

if(NOT ir MATCHES "array[.]index[.]wide[^\n]*= sext i32")
    message(FATAL_ERROR "expected an i32 array index to be widened only for the bounds comparison")
endif()

if(NOT ir MATCHES "getelementptr inbounds i32, ptr [^,\n]+, i32 %")
    message(FATAL_ERROR "expected one-dimensional array GEPs to preserve their i32 source index")
endif()
