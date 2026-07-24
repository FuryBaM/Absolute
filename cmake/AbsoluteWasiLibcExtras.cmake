# Selective wasi-libc archive members for Absolute coexistence.
# Full libc.a cannot be linked: wasm-ld rejects duplicates (malloc/exit/printf/…).
#
# Kits pull only the objects needed for a small surface area. Expand carefully:
# each new root symbol (strtod, qsort, …) may drag stdio/WASI fd helpers.

# Integer conversion (strtol) — minimal kit.
set(ABSOLUTE_WASI_LIBC_KIT_STRTOL
    strtol.o
    shgetc.o
    intscan.o
    __uflow.o
    __toread.o
    __stdio_exit.o
    ofl.o
)

# Floating conversion (strtod) + strtol. Closed set validated against wasi-sdk-25.
set(ABSOLUTE_WASI_LIBC_KIT_STRTOD
    strtol.o
    strtod.o
    atof.o
    shgetc.o
    intscan.o
    floatscan.o
    fputs.o
    __uflow.o
    scalbn.o
    fmod.o
    fwrite.o
    __toread.o
    __towrite.o
    __stdio_exit.o
    ofl.o
    errno.o
    stderr.o
    __stdio_close.o
    __stdio_write.o
    __stdio_seek.o
    __wasilibc_fd_renumber.o
    writev.o
    lseek.o
    __wasilibc_real.o
)

# Default kit used by the coexistence test.
set(ABSOLUTE_WASI_LIBC_KIT_DEFAULT ${ABSOLUTE_WASI_LIBC_KIT_STRTOD})

# Extract kit objects from libc.a into OUT_DIR; sets OUT_VAR to list of paths.
function(absolute_wasi_extract_kit libc_a out_dir kit_var out_var)
    if(NOT EXISTS "${libc_a}")
        message(FATAL_ERROR "libc.a missing: ${libc_a}")
    endif()
    find_program(ABSOLUTE_LLVM_AR NAMES llvm-ar llvm-ar.exe
        HINTS
            "${CMAKE_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin"
            "${CMAKE_BINARY_DIR}/../.absolute/toolchains/llvm-18.1.8/bin"
            "${LLVM_TOOLS_BINARY_DIR}")
    if(NOT ABSOLUTE_LLVM_AR)
        message(FATAL_ERROR "llvm-ar required to extract wasi-libc objects")
    endif()
    file(MAKE_DIRECTORY "${out_dir}")
    execute_process(
        COMMAND "${ABSOLUTE_LLVM_AR}" x "${libc_a}"
        WORKING_DIRECTORY "${out_dir}"
        RESULT_VARIABLE _ar_status
        OUTPUT_VARIABLE _ar_out
        ERROR_VARIABLE _ar_err
    )
    if(NOT _ar_status EQUAL 0)
        message(FATAL_ERROR "llvm-ar x failed: ${_ar_out}\n${_ar_err}")
    endif()
    set(_paths "")
    foreach(_obj IN LISTS ${kit_var})
        set(_p "${out_dir}/${_obj}")
        if(NOT EXISTS "${_p}")
            message(FATAL_ERROR "wasi-libc kit object missing after extract: ${_obj}")
        endif()
        list(APPEND _paths "${_p}")
    endforeach()
    set(${out_var} "${_paths}" PARENT_SCOPE)
endfunction()

# Optional compiler-rt builtins (provides __multi3 etc. for wasi-libc).
function(absolute_wasi_find_builtins out_var)
    file(GLOB _builtin_candidates
        "${CMAKE_SOURCE_DIR}/.absolute/toolchains/wasi-builtins-*/**/libclang_rt.builtins-wasm32.a"
        "${CMAKE_BINARY_DIR}/../.absolute/toolchains/wasi-builtins-*/**/libclang_rt.builtins-wasm32.a"
        "${CMAKE_SOURCE_DIR}/.absolute/toolchains/wasi-sysroot-*/lib/wasm32-wasi/libclang_rt.builtins-wasm32.a"
    )
    if(_builtin_candidates)
        list(GET _builtin_candidates 0 _first)
        set(${out_var} "${_first}" PARENT_SCOPE)
    else()
        set(${out_var} "" PARENT_SCOPE)
    endif()
endfunction()
