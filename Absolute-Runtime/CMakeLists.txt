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

# Optional wasm32 console/assert shim (clang --target=wasm32-unknown-unknown).
# LLVM may not be configured yet when this directory is processed, so search the
# portable toolchain and PATH explicitly.
set(ABSOLUTE_WASM_SHIM_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_shim.c")
set(ABSOLUTE_WASM_SHIM_OBJECT "${CMAKE_BINARY_DIR}/absolute_wasm_shim.o")
set(ABSOLUTE_WASM_CLANG "")
foreach(_absolute_clang_candidate IN ITEMS
        "${LLVM_TOOLS_BINARY_DIR}/clang${CMAKE_EXECUTABLE_SUFFIX}"
        "${CMAKE_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin/clang${CMAKE_EXECUTABLE_SUFFIX}"
        "${CMAKE_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin/clang.exe"
        "${CMAKE_CURRENT_SOURCE_DIR}/../.absolute/toolchains/llvm-18.1.8/bin/clang.exe")
    if(NOT ABSOLUTE_WASM_CLANG AND EXISTS "${_absolute_clang_candidate}")
        set(ABSOLUTE_WASM_CLANG "${_absolute_clang_candidate}")
    endif()
endforeach()
if(NOT ABSOLUTE_WASM_CLANG)
    find_program(ABSOLUTE_WASM_CLANG NAMES clang clang.exe clang-18 clang-19)
endif()
if(ABSOLUTE_WASM_CLANG)
    add_custom_command(
        OUTPUT "${ABSOLUTE_WASM_SHIM_OBJECT}"
        COMMAND "${ABSOLUTE_WASM_CLANG}"
            --target=wasm32-unknown-unknown
            -c "${ABSOLUTE_WASM_SHIM_SOURCE}"
            -o "${ABSOLUTE_WASM_SHIM_OBJECT}"
            -O2
        DEPENDS "${ABSOLUTE_WASM_SHIM_SOURCE}"
        COMMENT "Building Absolute wasm runtime shim"
        VERBATIM
    )
    add_custom_target(Absolute-Runtime-WasmShim ALL DEPENDS "${ABSOLUTE_WASM_SHIM_OBJECT}")
    set(ABSOLUTE_WASM_SHIM_OBJECT "${ABSOLUTE_WASM_SHIM_OBJECT}" CACHE FILEPATH
        "wasm32 object providing puts/printf/abort stubs" FORCE)
    message(STATUS "Absolute wasm shim: ${ABSOLUTE_WASM_SHIM_OBJECT} (clang=${ABSOLUTE_WASM_CLANG})")
else()
    set(ABSOLUTE_WASM_SHIM_OBJECT "" CACHE FILEPATH
        "wasm32 object providing puts/printf/abort stubs" FORCE)
    message(STATUS "clang for wasm32 not found; Absolute wasm shim will not be built")
endif()
