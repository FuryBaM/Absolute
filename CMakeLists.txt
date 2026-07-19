cmake_minimum_required(VERSION 3.20)
project(Absolute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ABSOLUTE_ENABLE_LLVM "Build the LLVM IR backend" ON)

# Добавляем подпроекты
add_subdirectory(Absolute-Runtime)
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
    add_test(
        NAME absolute.pointers-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/pointers.abs"
    )
    add_test(
        NAME absolute.pointer-functions-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/pointer-functions.abs"
    )
    add_test(
        NAME absolute.managed-fast-path-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-fast-path.abs"
    )
    add_test(
        NAME absolute.managed-role-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-role-errors.abs"
    )
    set_tests_properties(absolute.managed-role-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.pointer-subscriber-return
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/pointer-subscriber-return.abs"
    )
    set_tests_properties(absolute.pointer-subscriber-return PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.raw-lifetime-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/raw-lifetime.abs"
    )
    add_test(
        NAME absolute.raw-missing-delete
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/raw-missing-delete.abs"
    )
    set_tests_properties(absolute.raw-missing-delete PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.raw-invalid-operations
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/raw-invalid-operations.abs"
    )
    set_tests_properties(absolute.raw-invalid-operations PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.removed-keep
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/removed-keep.abs"
    )
    set_tests_properties(absolute.removed-keep PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.flow-analysis-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/flow-analysis.abs"
    )
    foreach(flow_error IN ITEMS uninitialized-errors pointer-flow-errors)
        add_test(
            NAME "absolute.${flow_error}"
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/${flow_error}.abs"
        )
        set_tests_properties("absolute.${flow_error}" PROPERTIES WILL_FAIL TRUE)
    endforeach()
    add_test(
        NAME absolute.async-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-basic.abs"
    )
    add_test(
        NAME absolute.async-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-errors.abs"
    )
    set_tests_properties(absolute.async-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.array-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-errors.abs"
    )
    set_tests_properties(absolute.array-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.array-advanced-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-advanced.abs"
    )
    add_test(
        NAME absolute.classes-codegen-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/classes-codegen.abs"
    )
    add_test(
        NAME absolute.array-advanced-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-advanced-errors.abs"
    )
    set_tests_properties(absolute.array-advanced-errors PROPERTIES WILL_FAIL TRUE)
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
        add_test(
            NAME absolute.emit-arrays
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/arrays.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/arrays.ll"
        )
        set_tests_properties(absolute.emit-arrays PROPERTIES
            FIXTURES_SETUP absolute-arrays-ir
        )
        add_test(
            NAME absolute.emit-array-constant-access
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-constant-access.abs"
                --emit-llvm
        )
        set_tests_properties(absolute.emit-array-constant-access PROPERTIES
            PASS_REGULAR_EXPRESSION "getelementptr inbounds"
            FAIL_REGULAR_EXPRESSION "array.bounds"
        )
        add_test(
            NAME absolute.emit-array-out-of-bounds
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-out-of-bounds.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/array-out-of-bounds.ll"
        )
        set_tests_properties(absolute.emit-array-out-of-bounds PROPERTIES
            FIXTURES_SETUP absolute-array-out-of-bounds-ir
        )
        add_test(
            NAME absolute.emit-array-advanced
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-advanced.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/array-advanced.ll"
        )
        set_tests_properties(absolute.emit-array-advanced PROPERTIES
            FIXTURES_SETUP absolute-array-advanced-ir
        )
        add_test(
            NAME absolute.emit-classes-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/classes-codegen.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/classes-codegen.ll"
        )
        set_tests_properties(absolute.emit-classes-codegen PROPERTIES
            FIXTURES_SETUP absolute-classes-codegen-ir
        )
        add_test(
            NAME absolute.emit-managed-fast-path
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-fast-path.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/managed-fast-path.ll"
        )
        set_tests_properties(absolute.emit-managed-fast-path PROPERTIES
            FIXTURES_SETUP absolute-managed-fast-path-ir
        )
        add_test(
            NAME absolute.check-managed-fast-path
            COMMAND "${CMAKE_COMMAND}"
                -DIR_FILE=${CMAKE_BINARY_DIR}/managed-fast-path.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-managed-fast-path.cmake"
        )
        set_tests_properties(absolute.check-managed-fast-path PROPERTIES
            FIXTURES_REQUIRED absolute-managed-fast-path-ir
        )
        add_test(
            NAME absolute.emit-slice-out-of-bounds
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/slice-out-of-bounds.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/slice-out-of-bounds.ll"
        )
        set_tests_properties(absolute.emit-slice-out-of-bounds PROPERTIES
            FIXTURES_SETUP absolute-slice-out-of-bounds-ir
        )

        find_program(ABSOLUTE_LLI_EXECUTABLE NAMES lli lli-18)
        if(ABSOLUTE_LLI_EXECUTABLE)
            add_test(
                NAME absolute.run-builtins
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/runtime-builtins.ll"
            )
            set_tests_properties(absolute.run-builtins PROPERTIES
                FIXTURES_REQUIRED absolute-builtins-ir
                PASS_REGULAR_EXPRESSION "before=40 after=42"
            )
            add_test(
                NAME absolute.run-arrays
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/arrays.ll"
            )
            set_tests_properties(absolute.run-arrays PROPERTIES
                FIXTURES_REQUIRED absolute-arrays-ir
                PASS_REGULAR_EXPRESSION "arrays=90"
            )
            add_test(
                NAME absolute.run-array-out-of-bounds
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/array-out-of-bounds.ll"
            )
            set_tests_properties(absolute.run-array-out-of-bounds PROPERTIES
                FIXTURES_REQUIRED absolute-array-out-of-bounds-ir
                WILL_FAIL TRUE
            )
            add_test(
                NAME absolute.run-array-advanced
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/array-advanced.ll"
            )
            set_tests_properties(absolute.run-array-advanced PROPERTIES
                FIXTURES_REQUIRED absolute-array-advanced-ir
                PASS_REGULAR_EXPRESSION "advanced-arrays=227"
            )
            add_test(
                NAME absolute.run-classes-codegen
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/classes-codegen.ll"
            )
            set_tests_properties(absolute.run-classes-codegen PROPERTIES
                FIXTURES_REQUIRED absolute-classes-codegen-ir
                PASS_REGULAR_EXPRESSION "classes=42"
            )
            add_test(
                NAME absolute.run-slice-out-of-bounds
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/slice-out-of-bounds.ll"
            )
            set_tests_properties(absolute.run-slice-out-of-bounds PROPERTIES
                FIXTURES_REQUIRED absolute-slice-out-of-bounds-ir
                WILL_FAIL TRUE
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
        add_test(
            NAME absolute.build-pointers
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/pointers.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/pointers-app"
        )
        set_tests_properties(absolute.build-pointers PROPERTIES
            FIXTURES_SETUP absolute-pointers-executable
        )
        add_test(NAME absolute.run-pointers COMMAND "${CMAKE_BINARY_DIR}/pointers-app")
        set_tests_properties(absolute.run-pointers PROPERTIES
            FIXTURES_REQUIRED absolute-pointers-executable
            PASS_REGULAR_EXPRESSION "pointers=ok"
        )
        add_test(
            NAME absolute.build-pointer-functions
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/pointer-functions.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/pointer-functions-app"
        )
        set_tests_properties(absolute.build-pointer-functions PROPERTIES
            FIXTURES_SETUP absolute-pointer-functions-executable
        )
        add_test(NAME absolute.run-pointer-functions COMMAND "${CMAKE_BINARY_DIR}/pointer-functions-app")
        set_tests_properties(absolute.run-pointer-functions PROPERTIES
            FIXTURES_REQUIRED absolute-pointer-functions-executable
            PASS_REGULAR_EXPRESSION "pointer-functions=ok"
        )
        add_test(
            NAME absolute.build-managed-fast-path
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-fast-path.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/managed-fast-path-app"
        )
        set_tests_properties(absolute.build-managed-fast-path PROPERTIES
            FIXTURES_SETUP absolute-managed-fast-path-executable
        )
        add_test(NAME absolute.run-managed-fast-path COMMAND "${CMAKE_BINARY_DIR}/managed-fast-path-app")
        set_tests_properties(absolute.run-managed-fast-path PROPERTIES
            FIXTURES_REQUIRED absolute-managed-fast-path-executable
            PASS_REGULAR_EXPRESSION "managed-fast-path="
        )
        add_test(
            NAME absolute.build-raw-lifetime
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/raw-lifetime.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/raw-lifetime-app"
        )
        set_tests_properties(absolute.build-raw-lifetime PROPERTIES
            FIXTURES_SETUP absolute-raw-lifetime-executable
        )
        add_test(NAME absolute.run-raw-lifetime COMMAND "${CMAKE_BINARY_DIR}/raw-lifetime-app")
        set_tests_properties(absolute.run-raw-lifetime PROPERTIES
            FIXTURES_REQUIRED absolute-raw-lifetime-executable
            PASS_REGULAR_EXPRESSION "raw-lifetime=ok"
        )
        add_test(
            NAME absolute.build-flow-analysis
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/flow-analysis.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/flow-analysis-app"
        )
        set_tests_properties(absolute.build-flow-analysis PROPERTIES
            FIXTURES_SETUP absolute-flow-analysis-executable
        )
        add_test(NAME absolute.run-flow-analysis COMMAND "${CMAKE_BINARY_DIR}/flow-analysis-app")
        set_tests_properties(absolute.run-flow-analysis PROPERTIES
            FIXTURES_REQUIRED absolute-flow-analysis-executable
            PASS_REGULAR_EXPRESSION "flow-analysis=ok"
        )
        add_test(
            NAME absolute.build-async
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-basic.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/async-app"
        )
        set_tests_properties(absolute.build-async PROPERTIES
            FIXTURES_SETUP absolute-async-executable
        )
        add_test(NAME absolute.run-async COMMAND "${CMAKE_BINARY_DIR}/async-app")
        set_tests_properties(absolute.run-async PROPERTIES
            FIXTURES_REQUIRED absolute-async-executable
            PASS_REGULAR_EXPRESSION "async=42"
        )
    endif()
endif()
