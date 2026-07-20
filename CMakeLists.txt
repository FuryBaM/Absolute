cmake_minimum_required(VERSION 3.20)
project(Absolute LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ABSOLUTE_ENABLE_LLVM "Build the LLVM IR backend" ON)
option(ABSOLUTE_BUILD_EXAMPLE_PLUGINS "Build example syntax plugins" ON)
option(ABSOLUTE_USE_COMPILER_CACHE "Use sccache or ccache when available" OFF)
set(ABSOLUTE_LLVM_ROOT "" CACHE PATH "Root of a native LLVM SDK containing lib/cmake/llvm/LLVMConfig.cmake")

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AbsoluteLLVM)

if(MSVC AND CMAKE_GENERATOR MATCHES "Visual Studio")
    add_compile_options(/MP)
endif()

if(ABSOLUTE_USE_COMPILER_CACHE)
    find_program(ABSOLUTE_COMPILER_CACHE NAMES sccache ccache)
    if(ABSOLUTE_COMPILER_CACHE)
        message(STATUS "Using compiler cache: ${ABSOLUTE_COMPILER_CACHE}")
        set(CMAKE_CXX_COMPILER_LAUNCHER "${ABSOLUTE_COMPILER_CACHE}" CACHE FILEPATH
            "Compiler cache launcher" FORCE)
    else()
        message(WARNING "ABSOLUTE_USE_COMPILER_CACHE is ON, but neither sccache nor ccache was found")
    endif()
endif()

if(ABSOLUTE_ENABLE_LLVM)
    absolute_find_llvm()
endif()

# Добавляем подпроекты
add_subdirectory(Absolute-Runtime)
add_subdirectory(Absolute-Parser)
add_subdirectory(Absolute-Analyzer)
if(ABSOLUTE_ENABLE_LLVM)
    add_subdirectory(Absolute-CodeGen)
endif()
add_subdirectory(Absolute-Compiler)
if(ABSOLUTE_BUILD_EXAMPLE_PLUGINS)
    add_subdirectory(plugins/unless)
    add_subdirectory(plugins/math)
    add_subdirectory(plugins/shader)
    add_subdirectory(plugins/desktop)
endif()

# Keep generated artifacts out of the source tree.

