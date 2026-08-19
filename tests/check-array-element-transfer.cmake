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
        "array-element-transfer-errors.abs unexpectedly compiled successfully")
endif()

# Both are required by name: the direct refusal has always been there, and the
# one inside the generic is the point -- the same line is refused for
# `Bag<Cell*>` and left alone for `Bag<int32>`.
set(diagnostics "${compiler_stdout}${compiler_stderr}")
foreach(pattern IN ITEMS
        "array-element-transfer-errors[.]abs:33:5: Semantic error: unsafeArrayCopy cannot copy elements that own something; that would duplicate the ownership -- unsafeArrayMove transfers them instead [[]E_UNSAFE_ARRAY_COPY_OWNING[]]"
        "array-element-transfer-errors[.]abs:25:9: Semantic error: unsafeArrayCopy cannot copy elements that own something; at 'Bag<Cell[*]>' that would duplicate the ownership -- unsafeArrayMove transfers them instead [[]E_UNSAFE_ARRAY_COPY_OWNING[]]")
    if(NOT diagnostics MATCHES "${pattern}")
        message(FATAL_ERROR "missing refusal: ${pattern}\n${diagnostics}")
    endif()
endforeach()

if(diagnostics MATCHES "Bag<int32>")
    message(FATAL_ERROR
        "an element that owns nothing was refused a block copy\n${diagnostics}")
endif()
