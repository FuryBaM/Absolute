if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE)
    message(FATAL_ERROR "COMPILER and SOURCE are required")
endif()

execute_process(
    COMMAND "${COMPILER}" "${SOURCE}"
    RESULT_VARIABLE compiler_result
    OUTPUT_VARIABLE compiler_stdout
    ERROR_VARIABLE compiler_stderr
)
if(compiler_result EQUAL 0)
    message(FATAL_ERROR
        "std-collections-owner-elements-errors.abs unexpectedly compiled successfully")
endif()

set(diagnostics "${compiler_stdout}${compiler_stderr}")

# One instantiation of each of the three classes behind Vector, and each of the
# three ways they duplicate a handle. Anchored to the class rather than to the
# line, because these lines belong to the standard library and are expected to
# move; what must not change quietly is that the instantiation is named and
# that all three classes are reached.
foreach(pattern IN ITEMS
        "at 'std[.]collections[.]Vector<Cell[*]>' a slot of 'res' is filled by reading another slot rather than taking it [[]E_RESOURCE_ELEMENT_REQUIRES_OWNER[]]"
        "at 'std[.]collections[.]Vector<Cell[*]>' 'items' hands back a field its object still owns [[]E_MANAGED_RETURN_REQUIRES_OWNER[]]"
        "at 'std[.]collections[.]VectorIterator<Cell[*]>' 'snapshot' hands back a field its object still owns [[]E_MANAGED_RETURN_REQUIRES_OWNER[]]"
        "at 'std[.]collections[.]VectorBuilder<Cell[*]>' the field 'buffer' is stored from a subscriber [[]E_RESOURCE_FIELD_REQUIRES_OWNER[]]"
        "at 'std[.]collections[.]VectorBuilder<Cell[*]>' a slot of 'buffer' is filled by reading another slot rather than taking it [[]E_RESOURCE_ELEMENT_REQUIRES_OWNER[]]")
    if(NOT diagnostics MATCHES "${pattern}")
        message(FATAL_ERROR "missing diagnostic: ${pattern}\n${diagnostics}")
    endif()
endforeach()

# The element that owns nothing is untouched, and the rest of the suite depends
# on that being true.
if(diagnostics MATCHES "Vector<int32>")
    message(FATAL_ERROR
        "a collection over elements that own nothing was refused\n${diagnostics}")
endif()