include(CTest)
if(BUILD_TESTING)
    if(TARGET Absolute-Unless-Plugin)
        add_test(
            NAME absolute.syntax-plugin
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/syntax-plugin.abs"
                --plugin "$<TARGET_FILE:Absolute-Unless-Plugin>"
        )
        set_tests_properties(absolute.syntax-plugin PROPERTIES
            PASS_REGULAR_EXPRESSION "Loaded syntax plugin absolute.unless"
        )
        add_test(
            NAME absolute.syntax-plugin-required
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/syntax-plugin.abs"
        )
        set_tests_properties(absolute.syntax-plugin-required PROPERTIES WILL_FAIL TRUE)
        add_test(
            NAME absolute.syntax-plugin-project
            COMMAND "${CMAKE_COMMAND}"
                -DCOMPILER=$<TARGET_FILE:Absolute-Compiler>
                -DPLUGIN=$<TARGET_FILE:Absolute-Unless-Plugin>
                -DWORK_DIR=${CMAKE_BINARY_DIR}/syntax-plugin-project
                -P "${PROJECT_SOURCE_DIR}/tests/run-syntax-plugin-project.cmake"
        )
    endif()
    if(TARGET Absolute-Math-Plugin)
        add_test(
            NAME absolute.math-plugin
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/math-plugin.abs"
                --plugin "$<TARGET_FILE:Absolute-Math-Plugin>"
        )
        set_tests_properties(absolute.math-plugin PROPERTIES
            PASS_REGULAR_EXPRESSION "Loaded syntax plugin absolute.math"
        )
        add_test(
            NAME absolute.math-plugin-required
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/math-plugin.abs"
        )
        set_tests_properties(absolute.math-plugin-required PROPERTIES WILL_FAIL TRUE)
    endif()
    if(TARGET Absolute-Shader-Plugin)
        add_test(
            NAME absolute.shader-plugin
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/shader-plugin.abs"
                --plugin "$<TARGET_FILE:Absolute-Shader-Plugin>"
        )
        set_tests_properties(absolute.shader-plugin PROPERTIES
            PASS_REGULAR_EXPRESSION "Opaque plugin statement.*shader Vertex"
        )
        add_test(
            NAME absolute.shader-plugin-required
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/shader-plugin.abs"
        )
        set_tests_properties(absolute.shader-plugin-required PROPERTIES WILL_FAIL TRUE)
        add_test(
            NAME absolute.shader-plugin-errors
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/shader-plugin-errors.abs"
                --plugin "$<TARGET_FILE:Absolute-Shader-Plugin>"
        )
        set_tests_properties(absolute.shader-plugin-errors PROPERTIES WILL_FAIL TRUE)
    endif()
    if(TARGET Absolute-Math-Plugin AND TARGET Absolute-Shader-Plugin)
        add_test(
            NAME absolute.plugin-dependencies
            COMMAND "${CMAKE_COMMAND}"
                -DCOMPILER=$<TARGET_FILE:Absolute-Compiler>
                -DMATH_PLUGIN=$<TARGET_FILE:Absolute-Math-Plugin>
                -DSHADER_PLUGIN=$<TARGET_FILE:Absolute-Shader-Plugin>
                -DSOURCE=${PROJECT_SOURCE_DIR}/tests/shader-plugin.abs
                -DWORK_DIR=${CMAKE_BINARY_DIR}/plugin-dependencies
                -P "${PROJECT_SOURCE_DIR}/tests/run-plugin-dependencies.cmake"
        )
    endif()
    if(TARGET Absolute-Desktop-Plugin)
        set(ABSOLUTE_DESKTOP_MANIFEST
            "$<TARGET_FILE_DIR:Absolute-Desktop-Plugin>/absolute-desktop.absplugin")
        add_test(
            NAME absolute.desktop-plugin
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/desktop-plugin.abs"
                --plugin "${ABSOLUTE_DESKTOP_MANIFEST}"
        )
        set_tests_properties(absolute.desktop-plugin PROPERTIES
            PASS_REGULAR_EXPRESSION "Loaded syntax plugin absolute.desktop v1.0.0"
        )
        if(ABSOLUTE_ENABLE_LLVM)
            add_test(
                NAME absolute.build-desktop-plugin
                COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/desktop-plugin.abs"
                    --plugin "${ABSOLUTE_DESKTOP_MANIFEST}"
                    --build-exe -o "${CMAKE_BINARY_DIR}/desktop-plugin${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.build-desktop-plugin PROPERTIES
                FIXTURES_SETUP absolute-desktop-plugin-executable
            )
            add_test(NAME absolute.run-desktop-plugin
                COMMAND "${CMAKE_BINARY_DIR}/desktop-plugin${CMAKE_EXECUTABLE_SUFFIX}")
            set_tests_properties(absolute.run-desktop-plugin PROPERTIES
                FIXTURES_REQUIRED absolute-desktop-plugin-executable
                PASS_REGULAR_EXPRESSION "desktop-plugin=4227327"
            )
        endif()
    endif()
    add_test(
        NAME absolute.sample
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/code.abs" --parse-only
    )
    add_test(
        NAME absolute.overloads-extensions
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/overloads-extensions.abs"
    )
    add_test(
        NAME absolute.overloads-extensions-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/overloads-extensions-errors.abs"
    )
    set_tests_properties(absolute.overloads-extensions-errors PROPERTIES WILL_FAIL TRUE)
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
    add_test(
        NAME absolute.switch-match-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/switch-match.abs"
    )
    add_test(
        NAME absolute.switch-match-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/switch-match-errors.abs"
    )
    set_tests_properties(absolute.switch-match-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.exceptions-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/exceptions.abs"
    )
    add_test(
        NAME absolute.exceptions-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/exceptions-errors.abs"
    )
    set_tests_properties(absolute.exceptions-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.defer-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/defer.abs"
    )
    add_test(
        NAME absolute.defer-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/defer-errors.abs"
    )
    set_tests_properties(absolute.defer-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.attributes-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/attributes.abs"
    )
    add_test(
        NAME absolute.attribute-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/attribute-errors.abs"
    )
    set_tests_properties(absolute.attribute-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.generic-functions-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generics-functions.abs"
    )
    add_test(
        NAME absolute.generic-types-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generics-types.abs"
    )
    add_test(
        NAME absolute.generics-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generics-errors.abs"
    )
    set_tests_properties(absolute.generics-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.base-language-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/base-language.abs"
    )
    add_test(
        NAME absolute.base-language-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/base-language-errors.abs"
    )
    set_tests_properties(absolute.base-language-errors PROPERTIES WILL_FAIL TRUE)
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
        NAME absolute.array-copy-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-copy.abs"
    )
    add_test(
        NAME absolute.array-copy-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-copy-errors.abs"
    )
    set_tests_properties(absolute.array-copy-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.chess-rules-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/chess-rules.abs"
    )
    add_test(
        NAME absolute.classes-codegen-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/classes-codegen.abs"
    )
    add_test(
        NAME absolute.interfaces-codegen-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interfaces-codegen.abs"
    )
    add_test(
        NAME absolute.interface-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interface-errors.abs"
    )
    set_tests_properties(absolute.interface-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.interface-default-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interface-default-errors.abs"
    )
    set_tests_properties(absolute.interface-default-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.interface-default-body-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interface-default-body-errors.abs"
    )
    set_tests_properties(absolute.interface-default-body-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.structs-codegen-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/structs-codegen.abs"
    )
    add_test(
        NAME absolute.static-members-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/static-members.abs"
    )
    add_test(
        NAME absolute.static-member-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/static-member-errors.abs"
    )
    set_tests_properties(absolute.static-member-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.base-constructors-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/base-constructors.abs"
    )
    add_test(
        NAME absolute.base-constructor-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/base-constructor-errors.abs"
    )
    set_tests_properties(absolute.base-constructor-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.access-control-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/access-control.abs"
    )
    add_test(
        NAME absolute.access-control-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/access-control-errors.abs"
    )
    set_tests_properties(absolute.access-control-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.runtime-type-operations-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/runtime-type-operations.abs"
    )
    add_test(
        NAME absolute.runtime-type-operation-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/runtime-type-operation-errors.abs"
    )
    set_tests_properties(absolute.runtime-type-operation-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.array-advanced-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-advanced-errors.abs"
    )
    set_tests_properties(absolute.array-advanced-errors PROPERTIES WILL_FAIL TRUE)
    if(ABSOLUTE_ENABLE_LLVM)
        if(TARGET Absolute-Unless-Plugin)
            add_test(
                NAME absolute.emit-syntax-plugin
                COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/syntax-plugin.abs"
                    --plugin "$<TARGET_FILE:Absolute-Unless-Plugin>"
                    --emit-llvm -o "${CMAKE_BINARY_DIR}/syntax-plugin.ll"
            )
            set_tests_properties(absolute.emit-syntax-plugin PROPERTIES
                FIXTURES_SETUP absolute-syntax-plugin-ir
            )
        endif()
        if(TARGET Absolute-Math-Plugin)
            add_test(
                NAME absolute.build-math-plugin
                COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/math-plugin.abs"
                    --plugin "$<TARGET_FILE:Absolute-Math-Plugin>"
                    --build-exe -o "${CMAKE_BINARY_DIR}/absolute-math-plugin${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.build-math-plugin PROPERTIES
                FIXTURES_SETUP absolute-math-plugin-executable
            )
            add_test(
                NAME absolute.run-math-plugin
                COMMAND "${CMAKE_BINARY_DIR}/absolute-math-plugin${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.run-math-plugin PROPERTIES
                FIXTURES_REQUIRED absolute-math-plugin-executable
                PASS_REGULAR_EXPRESSION "math-plugin=ok"
            )
        endif()
        if(TARGET Absolute-Shader-Plugin)
            add_test(
                NAME absolute.emit-shader-plugin
                COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/shader-plugin.abs"
                    --plugin "$<TARGET_FILE:Absolute-Shader-Plugin>"
                    --emit-llvm -o "${CMAKE_BINARY_DIR}/shader-plugin.ll"
            )
            set_tests_properties(absolute.emit-shader-plugin PROPERTIES
                FIXTURES_SETUP absolute-shader-plugin-ir
            )
            add_test(
                NAME absolute.check-shader-plugin-ir
                COMMAND "${CMAKE_COMMAND}"
                    -DIR_FILE=${CMAKE_BINARY_DIR}/shader-plugin.ll
                    -P "${PROJECT_SOURCE_DIR}/tests/check-shader-plugin-ir.cmake"
            )
            set_tests_properties(absolute.check-shader-plugin-ir PROPERTIES
                FIXTURES_REQUIRED absolute-shader-plugin-ir
            )
            add_test(
                NAME absolute.build-shader-plugin
                COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/shader-plugin.abs"
                    --plugin "$<TARGET_FILE:Absolute-Shader-Plugin>"
                    --build-exe -o "${CMAKE_BINARY_DIR}/shader-plugin${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.build-shader-plugin PROPERTIES
                FIXTURES_SETUP absolute-shader-plugin-executable
            )
            add_test(
                NAME absolute.run-shader-plugin
                COMMAND "${CMAKE_BINARY_DIR}/shader-plugin${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.run-shader-plugin PROPERTIES
                FIXTURES_REQUIRED absolute-shader-plugin-executable
                PASS_REGULAR_EXPRESSION "shader-plugin=ok"
            )
        endif()
        add_test(
            NAME absolute.build-overloads-extensions
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/overloads-extensions.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/overloads-extensions${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-overloads-extensions PROPERTIES
            FIXTURES_SETUP absolute-overloads-extensions-executable
        )
        add_test(
            NAME absolute.run-overloads-extensions
            COMMAND "${CMAKE_BINARY_DIR}/overloads-extensions${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-overloads-extensions PROPERTIES
            FIXTURES_REQUIRED absolute-overloads-extensions-executable
            PASS_REGULAR_EXPRESSION "overloads-extensions=ok"
        )
        add_test(
            NAME absolute.build-static-members
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/static-members.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/static-members${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-static-members PROPERTIES
            FIXTURES_SETUP absolute-static-members-executable
        )
        add_test(
            NAME absolute.run-static-members
            COMMAND "${CMAKE_BINARY_DIR}/static-members${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-static-members PROPERTIES
            FIXTURES_REQUIRED absolute-static-members-executable
            PASS_REGULAR_EXPRESSION "static-members=ok"
        )
        add_test(
            NAME absolute.build-base-constructors
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/base-constructors.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/base-constructors${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-base-constructors PROPERTIES
            FIXTURES_SETUP absolute-base-constructors-executable
        )
        add_test(
            NAME absolute.run-base-constructors
            COMMAND "${CMAKE_BINARY_DIR}/base-constructors${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-base-constructors PROPERTIES
            FIXTURES_REQUIRED absolute-base-constructors-executable
            PASS_REGULAR_EXPRESSION "base-constructors=ok"
        )
        add_test(
            NAME absolute.build-access-control
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/access-control.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/access-control${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-access-control PROPERTIES
            FIXTURES_SETUP absolute-access-control-executable
        )
        add_test(
            NAME absolute.run-access-control
            COMMAND "${CMAKE_BINARY_DIR}/access-control${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-access-control PROPERTIES
            FIXTURES_REQUIRED absolute-access-control-executable
            PASS_REGULAR_EXPRESSION "access-control=ok"
        )
        add_test(
            NAME absolute.build-runtime-type-operations
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/runtime-type-operations.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/runtime-type-operations${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-runtime-type-operations PROPERTIES
            FIXTURES_SETUP absolute-runtime-type-operations-executable
        )
        add_test(
            NAME absolute.run-runtime-type-operations
            COMMAND "${CMAKE_BINARY_DIR}/runtime-type-operations${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-runtime-type-operations PROPERTIES
            FIXTURES_REQUIRED absolute-runtime-type-operations-executable
            PASS_REGULAR_EXPRESSION "runtime-types=ok"
        )
        add_test(
            NAME absolute.emit-llvm
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/llvm-basic.abs" --emit-llvm
        )
        set_tests_properties(absolute.emit-llvm PROPERTIES
            PASS_REGULAR_EXPRESSION "define i32 @main"
        )
        add_test(
            NAME absolute.emit-attributes
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/attributes.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/attributes.ll"
        )
        set_tests_properties(absolute.emit-attributes PROPERTIES
            FIXTURES_SETUP absolute-attributes-ir
        )
        add_test(
            NAME absolute.check-attributes-ir
            COMMAND "${CMAKE_COMMAND}"
                -DIR_FILE=${CMAKE_BINARY_DIR}/attributes.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-attributes-ir.cmake"
        )
        set_tests_properties(absolute.check-attributes-ir PROPERTIES
            FIXTURES_REQUIRED absolute-attributes-ir
        )
        add_test(
            NAME absolute.emit-generic-functions
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generics-functions.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/generic-functions.ll"
        )
        set_tests_properties(absolute.emit-generic-functions PROPERTIES
            FIXTURES_SETUP absolute-generic-functions-ir
        )
        add_test(
            NAME absolute.emit-generic-types
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generics-types.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/generic-types.ll"
        )
        set_tests_properties(absolute.emit-generic-types PROPERTIES
            FIXTURES_SETUP absolute-generic-types-ir
        )
        add_test(
            NAME absolute.check-generics-ir
            COMMAND "${CMAKE_COMMAND}"
                -DFUNCTIONS_IR=${CMAKE_BINARY_DIR}/generic-functions.ll
                -DTYPES_IR=${CMAKE_BINARY_DIR}/generic-types.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-generics-ir.cmake"
        )
        set_tests_properties(absolute.check-generics-ir PROPERTIES
            FIXTURES_REQUIRED "absolute-generic-functions-ir;absolute-generic-types-ir"
        )
        add_test(
            NAME absolute.emit-base-language
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/base-language.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/base-language.ll"
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
            NAME absolute.emit-array-copy
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-copy.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/array-copy.ll"
        )
        set_tests_properties(absolute.emit-array-copy PROPERTIES
            FIXTURES_SETUP absolute-array-copy-ir
        )
        add_test(
            NAME absolute.emit-insertion-sort-optimization
            COMMAND Absolute-Compiler
                "${PROJECT_SOURCE_DIR}/benchmarks/array-suite/absolute/insertion-sort.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/insertion-sort-optimization.ll"
        )
        set_tests_properties(absolute.emit-insertion-sort-optimization PROPERTIES
            FIXTURES_SETUP absolute-insertion-sort-optimization-ir
        )
        add_test(
            NAME absolute.check-insertion-sort-optimization-ir
            COMMAND "${CMAKE_COMMAND}"
                -DIR_FILE=${CMAKE_BINARY_DIR}/insertion-sort-optimization.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-insertion-sort-optimization-ir.cmake"
        )
        set_tests_properties(absolute.check-insertion-sort-optimization-ir PROPERTIES
            FIXTURES_REQUIRED absolute-insertion-sort-optimization-ir
        )
        add_test(
            NAME absolute.emit-loop-range-facts
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/loop-range-facts.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/loop-range-facts.ll"
        )
        set_tests_properties(absolute.emit-loop-range-facts PROPERTIES
            FIXTURES_SETUP absolute-loop-range-facts-ir
        )
        add_test(
            NAME absolute.check-loop-range-facts-ir
            COMMAND "${CMAKE_COMMAND}"
                -DIR_FILE=${CMAKE_BINARY_DIR}/loop-range-facts.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-loop-range-facts-ir.cmake"
        )
        set_tests_properties(absolute.check-loop-range-facts-ir PROPERTIES
            FIXTURES_REQUIRED absolute-loop-range-facts-ir
        )
        add_test(
            NAME absolute.emit-chess-rules
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/chess-rules.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/chess-rules.ll"
        )
        set_tests_properties(absolute.emit-chess-rules PROPERTIES
            FIXTURES_SETUP absolute-chess-rules-ir
        )
        add_test(
            NAME absolute.emit-chess-example
            COMMAND Absolute-Compiler build
                "${PROJECT_SOURCE_DIR}/examples/chess/Chess.absproj"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/chess-example.ll"
        )
        set_tests_properties(absolute.emit-chess-example PROPERTIES
            FIXTURES_SETUP absolute-chess-example-ir
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
            NAME absolute.emit-interfaces-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interfaces-codegen.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/interfaces-codegen.ll"
        )
        set_tests_properties(absolute.emit-interfaces-codegen PROPERTIES
            FIXTURES_SETUP absolute-interfaces-codegen-ir
        )
        add_test(
            NAME absolute.emit-structs-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/structs-codegen.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/structs-codegen.ll"
        )
        set_tests_properties(absolute.emit-structs-codegen PROPERTIES
            FIXTURES_SETUP absolute-structs-codegen-ir
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

        # Windows UCRT exposes printf/snprintf as inline wrappers rather than
        # JIT-resolvable symbols. Native Windows runtime coverage therefore
        # uses the --build-exe tests below instead of lli.
        if(NOT WIN32)
            find_program(ABSOLUTE_LLI_EXECUTABLE NAMES lli lli-18
                HINTS "${LLVM_TOOLS_BINARY_DIR}" "${LLVM_DIR}/../../../bin")
        else()
            unset(ABSOLUTE_LLI_EXECUTABLE CACHE)
            set(ABSOLUTE_LLI_EXECUTABLE "")
        endif()
        if(ABSOLUTE_LLI_EXECUTABLE)
            if(TARGET Absolute-Unless-Plugin)
                add_test(
                    NAME absolute.run-syntax-plugin
                    COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/syntax-plugin.ll"
                )
                set_tests_properties(absolute.run-syntax-plugin PROPERTIES
                    FIXTURES_REQUIRED absolute-syntax-plugin-ir
                )
            endif()
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
                NAME absolute.run-array-copy
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/array-copy.ll"
            )
            set_tests_properties(absolute.run-array-copy PROPERTIES
                FIXTURES_REQUIRED absolute-array-copy-ir
                PASS_REGULAR_EXPRESSION "array-copy=ok"
            )
            add_test(
                NAME absolute.run-chess-rules
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/chess-rules.ll"
            )
            set_tests_properties(absolute.run-chess-rules PROPERTIES
                FIXTURES_REQUIRED absolute-chess-rules-ir
                PASS_REGULAR_EXPRESSION "pawn-pseudo=true pawn-legal=true knight-legal=true"
            )
            add_test(
                NAME absolute.run-chess-checkmate
                COMMAND "${CMAKE_COMMAND}"
                    -DLLI=${ABSOLUTE_LLI_EXECUTABLE}
                    -DIR_FILE=${CMAKE_BINARY_DIR}/chess-example.ll
                    -DINPUT_FILE=${PROJECT_SOURCE_DIR}/tests/chess-mate-input.txt
                    -P "${PROJECT_SOURCE_DIR}/tests/run-chess-checkmate.cmake"
            )
            set_tests_properties(absolute.run-chess-checkmate PROPERTIES
                FIXTURES_REQUIRED absolute-chess-example-ir
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
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/NativeFfi-$<CONFIG>.absproj" CONTENT
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
            COMMAND Absolute-Compiler build "${CMAKE_BINARY_DIR}/NativeFfi-$<CONFIG>.absproj"
                --emit-object -o "${CMAKE_BINARY_DIR}/native-ffi-object.o"
        )
        add_test(
            NAME absolute.build-native
            COMMAND Absolute-Compiler build "${CMAKE_BINARY_DIR}/NativeFfi-$<CONFIG>.absproj"
                --build-exe -o "${CMAKE_BINARY_DIR}/native-ffi-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-native PROPERTIES
            FIXTURES_SETUP absolute-native-executable
        )
        add_test(NAME absolute.run-native COMMAND "${CMAKE_BINARY_DIR}/native-ffi-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-native PROPERTIES
            FIXTURES_REQUIRED absolute-native-executable
            PASS_REGULAR_EXPRESSION "native=42"
        )
        add_test(
            NAME absolute.build-pointers
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/pointers.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/pointers-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-pointers PROPERTIES
            FIXTURES_SETUP absolute-pointers-executable
        )
        add_test(NAME absolute.run-pointers COMMAND "${CMAKE_BINARY_DIR}/pointers-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-pointers PROPERTIES
            FIXTURES_REQUIRED absolute-pointers-executable
            PASS_REGULAR_EXPRESSION "pointers=ok"
        )
        add_test(
            NAME absolute.build-pointer-functions
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/pointer-functions.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/pointer-functions-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-pointer-functions PROPERTIES
            FIXTURES_SETUP absolute-pointer-functions-executable
        )
        add_test(NAME absolute.run-pointer-functions COMMAND "${CMAKE_BINARY_DIR}/pointer-functions-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-pointer-functions PROPERTIES
            FIXTURES_REQUIRED absolute-pointer-functions-executable
            PASS_REGULAR_EXPRESSION "pointer-functions=ok"
        )
        add_test(
            NAME absolute.build-classes-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/classes-codegen.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/classes-codegen-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-classes-codegen PROPERTIES
            FIXTURES_SETUP absolute-classes-codegen-executable
        )
        add_test(NAME absolute.run-classes-codegen COMMAND "${CMAKE_BINARY_DIR}/classes-codegen-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-classes-codegen PROPERTIES
            FIXTURES_REQUIRED absolute-classes-codegen-executable
            PASS_REGULAR_EXPRESSION "classes=42"
        )
        add_test(
            NAME absolute.build-interfaces-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interfaces-codegen.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/interfaces-codegen-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-interfaces-codegen PROPERTIES
            FIXTURES_SETUP absolute-interfaces-codegen-executable
        )
        add_test(NAME absolute.run-interfaces-codegen COMMAND "${CMAKE_BINARY_DIR}/interfaces-codegen-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-interfaces-codegen PROPERTIES
            FIXTURES_REQUIRED absolute-interfaces-codegen-executable
            PASS_REGULAR_EXPRESSION "interfaces=ok"
        )
        add_test(
            NAME absolute.build-structs-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/structs-codegen.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/structs-codegen-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-structs-codegen PROPERTIES
            FIXTURES_SETUP absolute-structs-codegen-executable
        )
        add_test(NAME absolute.run-structs-codegen COMMAND "${CMAKE_BINARY_DIR}/structs-codegen-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-structs-codegen PROPERTIES
            FIXTURES_REQUIRED absolute-structs-codegen-executable
            PASS_REGULAR_EXPRESSION "structs=ok"
        )
        add_test(
            NAME absolute.build-base-language
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/base-language.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/base-language-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-base-language PROPERTIES
            FIXTURES_SETUP absolute-base-language-executable
        )
        add_test(NAME absolute.run-base-language
            COMMAND "${CMAKE_BINARY_DIR}/base-language-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-base-language PROPERTIES
            FIXTURES_REQUIRED absolute-base-language-executable
            PASS_REGULAR_EXPRESSION "base-language=ok"
        )
        add_test(
            NAME absolute.build-managed-fast-path
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-fast-path.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/managed-fast-path-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-managed-fast-path PROPERTIES
            FIXTURES_SETUP absolute-managed-fast-path-executable
        )
        add_test(NAME absolute.run-managed-fast-path COMMAND "${CMAKE_BINARY_DIR}/managed-fast-path-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-managed-fast-path PROPERTIES
            FIXTURES_REQUIRED absolute-managed-fast-path-executable
            PASS_REGULAR_EXPRESSION "managed-fast-path="
        )
        add_test(
            NAME absolute.build-raw-lifetime
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/raw-lifetime.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/raw-lifetime-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-raw-lifetime PROPERTIES
            FIXTURES_SETUP absolute-raw-lifetime-executable
        )
        add_test(NAME absolute.run-raw-lifetime COMMAND "${CMAKE_BINARY_DIR}/raw-lifetime-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-raw-lifetime PROPERTIES
            FIXTURES_REQUIRED absolute-raw-lifetime-executable
            PASS_REGULAR_EXPRESSION "raw-lifetime=ok"
        )
        add_test(
            NAME absolute.build-flow-analysis
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/flow-analysis.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/flow-analysis-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-flow-analysis PROPERTIES
            FIXTURES_SETUP absolute-flow-analysis-executable
        )
        add_test(NAME absolute.run-flow-analysis COMMAND "${CMAKE_BINARY_DIR}/flow-analysis-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-flow-analysis PROPERTIES
            FIXTURES_REQUIRED absolute-flow-analysis-executable
            PASS_REGULAR_EXPRESSION "flow-analysis=ok"
        )
        add_test(
            NAME absolute.build-switch-match
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/switch-match.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/switch-match-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-switch-match PROPERTIES
            FIXTURES_SETUP absolute-switch-match-executable
        )
        add_test(NAME absolute.run-switch-match
            COMMAND "${CMAKE_BINARY_DIR}/switch-match-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-switch-match PROPERTIES
            FIXTURES_REQUIRED absolute-switch-match-executable
            PASS_REGULAR_EXPRESSION "switch-match=ok"
        )
        add_test(
            NAME absolute.build-exceptions
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/exceptions.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/exceptions-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-exceptions PROPERTIES
            FIXTURES_SETUP absolute-exceptions-executable
        )
        add_test(NAME absolute.run-exceptions
            COMMAND "${CMAKE_BINARY_DIR}/exceptions-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-exceptions PROPERTIES
            FIXTURES_REQUIRED absolute-exceptions-executable
            PASS_REGULAR_EXPRESSION "exceptions=ok"
        )
        add_test(
            NAME absolute.build-defer
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/defer.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/defer-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-defer PROPERTIES
            FIXTURES_SETUP absolute-defer-executable
        )
        add_test(NAME absolute.run-defer
            COMMAND "${CMAKE_BINARY_DIR}/defer-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-defer PROPERTIES
            FIXTURES_REQUIRED absolute-defer-executable
            PASS_REGULAR_EXPRESSION "defer=ok"
        )
        add_test(
            NAME absolute.build-attributes
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/attributes.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/attributes-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-attributes PROPERTIES
            FIXTURES_SETUP absolute-attributes-executable
        )
        add_test(NAME absolute.run-attributes
            COMMAND "${CMAKE_BINARY_DIR}/attributes-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-attributes PROPERTIES
            FIXTURES_REQUIRED absolute-attributes-executable
            PASS_REGULAR_EXPRESSION "attributes=ok"
        )
        add_test(
            NAME absolute.build-generic-functions
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generics-functions.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/generic-functions-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-generic-functions PROPERTIES
            FIXTURES_SETUP absolute-generic-functions-executable
        )
        add_test(NAME absolute.run-generic-functions
            COMMAND "${CMAKE_BINARY_DIR}/generic-functions-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-generic-functions PROPERTIES
            FIXTURES_REQUIRED absolute-generic-functions-executable
            PASS_REGULAR_EXPRESSION "generic-functions=ok"
        )
        add_test(
            NAME absolute.build-generic-types
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generics-types.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/generic-types-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-generic-types PROPERTIES
            FIXTURES_SETUP absolute-generic-types-executable
        )
        add_test(NAME absolute.run-generic-types
            COMMAND "${CMAKE_BINARY_DIR}/generic-types-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-generic-types PROPERTIES
            FIXTURES_REQUIRED absolute-generic-types-executable
            PASS_REGULAR_EXPRESSION "generic-types=ok"
        )
        add_test(
            NAME absolute.build-unhandled-exception
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/unhandled-exception.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/unhandled-exception-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-unhandled-exception PROPERTIES
            FIXTURES_SETUP absolute-unhandled-exception-executable
        )
        add_test(NAME absolute.run-unhandled-exception
            COMMAND "${CMAKE_BINARY_DIR}/unhandled-exception-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-unhandled-exception PROPERTIES
            FIXTURES_REQUIRED absolute-unhandled-exception-executable
            WILL_FAIL TRUE
        )
        add_test(
            NAME absolute.build-async
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-basic.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/async-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-async PROPERTIES
            FIXTURES_SETUP absolute-async-executable
        )
        add_test(NAME absolute.run-async COMMAND "${CMAKE_BINARY_DIR}/async-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-async PROPERTIES
            FIXTURES_REQUIRED absolute-async-executable
            PASS_REGULAR_EXPRESSION "async=42"
        )
    endif()
endif()
