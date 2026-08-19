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
        "generic-body-ownership-errors.abs unexpectedly compiled successfully")
endif()

# Every one of these is the same body, and each names the instantiation it was
# judged for. A pass regular expression would be satisfied by any one of them;
# the point is that all five are reached, so each is required by name.
set(diagnostics "${compiler_stdout}${compiler_stderr}")
foreach(pattern IN ITEMS
        "generic-body-ownership-errors[.]abs:30:41: Semantic error: managed resource fields require a fresh owner or null; at 'Sieve<Node[*]>' the field 'held' is stored from a subscriber [[]E_RESOURCE_FIELD_REQUIRES_OWNER[]]"
        "generic-body-ownership-errors[.]abs:32:26: Semantic error: a managed pointer return must transfer an owner; at 'Sieve<Node[*]>' 'held' hands back a field its object still owns [[]E_MANAGED_RETURN_REQUIRES_OWNER[]]"
        "generic-body-ownership-errors[.]abs:34:29: Semantic error: managed subscriber cannot be deleted; at 'Sieve<Node[*]>' the field 'held' is released by its own object, not by its owner [[]E_DELETE_SUBSCRIBER[]]"
        "generic-body-ownership-errors[.]abs:34:29: Semantic error: managed subscriber cannot be deleted; at 'Sieve<sub Node[*]>' the field 'held' borrows an object it does not own [[]E_DELETE_SUBSCRIBER[]]"
        "generic-body-ownership-errors[.]abs:34:29: Semantic error: delete requires a pointer; at 'Sieve<int32>' 'held' is 'int32' [[]E_DELETE_REQUIRES_POINTER[]]")
    if(NOT diagnostics MATCHES "${pattern}")
        message(FATAL_ERROR
            "missing per-instantiation diagnostic: ${pattern}\n${diagnostics}")
    endif()
endforeach()
