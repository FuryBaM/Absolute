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

# What remains is the builders, and each one now says so once, at the line that
# asked for it, instead of handing back its body's every consequence. Anchored
# to the message rather than to a line: these lines belong to the test, and the
# ones the diagnostics used to name belonged to the standard library.
foreach(pattern IN ITEMS
        "generic type 'std[.]collections[.]VectorBuilder' requires a copyable 'T'"
        "generic type 'std[.]collections[.]SetBuilder' requires a copyable 'T'"
        "generic type 'std[.]collections[.]MapBuilder' requires a copyable 'V'")
    if(NOT diagnostics MATCHES "${pattern}")
        message(FATAL_ERROR "missing diagnostic: ${pattern}\n${diagnostics}")
    endif()
endforeach()

# And says it once. A builder's body cannot serve an owning element in six
# places, and none of those six is the line the program wrote.
foreach(private_field IN ITEMS "the field 'buffer' is stored from a subscriber"
        "a slot of 'buffer' is filled by reading another slot"
        "'buffer' hands back a field its object still owns")
    if(diagnostics MATCHES "${private_field}")
        message(FATAL_ERROR
            "a refused instantiation reported its body as well: ${private_field}\n${diagnostics}")
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
