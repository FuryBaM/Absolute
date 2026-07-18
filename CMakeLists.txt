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
    add_test(
        NAME absolute.builtin-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/builtin-errors.abs"
    )
    set_tests_properties(absolute.builtin-errors PROPERTIES WILL_FAIL TRUE)
    foreach(module_error IN ITEMS missing-import unknown-namespace)
        add_test(
            NAME "absolute.${module_error}"
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/${module_error}.abs"
        )
        set_tests_properties("absolute.${module_error}" PROPERTIES WILL_FAIL TRUE)
    endforeach()
    add_test(
        NAME absolute.project
        COMMAND Absolute-Compiler build
            "${PROJECT_SOURCE_DIR}/tests/projects/multi-file/MultiFile.absproj"
    )
    if(ABSOLUTE_ENABLE_LLVM)
        add_test(
            NAME absolute.emit-llvm
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/llvm-basic.abs" --emit-llvm
        )
        set_tests_properties(absolute.emit-llvm PROPERTIES
            PASS_REGULAR_EXPRESSION "define i32 @main"
        )
        add_test(
            NAME absolute.emit-builtins
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/runtime-builtins.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/runtime-builtins.ll"
        )
        set_tests_properties(absolute.emit-builtins PROPERTIES
            FIXTURES_SETUP absolute-builtins-ir
        )

        find_program(ABSOLUTE_LLI_EXECUTABLE NAMES lli)
        if(ABSOLUTE_LLI_EXECUTABLE)
            add_test(
                NAME absolute.run-builtins
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/runtime-builtins.ll"
            )
            set_tests_properties(absolute.run-builtins PROPERTIES
                FIXTURES_REQUIRED absolute-builtins-ir
                PASS_REGULAR_EXPRESSION "before=40 after=42"
            )
        endif()
        add_test(
            NAME absolute.emit-project
            COMMAND Absolute-Compiler build
                "${PROJECT_SOURCE_DIR}/tests/projects/multi-file/MultiFile.absproj"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/multi-file-project.ll"
        )
        set_tests_properties(absolute.emit-project PROPERTIES
            FIXTURES_SETUP absolute-project-ir
        )
        if(ABSOLUTE_LLI_EXECUTABLE)
            add_test(
                NAME absolute.run-project
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/multi-file-project.ll"
            )
            set_tests_properties(absolute.run-project PROPERTIES
                FIXTURES_REQUIRED absolute-project-ir
                PASS_REGULAR_EXPRESSION "project=42"
            )
        endif()
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
    add_test(
        NAME absolute.invalid-extern-abi
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/invalid-extern-abi.abs" --parse-only
    )
    set_tests_properties(absolute.invalid-extern-abi PROPERTIES WILL_FAIL TRUE)

    if(ABSOLUTE_ENABLE_LLVM)
        add_test(
            NAME absolute.emit-native-ffi
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/native-ffi.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/native-ffi.ll"
        )
        set_tests_properties(absolute.emit-native-ffi PROPERTIES
            FIXTURES_SETUP absolute-native-ffi-ir
            PASS_REGULAR_EXPRESSION "declare i32 @abs\\(i32"
        )
        if(ABSOLUTE_LLI_EXECUTABLE)
            add_test(
                NAME absolute.run-native-ffi
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/native-ffi.ll"
            )
            set_tests_properties(absolute.run-native-ffi PROPERTIES
                FIXTURES_REQUIRED absolute-native-ffi-ir
                PASS_REGULAR_EXPRESSION "ffi=42"
            )
        endif()

        add_library(absolute-native-test STATIC tests/native/native_math.cpp)
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/NativeFfi.absproj" CONTENT
"{
  \"name\": \"NativeFfi\",
  \"entry\": \"${PROJECT_SOURCE_DIR}/tests/projects/native-ffi/src/main.abs\",
  \"sources\": [\"${PROJECT_SOURCE_DIR}/tests/projects/native-ffi/src\"],
  \"nativeLibraries\": [\"$<TARGET_FILE:absolute-native-test>\"],
  \"nativeSearchPaths\": []
}
")
        add_test(
            NAME absolute.emit-object
            COMMAND Absolute-Compiler build "${CMAKE_BINARY_DIR}/NativeFfi.absproj"
                --emit-object -o "${CMAKE_BINARY_DIR}/native-ffi-object.o"
        )
        add_test(
            NAME absolute.build-native
            COMMAND Absolute-Compiler build "${CMAKE_BINARY_DIR}/NativeFfi.absproj"
                --build-exe -o "${CMAKE_BINARY_DIR}/native-ffi-app"
        )
        set_tests_properties(absolute.build-native PROPERTIES
            FIXTURES_SETUP absolute-native-executable
        )
        add_test(NAME absolute.run-native COMMAND "${CMAKE_BINARY_DIR}/native-ffi-app")
        set_tests_properties(absolute.run-native PROPERTIES
            FIXTURES_REQUIRED absolute-native-executable
            PASS_REGULAR_EXPRESSION "native=42"
        )
    endif()
endif()
