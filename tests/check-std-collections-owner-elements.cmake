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

# What remains is VectorBuilder, and each of the three ways it duplicates a
# handle. Anchored to the class rather than to the line, because these lines
# belong to the standard library and are expected to move; what must not change
# quietly is that the instantiation is named.
#
# It is reached only because the program asks for a builder. `Vector<Cell*>`
# itself is an ordinary vector -- the check below that a fixed class stays
# quiet is what says so, and tests/std-vector-owner-elements.abs runs one.
foreach(pattern IN ITEMS
        "at 'std[.]collections[.]VectorBuilder<Cell[*]>' the field 'buffer' is stored from a subscriber [[]E_RESOURCE_FIELD_REQUIRES_OWNER[]]"
        "at 'std[.]collections[.]VectorBuilder<Cell[*]>' a slot of 'buffer' is filled by reading another slot rather than taking it [[]E_RESOURCE_ELEMENT_REQUIRES_OWNER[]]"
        "at 'std[.]collections[.]VectorBuilder<Cell[*]>' 'buffer' hands back a field its object still owns [[]E_MANAGED_RETURN_REQUIRES_OWNER[]]"
        "at 'std[.]collections[.]SetBuilder<Cell[*]>' the field 'buffer' is stored from a subscriber [[]E_RESOURCE_FIELD_REQUIRES_OWNER[]]"
        "at 'std[.]collections[.]MapBuilder<Cell[*]>' the field 'std[.]collections[.]KeyValuePair[.]value' is stored from a subscriber [[]E_RESOURCE_FIELD_REQUIRES_OWNER[]]")
    if(NOT diagnostics MATCHES "${pattern}")
        message(FATAL_ERROR "missing diagnostic: ${pattern}\n${diagnostics}")
    endif()
endforeach()

# The containers themselves say the weaker thing now, and must stay quiet. Only
# their builders report, and only because this program asks for one.
foreach(fixed IN ITEMS "Vector<Cell[*]>" "VectorIterator<Cell[*]>"
        "Set<Cell[*]>" "SetIterator<Cell[*]>"
        "Map<Cell[*]>" "MapIterator<Cell[*]>")
    if(diagnostics MATCHES "at 'std[.]collections[.]${fixed}'")
        message(FATAL_ERROR
            "a class that was fixed is reporting again: ${fixed}\n${diagnostics}")
    endif()
endforeach()

# The element that owns nothing is untouched, and the rest of the suite depends
# on that being true.
if(diagnostics MATCHES "Vector<int32>")
    message(FATAL_ERROR
        "a collection over elements that own nothing was refused\n${diagnostics}")
endif()
