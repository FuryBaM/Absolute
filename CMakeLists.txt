cmake_minimum_required(VERSION 3.20)
project(Absolute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ABSOLUTE_ENABLE_LLVM "Build the LLVM IR backend" ON)

# Добавляем подпроекты
add_subdirectory(Absolute-Parser)
add_subdirectory(Absolute-Analyzer)
if(ABSOLUTE_ENABLE_LLVM)
    add_subdirectory(Absolute-CodeGen)
endif()
add_subdirectory(Absolute-Compiler)

# Keep generated artifacts out of the source tree.

include(CTest)
if(BUILD_TESTING)
    add_test(
        NAME absolute.sample
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/code.abs" --parse-only
    )
    add_test(
        NAME absolute.comments
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/comments.abs"
    )
    add_test(
        NAME absolute.templates
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/templates.abs" --parse-only
    )
    add_test(
        NAME absolute.semantic-members
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/semantic-members.abs"
    )
    add_test(
        NAME absolute.semantic-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/semantic-errors.abs"
    )
    set_tests_properties(absolute.semantic-errors PROPERTIES WILL_FAIL TRUE)
    if(ABSOLUTE_ENABLE_LLVM)
        add_test(
            NAME absolute.emit-llvm
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/llvm-basic.abs" --emit-llvm
        )
        set_tests_properties(absolute.emit-llvm PROPERTIES
            PASS_REGULAR_EXPRESSION "define i32 @main"
        )
    endif()
    add_test(
        NAME absolute.invalid-character
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/invalid-character.abs"
    )
    set_tests_properties(absolute.invalid-character PROPERTIES WILL_FAIL TRUE)

    add_test(NAME absolute.requires-source COMMAND Absolute-Compiler)
    set_tests_properties(absolute.requires-source PROPERTIES WILL_FAIL TRUE)

    foreach(invalid_case IN ITEMS incomplete-template incomplete-array unterminated-block legacy-template-marker)
        add_test(
            NAME "absolute.${invalid_case}"
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/${invalid_case}.abs"
        )
        set_tests_properties("absolute.${invalid_case}" PROPERTIES WILL_FAIL TRUE)
    endforeach()
endif()
