if(NOT DEFINED EXECUTABLE)
    message(FATAL_ERROR "EXECUTABLE is required")
endif()
if(NOT EXISTS "${EXECUTABLE}")
    message(FATAL_ERROR "debug executable was not created: ${EXECUTABLE}")
endif()
file(SIZE "${EXECUTABLE}" executable_size)
if(executable_size LESS 1024)
    message(FATAL_ERROR "debug executable is unexpectedly small: ${executable_size}")
endif()

if(WIN32_EXPECTED)
    get_filename_component(output_directory "${EXECUTABLE}" DIRECTORY)
    get_filename_component(output_name "${EXECUTABLE}" NAME_WE)
    set(pdb "${output_directory}/${output_name}.pdb")
    if(NOT EXISTS "${pdb}")
        message(FATAL_ERROR "CodeView PDB was not created: ${pdb}")
    endif()
    file(SIZE "${pdb}" pdb_size)
    if(pdb_size LESS 4096)
        message(FATAL_ERROR "CodeView PDB is unexpectedly small: ${pdb_size}")
    endif()
    if(DEFINED PDBUTIL AND EXISTS "${PDBUTIL}")
        execute_process(
            COMMAND "${PDBUTIL}" dump -modi=0 -files -symbols -types "${pdb}"
            RESULT_VARIABLE pdbutil_result
            OUTPUT_VARIABLE pdb_dump
            ERROR_VARIABLE pdbutil_stderr
        )
        if(NOT pdbutil_result EQUAL 0)
            message(FATAL_ERROR
                "llvm-pdbutil failed (${pdbutil_result})\n${pdbutil_stderr}")
        endif()
        foreach(pattern IN ITEMS
                "tests.debug-info[.]abs"
                "S_LOCAL.*`counter`"
                "S_LOCAL.*`counterAddress`"
                "S_LOCAL.*`offset`"
                "S_LOCAL.*`addOffset`"
                "S_LOCAL.*`owner`"
                "S_LOCAL.*`subscriber`"
                "S_LOCAL.*`observer`"
                "S_LOCAL.*`values`"
                "S_LOCAL.*`view[.]isOwner`"
                "AbsoluteManagedHandle<DebugNode[*]>"
                "AbsoluteArray1D<int32>"
                "DebugPair")
            if(NOT pdb_dump MATCHES "${pattern}")
                message(FATAL_ERROR "CodeView PDB is missing pattern: ${pattern}")
            endif()
        endforeach()
    endif()
    message(STATUS "debug artifacts: exe=${executable_size} bytes pdb=${pdb_size} bytes")
else()
    message(STATUS "debug artifact: executable=${executable_size} bytes")
endif()
