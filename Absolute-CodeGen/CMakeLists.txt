cmake_minimum_required(VERSION 3.20)
project(Absolute-CodeGen LANGUAGES CXX)

find_package(LLVM REQUIRED CONFIG)

message(STATUS "Using LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVMConfig.cmake: ${LLVM_DIR}")

add_library(Absolute-CodeGen STATIC
    include/codegen.h
    include/codegen_internal.h
    include/codegen_pch.h
    include/internal/codegen_module.inc
    include/internal/codegen_runtime.inc
    include/internal/codegen_state.inc
    include/internal/codegen_types.inc
    src/codegen.cpp
    src/codegen_access.cpp
    src/codegen_builtins.cpp
    src/codegen_core.cpp
    src/codegen_module.cpp
    src/codegen_operators.cpp
    src/codegen_pch.cpp
    src/codegen_runtime.cpp
    src/codegen_statements.cpp
    src/codegen_types.cpp
    src/codegen_values.cpp
)

target_precompile_headers(Absolute-CodeGen PRIVATE include/codegen_internal.h)

target_compile_features(Absolute-CodeGen PUBLIC cxx_std_20)
target_include_directories(Absolute-CodeGen PUBLIC include)
target_include_directories(Absolute-CodeGen SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})

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
)

target_link_libraries(Absolute-CodeGen
    PUBLIC Absolute-Parser Absolute-Analyzer
    PRIVATE ${ABSOLUTE_LLVM_LIBS}
)

if(MSVC)
    target_compile_options(Absolute-CodeGen PRIVATE /W3 /permissive-)
else()
    target_compile_options(Absolute-CodeGen PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(Absolute-CodeGen PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
