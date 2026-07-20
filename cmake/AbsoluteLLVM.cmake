function(absolute_find_llvm)
    if(LLVM_DIR AND EXISTS "${LLVM_DIR}/LLVMConfig.cmake")
        message(STATUS "Using explicit LLVM_DIR: ${LLVM_DIR}")
        return()
    endif()

    set(_absolute_llvm_candidates)
    foreach(_root IN ITEMS
        "${ABSOLUTE_LLVM_ROOT}"
        "$ENV{ABSOLUTE_LLVM_ROOT}"
        "$ENV{LLVM_ROOT}"
        "$ENV{ProgramFiles}/LLVM"
        "$ENV{LOCALAPPDATA}/Programs/LLVM")
        if(_root)
            list(APPEND _absolute_llvm_candidates "${_root}/lib/cmake/llvm")
        endif()
    endforeach()
    foreach(_directory IN ITEMS "$ENV{ABSOLUTE_LLVM_DIR}" "$ENV{LLVM_DIR}")
        if(_directory)
            list(PREPEND _absolute_llvm_candidates "${_directory}")
        endif()
    endforeach()

    file(GLOB _absolute_local_sdks LIST_DIRECTORIES TRUE
        "${CMAKE_SOURCE_DIR}/.absolute/toolchains/*/lib/cmake/llvm")
    list(APPEND _absolute_llvm_candidates ${_absolute_local_sdks})

    find_path(_absolute_llvm_dir LLVMConfig.cmake
        HINTS ${_absolute_llvm_candidates}
        DOC "Directory containing LLVMConfig.cmake")
    if(_absolute_llvm_dir)
        set(LLVM_DIR "${_absolute_llvm_dir}" CACHE PATH "Directory containing LLVMConfig.cmake" FORCE)
        message(STATUS "Auto-detected LLVM_DIR: ${LLVM_DIR}")
        return()
    endif()

    if(WIN32)
        message(FATAL_ERROR
            "Native LLVM SDK was not found. Absolute needs LLVM development files, not only clang.exe.\n"
            "Expected: <LLVM root>/lib/cmake/llvm/LLVMConfig.cmake\n"
            "Run build-windows.bat --bootstrap, or set ABSOLUTE_LLVM_ROOT/LLVM_DIR.")
    endif()
endfunction()
