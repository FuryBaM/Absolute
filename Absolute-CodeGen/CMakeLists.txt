cmake_minimum_required(VERSION 3.20)
project(Absolute-CodeGen LANGUAGES C CXX)

option(ABSOLUTE_LLVM_USE_LIBEDIT "Allow LLVM to use LibEdit integration" OFF)
option(ABSOLUTE_LLVM_USE_CURL "Allow LLVM to use CURL integration" OFF)

if(NOT ABSOLUTE_LLVM_USE_LIBEDIT)
    set(CMAKE_DISABLE_FIND_PACKAGE_LibEdit TRUE)
endif()
if(NOT ABSOLUTE_LLVM_USE_CURL)
    set(CMAKE_DISABLE_FIND_PACKAGE_CURL TRUE)
endif()

find_package(LLVM REQUIRED CONFIG)

if(LLVM_PACKAGE_VERSION VERSION_LESS 18)
    message(FATAL_ERROR "Absolute requires LLVM 18 or newer; found ${LLVM_PACKAGE_VERSION}")
endif()

if(LLVM_PACKAGE_VERSION VERSION_GREATER_EQUAL 19)
    message(WARNING "Absolute is validated with LLVM 18; LLVM ${LLVM_PACKAGE_VERSION} may require API adjustments")
endif()

# Official portable Windows LLVM 18 archives contain an absolute DIA SDK path
# from the machine that produced the archive. Replace it with the active Visual
# Studio installation so the SDK remains relocatable.
if(MSVC AND TARGET LLVMDebugInfoPDB)
    find_library(ABSOLUTE_DIAGUIDS_LIBRARY NAMES diaguids
        HINTS
            "$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Community/DIA SDK/lib/amd64"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Professional/DIA SDK/lib/amd64"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/2022/Enterprise/DIA SDK/lib/amd64")
    get_target_property(_absolute_pdb_links LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
    if(_absolute_pdb_links)
        set(_absolute_fixed_pdb_links)
        foreach(_absolute_link IN LISTS _absolute_pdb_links)
            if(_absolute_link MATCHES "diaguids\\.lib$" AND NOT EXISTS "${_absolute_link}")
                if(NOT ABSOLUTE_DIAGUIDS_LIBRARY)
                    message(FATAL_ERROR
                        "LLVM requires diaguids.lib, but the active Visual Studio DIA SDK was not found")
                endif()
                list(APPEND _absolute_fixed_pdb_links "${ABSOLUTE_DIAGUIDS_LIBRARY}")
                message(STATUS "Replaced non-relocatable LLVM DIA library with ${ABSOLUTE_DIAGUIDS_LIBRARY}")
            else()
                list(APPEND _absolute_fixed_pdb_links "${_absolute_link}")
            endif()
        endforeach()
        set_target_properties(LLVMDebugInfoPDB PROPERTIES
            INTERFACE_LINK_LIBRARIES "${_absolute_fixed_pdb_links}")
    endif()
endif()

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVMConfig.cmake: ${LLVM_DIR}")
message(STATUS "LLVM optional integrations: LibEdit=${ABSOLUTE_LLVM_USE_LIBEDIT}, CURL=${ABSOLUTE_LLVM_USE_CURL}")

add_library(Absolute-CodeGen STATIC
    include/codegen.h
    include/codegen_internal.h
    include/codegen_pch.h
    include/internal/codegen_abi.inc
    include/internal/codegen_module.inc
    include/internal/codegen_runtime.inc
    include/internal/codegen_state.inc
    include/internal/codegen_types.inc
    src/codegen.cpp
    src/codegen_abi.cpp
    src/codegen_access.cpp
    src/codegen_builtins.cpp
    src/codegen_core.cpp
    src/codegen_exceptions.cpp
    src/codegen_module.cpp
    src/codegen_operators.cpp
    src/codegen_pch.cpp
    src/codegen_runtime.cpp
    src/codegen_statements.cpp
    src/codegen_types.cpp
    src/codegen_values.cpp
)

target_precompile_headers(Absolute-CodeGen PRIVATE include/codegen_pch.h)

target_compile_features(Absolute-CodeGen PUBLIC cxx_std_20)
target_include_directories(Absolute-CodeGen PUBLIC include)
target_include_directories(Absolute-CodeGen SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

# nativecodegen = host backend; WebAssembly enables --target wasm32-... object emit.
llvm_map_components_to_libnames(ABSOLUTE_LLVM_LIBS
    Core
    Support
    Analysis
    Target
    CodeGen
    MC
    Passes
    AsmParser
    Linker
    nativecodegen
    WebAssembly
)

target_link_libraries(Absolute-CodeGen
    PUBLIC Absolute-Parser Absolute-Analyzer
    PRIVATE ${ABSOLUTE_LLVM_LIBS}
)

if(MSVC)
    target_compile_options(Absolute-CodeGen PRIVATE /W3 /permissive- /external:W0)
else()
    target_compile_options(Absolute-CodeGen PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(Absolute-CodeGen PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
