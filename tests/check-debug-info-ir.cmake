if(NOT DEFINED COMPILER OR NOT DEFINED SOURCE OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "COMPILER, SOURCE, and OUTPUT are required")
endif()

get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

execute_process(
    COMMAND "${COMPILER}" "${SOURCE}" -g -O0 --emit-llvm -o "${OUTPUT}"
    RESULT_VARIABLE compiler_result
    OUTPUT_VARIABLE compiler_stdout
    ERROR_VARIABLE compiler_stderr
)
if(NOT compiler_result EQUAL 0)
    message(FATAL_ERROR
        "debug-info IR compilation failed (${compiler_result})\n"
        "${compiler_stdout}${compiler_stderr}")
endif()

file(READ "${OUTPUT}" ir)
foreach(pattern IN ITEMS
        "!DICompileUnit"
        "llvm[.]dbg[.]declare"
        "llvm[.]debugtrap"
        "filename: \"debug-info[.]abs\""
        "name: \"main\""
        "name: \"counter\""
        "name: \"counterAddress\""
        "name: \"pair\""
        "name: \"offset\""
        "name: \"addOffset\""
        "name: \"owner\""
        "name: \"subscriber\""
        "name: \"observer\""
        "name: \"values\""
        "name: \"view[.]isOwner\""
        "name: \"slice[.]isOwner\""
        "AbsoluteManagedHandle<DebugNode[*]>"
        "AbsoluteArray1D<int32>"
        "DISubprogram\\(name: \"lambda[.]0\""
        "DILocalVariable\\(name: \"input\"")
    if(NOT ir MATCHES "${pattern}")
        message(FATAL_ERROR "debug-info IR is missing pattern: ${pattern}")
    endif()
endforeach()

if(WIN32_EXPECTED AND NOT ir MATCHES
        "!\\{i32 2, !\"CodeView\", i32 1\\}")
    message(FATAL_ERROR "Windows debug-info IR is missing the CodeView module flag")
endif()
