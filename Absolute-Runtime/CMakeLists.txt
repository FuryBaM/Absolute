cmake_minimum_required(VERSION 3.20)
project(Absolute-Runtime LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(Absolute-Runtime STATIC
    src/binary.cpp
    src/datetime.cpp
    src/env.cpp
    src/errors.cpp
    src/filesystem.cpp
    src/json.cpp
    src/libraries.cpp
    src/network.cpp
    src/pointers.cpp
    src/process.cpp
    src/random.cpp
    src/string.cpp
    src/tasks.cpp
    src/time.cpp
)
target_compile_features(Absolute-Runtime PUBLIC cxx_std_20)
target_link_libraries(Absolute-Runtime PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
    target_link_libraries(Absolute-Runtime PUBLIC ws2_32 shell32)
endif()

if(MSVC)
    target_compile_options(Absolute-Runtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(Absolute-Runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(Absolute-Runtime PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Optional wasm32 runtime subset (clang --target=wasm32-unknown-unknown).
# LLVM may not be configured yet when this directory is processed, so search the
# portable toolchain and PATH explicitly.
#
# Prefer a host-native clang. The Windows portable SDK under .absolute/toolchains
# is visible from WSL via /mnt/*; never select PE clang.exe when configuring on
# a non-Windows host (that produces "Exec format error" at build time).
set(ABSOLUTE_WASM_RT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_runtime.c")
set(ABSOLUTE_WASM_SHIM_OBJECT "${CMAKE_BINARY_DIR}/absolute_wasm_runtime.o")
# Leave ABSOLUTE_WASM_CLANG unset until a real path is chosen. An empty normal
# variable would shadow find_program()'s cache result and hide PATH hits.
unset(ABSOLUTE_WASM_CLANG)
unset(ABSOLUTE_WASM_CLANG CACHE)
set(_absolute_wasm_clang_candidates)
if(DEFINED LLVM_TOOLS_BINARY_DIR AND LLVM_TOOLS_BINARY_DIR)
    list(APPEND _absolute_wasm_clang_candidates
        "${LLVM_TOOLS_BINARY_DIR}/clang${CMAKE_EXECUTABLE_SUFFIX}")
endif()
list(APPEND _absolute_wasm_clang_candidates
    "${CMAKE_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin/clang${CMAKE_EXECUTABLE_SUFFIX}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../.absolute/toolchains/llvm-18.1.8/bin/clang${CMAKE_EXECUTABLE_SUFFIX}")
if(WIN32)
    list(APPEND _absolute_wasm_clang_candidates
        "${CMAKE_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin/clang.exe"
        "${CMAKE_CURRENT_SOURCE_DIR}/../.absolute/toolchains/llvm-18.1.8/bin/clang.exe")
endif()
foreach(_absolute_clang_candidate IN LISTS _absolute_wasm_clang_candidates)
    if(NOT ABSOLUTE_WASM_CLANG AND EXISTS "${_absolute_clang_candidate}")
        set(ABSOLUTE_WASM_CLANG "${_absolute_clang_candidate}")
    endif()
endforeach()
if(NOT ABSOLUTE_WASM_CLANG)
    if(WIN32)
        find_program(ABSOLUTE_WASM_CLANG NAMES clang clang.exe clang-18 clang-19)
    else()
        find_program(ABSOLUTE_WASM_CLANG NAMES clang-18 clang-19 clang)
    endif()
endif()
if(ABSOLUTE_WASM_CLANG)
    set(ABSOLUTE_WASM_RT_FLAGS
        --target=wasm32-unknown-unknown
        -O2
        -std=c11
        -fno-builtin-malloc
        -fno-builtin-calloc
        -fno-builtin-realloc
        -fno-builtin-free
    )
    set(ABSOLUTE_WASM_RT_SHARED_FLAGS
        ${ABSOLUTE_WASM_RT_FLAGS}
        -matomics
        -mbulk-memory
        -DABSOLUTE_WASM_SHARED=1
    )
    add_custom_command(
        OUTPUT "${ABSOLUTE_WASM_SHIM_OBJECT}"
        COMMAND "${ABSOLUTE_WASM_CLANG}"
            ${ABSOLUTE_WASM_RT_FLAGS}
            -c "${ABSOLUTE_WASM_RT_SOURCE}"
            -o "${ABSOLUTE_WASM_SHIM_OBJECT}"
        DEPENDS "${ABSOLUTE_WASM_RT_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_platform.c"
        COMMENT "Building Absolute wasm runtime (host env.absolute_log)"
        VERBATIM
    )
    set(ABSOLUTE_WASM_WASI_OBJECT "${CMAKE_BINARY_DIR}/absolute_wasm_runtime_wasi.o")
    add_custom_command(
        OUTPUT "${ABSOLUTE_WASM_WASI_OBJECT}"
        COMMAND "${ABSOLUTE_WASM_CLANG}"
            ${ABSOLUTE_WASM_RT_FLAGS}
            -DABSOLUTE_WASM_USE_WASI=1
            -c "${ABSOLUTE_WASM_RT_SOURCE}"
            -o "${ABSOLUTE_WASM_WASI_OBJECT}"
        DEPENDS "${ABSOLUTE_WASM_RT_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_platform.c"
        COMMENT "Building Absolute wasm runtime (WASI fd_write console)"
        VERBATIM
    )
    set(ABSOLUTE_WASM_SHARED_OBJECT "${CMAKE_BINARY_DIR}/absolute_wasm_runtime_shared.o")
    add_custom_command(
        OUTPUT "${ABSOLUTE_WASM_SHARED_OBJECT}"
        COMMAND "${ABSOLUTE_WASM_CLANG}"
            ${ABSOLUTE_WASM_RT_SHARED_FLAGS}
            -c "${ABSOLUTE_WASM_RT_SOURCE}"
            -o "${ABSOLUTE_WASM_SHARED_OBJECT}"
        DEPENDS "${ABSOLUTE_WASM_RT_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_platform.c"
        COMMENT "Building Absolute wasm runtime (shared-memory + atomics heap lock)"
        VERBATIM
    )
    add_custom_target(Absolute-Runtime-WasmShim ALL
        DEPENDS "${ABSOLUTE_WASM_SHIM_OBJECT}" "${ABSOLUTE_WASM_WASI_OBJECT}"
            "${ABSOLUTE_WASM_SHARED_OBJECT}")
    set(ABSOLUTE_WASM_SHIM_OBJECT "${ABSOLUTE_WASM_SHIM_OBJECT}" CACHE FILEPATH
        "wasm32 Absolute runtime object (host imports)" FORCE)
    set(ABSOLUTE_WASM_WASI_OBJECT "${ABSOLUTE_WASM_WASI_OBJECT}" CACHE FILEPATH
        "wasm32 Absolute runtime object (WASI console)" FORCE)
    set(ABSOLUTE_WASM_SHARED_OBJECT "${ABSOLUTE_WASM_SHARED_OBJECT}" CACHE FILEPATH
        "wasm32 Absolute runtime object (shared memory)" FORCE)
    message(STATUS "Absolute wasm runtime: ${ABSOLUTE_WASM_SHIM_OBJECT}")
    message(STATUS "Absolute wasm WASI runtime: ${ABSOLUTE_WASM_WASI_OBJECT}")
    message(STATUS "Absolute wasm shared runtime: ${ABSOLUTE_WASM_SHARED_OBJECT}")
else()
    set(ABSOLUTE_WASM_SHIM_OBJECT "" CACHE FILEPATH
        "wasm32 Absolute runtime object (host imports)" FORCE)
    set(ABSOLUTE_WASM_WASI_OBJECT "" CACHE FILEPATH
        "wasm32 Absolute runtime object (WASI console)" FORCE)
    set(ABSOLUTE_WASM_SHARED_OBJECT "" CACHE FILEPATH
        "wasm32 Absolute runtime object (shared memory)" FORCE)
    message(STATUS "clang for wasm32 not found; Absolute wasm runtime will not be built")
endif()
