cmake_minimum_required(VERSION 3.20)
project(Absolute VERSION 0.5.0 LANGUAGES CXX)

set(ABSOLUTE_VERSION "${PROJECT_VERSION}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ABSOLUTE_ENABLE_LLVM "Build the LLVM IR backend" ON)
option(ABSOLUTE_BUILD_EXAMPLE_PLUGINS "Build example syntax plugins" ON)
option(ABSOLUTE_USE_COMPILER_CACHE "Use sccache or ccache when available" OFF)
option(ABSOLUTE_TERMUX "Enable Android/Termux host compatibility" OFF)
set(ABSOLUTE_LLVM_ROOT "" CACHE PATH "Root of a native LLVM SDK containing lib/cmake/llvm/LLVMConfig.cmake")

if(DEFINED ENV{TERMUX_VERSION} OR "$ENV{PREFIX}" MATCHES "^/data/data/com\.termux/")
    set(ABSOLUTE_TERMUX ON CACHE BOOL "Enable Android/Termux host compatibility" FORCE)
endif()
if(ABSOLUTE_TERMUX)
    message(STATUS "Absolute Termux host compatibility enabled")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(AbsoluteLLVM)
include(AbsoluteWasi)

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
    find_package(Python3 COMPONENTS Interpreter QUIET)

    add_test(
        NAME absolute.compiler-version
        COMMAND Absolute-Compiler --version)
    set_tests_properties(absolute.compiler-version PROPERTIES
        PASS_REGULAR_EXPRESSION "absolutec ${ABSOLUTE_VERSION}")
    add_test(
        NAME absolute.optimization-level-errors
        COMMAND Absolute-Compiler
            "${PROJECT_SOURCE_DIR}/tests/arrays.abs" -O4)
    set_tests_properties(absolute.optimization-level-errors PROPERTIES
        WILL_FAIL TRUE)

    add_executable(absolute-runtime-scheduler-v2-test
        tests/runtime-scheduler-v2.cpp)
    target_include_directories(absolute-runtime-scheduler-v2-test
        PRIVATE "${PROJECT_SOURCE_DIR}/Absolute-Runtime/src")
    target_link_libraries(absolute-runtime-scheduler-v2-test
        PRIVATE Absolute-Runtime)
    add_test(
        NAME absolute.runtime-scheduler-v2
        COMMAND absolute-runtime-scheduler-v2-test)
    set_tests_properties(absolute.runtime-scheduler-v2 PROPERTIES
        PASS_REGULAR_EXPRESSION "runtime-scheduler-v2=ok"
        TIMEOUT 30)

    add_executable(absolute-runtime-scheduler-work-stealing-test
        tests/runtime-scheduler-work-stealing.cpp)
    target_link_libraries(absolute-runtime-scheduler-work-stealing-test
        PRIVATE Absolute-Runtime)
    add_test(
        NAME absolute.runtime-scheduler-work-stealing
        COMMAND absolute-runtime-scheduler-work-stealing-test)
    set_tests_properties(absolute.runtime-scheduler-work-stealing PROPERTIES
        PASS_REGULAR_EXPRESSION "runtime-scheduler-work-stealing=ok"
        TIMEOUT 30)

    add_executable(absolute-runtime-scheduler-fairness-test
        tests/runtime-scheduler-fairness.cpp)
    target_link_libraries(absolute-runtime-scheduler-fairness-test
        PRIVATE Absolute-Runtime)
    add_test(
        NAME absolute.runtime-scheduler-fairness
        COMMAND absolute-runtime-scheduler-fairness-test)
    set_tests_properties(absolute.runtime-scheduler-fairness PROPERTIES
        PASS_REGULAR_EXPRESSION "runtime-scheduler-fairness=ok"
        TIMEOUT 30)

    add_executable(absolute-runtime-scheduler-metrics-test
        tests/runtime-scheduler-metrics.cpp)
    target_link_libraries(absolute-runtime-scheduler-metrics-test
        PRIVATE Absolute-Runtime)
    add_test(
        NAME absolute.runtime-scheduler-metrics
        COMMAND absolute-runtime-scheduler-metrics-test)
    set_tests_properties(absolute.runtime-scheduler-metrics PROPERTIES
        PASS_REGULAR_EXPRESSION "runtime-scheduler-metrics=ok"
        TIMEOUT 30)

    add_executable(absolute-runtime-scheduler-stress-test
        tests/runtime-scheduler-stress.cpp)
    target_link_libraries(absolute-runtime-scheduler-stress-test
        PRIVATE Absolute-Runtime)
    add_test(
        NAME absolute.runtime-scheduler-stress
        COMMAND absolute-runtime-scheduler-stress-test)
    set_tests_properties(absolute.runtime-scheduler-stress PROPERTIES
        PASS_REGULAR_EXPRESSION "runtime-scheduler-stress=ok"
        TIMEOUT 60)

    if(TARGET Absolute-CodeGen AND Python3_Interpreter_FOUND)
        add_test(
            NAME absolute.optimization-differential
            COMMAND "${Python3_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/testing/optimization_differential.py"
                --compiler "$<TARGET_FILE:Absolute-Compiler>"
                --work-dir
                    "${CMAKE_BINARY_DIR}/differential/optimization/$<CONFIG>"
                --configuration "$<CONFIG>")
        set_tests_properties(absolute.optimization-differential PROPERTIES
            PASS_REGULAR_EXPRESSION "optimization-differential=ok"
            TIMEOUT 180)
    endif()

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
            NAME absolute.plugin-resource-type
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/plugin-resource-type.abs"
                --plugin "$<TARGET_FILE:Absolute-Unless-Plugin>"
                --build-exe -o "${CMAKE_BINARY_DIR}/plugin-resource-type${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.plugin-resource-type PROPERTIES
            FIXTURES_SETUP absolute-plugin-resource-executable
        )
        add_test(
            NAME absolute.run-plugin-resource-type
            COMMAND "${CMAKE_BINARY_DIR}/plugin-resource-type${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-plugin-resource-type PROPERTIES
            FIXTURES_REQUIRED absolute-plugin-resource-executable
            PASS_REGULAR_EXPRESSION "test_resource_destroy called for 42.*test_resource_destroy called for 100"
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
        set(ABSOLUTE_MATH_MANIFEST
            "$<TARGET_FILE_DIR:Absolute-Math-Plugin>/absolute-math.absplugin")
        file(WRITE "${CMAKE_BINARY_DIR}/unsupported-permissions.absplugin" [=[{
  "name": "absolute.test-permissions",
  "version": "1.0.0",
  "abi": 1,
  "library": "]=] "$<TARGET_FILE:Absolute-Math-Plugin>" [=[",
  "permissions": ["filesystem.unknown"],
  "provides": ["test.permissions"]
}
]=])
        add_test(
            NAME absolute.math-plugin
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/math-plugin.abs"
                --plugin "${ABSOLUTE_MATH_MANIFEST}"
        )
        add_test(
            NAME absolute.plugin-permissions
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/comments.abs"
                --plugin "${CMAKE_BINARY_DIR}/unsupported-permissions.absplugin"
        )
        set_tests_properties(absolute.plugin-permissions PROPERTIES WILL_FAIL TRUE)
        file(WRITE "${CMAKE_BINARY_DIR}/missing-native-library-permission.absplugin" [=[{
  "name": "absolute.test-native-permission",
  "version": "1.0.0",
  "abi": 1,
  "library": "]=] "$<TARGET_FILE:Absolute-Math-Plugin>" [=[",
  "nativeLibraries": ["native.dll"],
  "provides": ["test.permissions"]
}
]=])
        add_test(
            NAME absolute.plugin-native-library-permission
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/comments.abs"
                --plugin "${CMAKE_BINARY_DIR}/missing-native-library-permission.absplugin"
        )
        set_tests_properties(absolute.plugin-native-library-permission PROPERTIES WILL_FAIL TRUE)
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
        set(ABSOLUTE_SHADER_MANIFEST
            "$<TARGET_FILE_DIR:Absolute-Shader-Plugin>/absolute-shader.absplugin")
        add_test(
            NAME absolute.shader-plugin
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/shader-plugin.abs"
                --plugin "${ABSOLUTE_SHADER_MANIFEST}"
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
                --plugin "${ABSOLUTE_SHADER_MANIFEST}"
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
                NAME absolute.desktop-plugin-raii-ir
                COMMAND "${CMAKE_COMMAND}"
                    -DCOMPILER=$<TARGET_FILE:Absolute-Compiler>
                    -DPLUGIN=${ABSOLUTE_DESKTOP_MANIFEST}
                    -DSOURCE=${PROJECT_SOURCE_DIR}/tests/desktop-plugin.abs
                    -DOUTPUT=${CMAKE_BINARY_DIR}/tests/desktop-plugin-raii.ll
                    -P "${PROJECT_SOURCE_DIR}/tests/check-desktop-plugin-raii-ir.cmake"
            )
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
        NAME absolute.move-semantics
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/move-semantics.abs"
    )
    add_test(
        NAME absolute.move-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/move-errors.abs"
    )
    set_tests_properties(absolute.move-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.pointer-functions-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/pointer-functions.abs"
    )
    add_test(
        NAME absolute.managed-fast-path-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-fast-path.abs"
    )
    add_test(
        NAME absolute.managed-deref-after-delete
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-deref-after-delete.abs"
    )
    set_tests_properties(absolute.managed-deref-after-delete PROPERTIES WILL_FAIL TRUE)
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
        NAME absolute.raw-lifetime-semantic-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/raw-lifetime-semantic-errors.abs"
    )
    set_tests_properties(absolute.raw-lifetime-semantic-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.leak-detector
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/leak-detector.abs"
    )
    set_tests_properties(absolute.leak-detector PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.sanitizer-uaf
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/sanitizer-uaf.abs" --sanitize=address
    )
    set_tests_properties(absolute.sanitizer-uaf PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.sanitizer-leak
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/sanitizer-leak.abs" --sanitize=address
    )
    set_tests_properties(absolute.sanitizer-leak PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.sanitizer-double-free
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/sanitizer-double-free.abs" --sanitize=address
    )
    set_tests_properties(absolute.sanitizer-double-free PROPERTIES WILL_FAIL TRUE)
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
        NAME absolute.generic-callback-abi-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generic-callback-abi.abs"
    )
    add_test(
        NAME absolute.std-time-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-time.abs"
    )
    add_test(
        NAME absolute.std-core-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-core.abs"
    )
    add_test(
        NAME absolute.std-random-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-random.abs"
    )
    add_test(
        NAME absolute.std-fs-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-fs.abs"
    )
    add_test(
        NAME absolute.std-net-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-net.abs"
    )
    add_test(
        NAME absolute.std-env-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-env.abs"
    )
    add_test(
        NAME absolute.std-process-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-process.abs"
    )
    add_test(
        NAME absolute.std-datetime-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-datetime.abs"
    )
    add_test(
        NAME absolute.std-string-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-string.abs"
    )
    add_test(
        NAME absolute.string-interpolation-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/string-interpolation.abs"
    )
    add_test(
        NAME absolute.std-json-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-json.abs"
    )
    add_test(
        NAME absolute.std-binary-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-binary.abs"
    )
    add_test(
        NAME absolute.std-net-udp-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-net-udp.abs"
    )
    add_test(
        NAME absolute.std-uri-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-uri.abs"
    )
    add_test(
        NAME absolute.std-http-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-http.abs"
    )
    add_test(
        NAME absolute.std-log-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-log.abs"
    )
    add_test(
        NAME absolute.std-assert-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-assert.abs"
    )
    add_test(
        NAME absolute.std-testing-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-testing.abs"
    )
    add_test(
        NAME absolute.script-main-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/script-main.abs"
    )
    add_test(
        NAME absolute.script-main-conflict
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/script-main-conflict.abs"
    )
    set_tests_properties(absolute.script-main-conflict PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.iterators
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/iterators.abs"
    )
    add_test(
        NAME absolute.collection-snapshot-iteration-semantic
        COMMAND Absolute-Compiler
            "${PROJECT_SOURCE_DIR}/tests/collection-snapshot-iteration.abs"
    )
    add_test(
        NAME absolute.std-deque-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-deque.abs"
    )
    add_test(
        NAME absolute.std-priority-queue-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-priority-queue.abs"
    )
    add_test(
        NAME absolute.std-hash-collections-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-hash-collections.abs"
    )
    add_test(
        NAME absolute.collection-builder-capability-semantic
        COMMAND Absolute-Compiler
            "${PROJECT_SOURCE_DIR}/tests/collection-builder-capability.abs"
    )
    add_test(
        NAME absolute.collection-algorithms-semantic
        COMMAND Absolute-Compiler
            "${PROJECT_SOURCE_DIR}/tests/collection-algorithms.abs"
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
        NAME absolute.implicit-conversions-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/implicit-conversions.abs"
    )
    set_tests_properties(absolute.implicit-conversions-semantic PROPERTIES
        PASS_REGULAR_EXPRESSION "implicit-conversions=ok"
    )
    add_test(
        NAME absolute.implicit-conversion-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/implicit-conversion-errors.abs"
    )
    set_tests_properties(absolute.implicit-conversion-errors PROPERTIES WILL_FAIL TRUE)
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
        NAME absolute.async-lifetime-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-lifetime-errors.abs"
    )
    set_tests_properties(absolute.async-lifetime-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.task-scheduling-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/task-scheduling.abs"
    )
    add_test(
        NAME absolute.task-group-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/task-group.abs"
    )
    add_test(
        NAME absolute.task-group-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/task-group-errors.abs"
    )
    set_tests_properties(absolute.task-group-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.task-scheduling-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/task-scheduling-errors.abs"
    )
    set_tests_properties(absolute.task-scheduling-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.async-methods-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-methods.abs"
    )
    add_test(
        NAME absolute.async-method-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-method-errors.abs"
    )
    set_tests_properties(absolute.async-method-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.async-method-override-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-method-override-errors.abs"
    )
    set_tests_properties(absolute.async-method-override-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.value-ref-by-value-semantic
        COMMAND Absolute-Compiler
            "${PROJECT_SOURCE_DIR}/benchmarks/value-ref-suite/absolute/by-value.abs"
    )
    add_test(
        NAME absolute.value-ref-raw-borrow-semantic
        COMMAND Absolute-Compiler
            "${PROJECT_SOURCE_DIR}/benchmarks/value-ref-suite/absolute/raw-borrow.abs"
    )
    add_test(
        NAME absolute.value-references-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/value-references.abs"
    )
    add_test(
        NAME absolute.value-reference-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/value-reference-errors.abs"
    )
    set_tests_properties(absolute.value-reference-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.weak-managed-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/weak-managed-references.abs"
    )
    add_test(
        NAME absolute.weak-managed-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/weak-managed-reference-errors.abs"
    )
    set_tests_properties(absolute.weak-managed-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.managed-object-graphs-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-object-graphs.abs"
    )
    add_test(
        NAME absolute.managed-object-graph-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-object-graph-errors.abs"
    )
    set_tests_properties(absolute.managed-object-graph-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.managed-pointer-move-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-pointer-move.abs"
    )
    add_test(
        NAME absolute.managed-pointer-move-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-pointer-move-errors.abs"
    )
    set_tests_properties(absolute.managed-pointer-move-errors PROPERTIES WILL_FAIL TRUE)
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
        NAME absolute.multidimensional-slices-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/multidimensional-slices.abs"
    )
    add_test(
        NAME absolute.simd-vectorization-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/simd-vectorization.abs"
    )
    add_test(
        NAME absolute.async-concurrency-advanced-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-concurrency-advanced.abs"
    )
    add_test(
        NAME absolute.std-typed-channel-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-typed-channel.abs"
    )
    add_test(
        NAME absolute.ownership-transfer-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/ownership-transfer-errors.abs"
    )
    set_tests_properties(absolute.ownership-transfer-errors PROPERTIES WILL_FAIL TRUE)
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
        NAME absolute.resource-fields-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/resource-fields.abs"
    )
    add_test(
        NAME absolute.resource-field-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/resource-field-errors.abs"
    )
    set_tests_properties(absolute.resource-field-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.managed-temporary-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-temporary-errors.abs"
    )
    set_tests_properties(absolute.managed-temporary-errors PROPERTIES WILL_FAIL TRUE)
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
        NAME absolute.properties-codegen-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/properties-codegen.abs"
    )
    add_test(
        NAME absolute.property-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/property-errors.abs"
    )
    set_tests_properties(absolute.property-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.property-syntax-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/property-syntax-errors.abs"
    )
    set_tests_properties(absolute.property-syntax-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.indexers-codegen-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/indexers-codegen.abs"
    )
    add_test(
        NAME absolute.indexer-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/indexer-errors.abs"
    )
    set_tests_properties(absolute.indexer-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.indexer-syntax-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/indexer-syntax-errors.abs"
    )
    set_tests_properties(absolute.indexer-syntax-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.interface-static-codegen-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interface-static-codegen.abs"
    )
    add_test(
        NAME absolute.interface-static-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interface-static-errors.abs"
    )
    set_tests_properties(absolute.interface-static-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.interface-field-syntax-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interface-field-syntax-errors.abs"
    )
    set_tests_properties(absolute.interface-field-syntax-errors PROPERTIES WILL_FAIL TRUE)
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
        NAME absolute.constructor-overloads-semantic
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/constructor-overloads.abs"
    )
    add_test(
        NAME absolute.constructor-overload-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/constructor-overload-errors.abs"
    )
    set_tests_properties(absolute.constructor-overload-errors PROPERTIES WILL_FAIL TRUE)
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
                    --plugin "${ABSOLUTE_MATH_MANIFEST}"
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
        if(TARGET Absolute-Math-Plugin AND TARGET Absolute-Shader-Plugin AND
           TARGET Absolute-Desktop-Plugin)
            add_test(
                NAME absolute.build-math-plugin-integration
                COMMAND Absolute-Compiler
                    "${PROJECT_SOURCE_DIR}/tests/math-plugin-integration.abs"
                    --plugin "${ABSOLUTE_DESKTOP_MANIFEST}"
                    --plugin "${ABSOLUTE_SHADER_MANIFEST}"
                    --plugin "${ABSOLUTE_MATH_MANIFEST}"
                    --build-exe
                    -o "${CMAKE_BINARY_DIR}/absolute-math-integration${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.build-math-plugin-integration PROPERTIES
                FIXTURES_SETUP absolute-math-integration-executable
            )
            add_test(
                NAME absolute.run-math-plugin-integration
                COMMAND "${CMAKE_BINARY_DIR}/absolute-math-integration${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.run-math-plugin-integration PROPERTIES
                FIXTURES_REQUIRED absolute-math-integration-executable
                PASS_REGULAR_EXPRESSION "math-integration=ok"
            )
        endif()
        if(TARGET Absolute-Shader-Plugin)
            add_test(
                NAME absolute.emit-shader-plugin
                COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/shader-plugin.abs"
                    --plugin "${ABSOLUTE_SHADER_MANIFEST}"
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
                    --plugin "${ABSOLUTE_SHADER_MANIFEST}"
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
            NAME absolute.build-constructor-overloads
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/constructor-overloads.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/constructor-overloads${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-constructor-overloads PROPERTIES
            FIXTURES_SETUP absolute-constructor-overloads-executable
        )
        add_test(
            NAME absolute.run-constructor-overloads
            COMMAND "${CMAKE_BINARY_DIR}/constructor-overloads${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-constructor-overloads PROPERTIES
            FIXTURES_REQUIRED absolute-constructor-overloads-executable
            PASS_REGULAR_EXPRESSION "constructor-overloads=ok"
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
            NAME absolute.emit-multidimensional-slices
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/multidimensional-slices.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/multidimensional-slices.ll"
        )
        set_tests_properties(absolute.emit-multidimensional-slices PROPERTIES
            FIXTURES_SETUP absolute-multidimensional-slices-ir
        )
        add_test(
            NAME absolute.check-array-ownership
            COMMAND "${CMAKE_COMMAND}"
                -DIR_FILE=${CMAKE_BINARY_DIR}/array-copy.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-array-ownership.cmake"
        )
        set_tests_properties(absolute.check-array-ownership PROPERTIES
            FIXTURES_REQUIRED absolute-array-copy-ir
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
            NAME absolute.emit-resource-fields
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/resource-fields.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/resource-fields.ll"
        )
        set_tests_properties(absolute.emit-resource-fields PROPERTIES
            FIXTURES_SETUP absolute-resource-fields-ir
        )
        add_test(
            NAME absolute.check-resource-fields
            COMMAND "${CMAKE_COMMAND}"
                -DIR_FILE=${CMAKE_BINARY_DIR}/resource-fields.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-resource-fields.cmake"
        )
        set_tests_properties(absolute.check-resource-fields PROPERTIES
            FIXTURES_REQUIRED absolute-resource-fields-ir
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
            NAME absolute.emit-properties-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/properties-codegen.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/properties-codegen.ll"
        )
        set_tests_properties(absolute.emit-properties-codegen PROPERTIES
            FIXTURES_SETUP absolute-properties-codegen-ir
        )
        add_test(
            NAME absolute.emit-indexers-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/indexers-codegen.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/indexers-codegen.ll"
        )
        set_tests_properties(absolute.emit-indexers-codegen PROPERTIES
            FIXTURES_SETUP absolute-indexers-codegen-ir
        )
        add_test(
            NAME absolute.emit-interface-static-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interface-static-codegen.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/interface-static-codegen.ll"
        )
        set_tests_properties(absolute.emit-interface-static-codegen PROPERTIES
            FIXTURES_SETUP absolute-interface-static-codegen-ir
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
            NAME absolute.check-struct-value-abi
            COMMAND "${CMAKE_COMMAND}"
                -DINPUT=${CMAKE_BINARY_DIR}/structs-codegen.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-struct-value-abi.cmake"
        )
        set_tests_properties(absolute.check-struct-value-abi PROPERTIES
            FIXTURES_REQUIRED absolute-structs-codegen-ir
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
        if(NOT WIN32 AND NOT ABSOLUTE_TERMUX)
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
                PASS_REGULAR_EXPRESSION "advanced-arrays=248"
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
                NAME absolute.run-multidimensional-slices
                COMMAND "${ABSOLUTE_LLI_EXECUTABLE}" "${CMAKE_BINARY_DIR}/multidimensional-slices.ll"
            )
            set_tests_properties(absolute.run-multidimensional-slices PROPERTIES
                FIXTURES_REQUIRED absolute-multidimensional-slices-ir
                PASS_REGULAR_EXPRESSION "multidimensional-slices=ok"
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
        elseif(ABSOLUTE_TERMUX)
            # LLVM's MCJIT/lli is not a supported Android execution path and can
            # crash inside Termux even when the same IR links and runs natively.
            # Keep IR emission/check coverage, but execute representative array
            # programs through the normal Android ELF linker path.
            add_test(
                NAME absolute.build-arrays-termux
                COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/arrays.abs"
                    --build-exe -o "${CMAKE_BINARY_DIR}/arrays-termux${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.build-arrays-termux PROPERTIES
                FIXTURES_SETUP absolute-arrays-termux-executable
            )
            add_test(
                NAME absolute.run-arrays
                COMMAND "${CMAKE_BINARY_DIR}/arrays-termux${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.run-arrays PROPERTIES
                FIXTURES_REQUIRED absolute-arrays-termux-executable
                PASS_REGULAR_EXPRESSION "arrays=90"
            )
            add_test(
                NAME absolute.build-array-advanced-termux
                COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/array-advanced.abs"
                    --build-exe -o "${CMAKE_BINARY_DIR}/array-advanced-termux${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.build-array-advanced-termux PROPERTIES
                FIXTURES_SETUP absolute-array-advanced-termux-executable
            )
            add_test(
                NAME absolute.run-array-advanced
                COMMAND "${CMAKE_BINARY_DIR}/array-advanced-termux${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.run-array-advanced PROPERTIES
                FIXTURES_REQUIRED absolute-array-advanced-termux-executable
                PASS_REGULAR_EXPRESSION "advanced-arrays=248"
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
        elseif(ABSOLUTE_TERMUX)
            add_test(
                NAME absolute.build-project-termux
                COMMAND Absolute-Compiler build
                    "${PROJECT_SOURCE_DIR}/tests/projects/multi-file/MultiFile.absproj"
                    --build-exe -o "${CMAKE_BINARY_DIR}/multi-file-project-termux${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.build-project-termux PROPERTIES
                FIXTURES_SETUP absolute-project-termux-executable
            )
            add_test(
                NAME absolute.run-project
                COMMAND "${CMAKE_BINARY_DIR}/multi-file-project-termux${CMAKE_EXECUTABLE_SUFFIX}"
            )
            set_tests_properties(absolute.run-project PROPERTIES
                FIXTURES_REQUIRED absolute-project-termux-executable
                PASS_REGULAR_EXPRESSION "project=42"
                TIMEOUT 60
            )
        endif()
    endif()
    add_test(
        NAME absolute.run-package-manifest-dependencies
        COMMAND "${CMAKE_COMMAND}"
            -DCOMPILER=$<TARGET_FILE:Absolute-Compiler>
            -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/package-manifest-deps
            -P "${PROJECT_SOURCE_DIR}/tests/run-package-manifest-dependencies.cmake"
    )

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
    add_test(
        NAME absolute.load-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/load-errors.abs"
    )
    set_tests_properties(absolute.load-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.invalid-export-abi
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/invalid-export-abi.abs" --parse-only
    )
    set_tests_properties(absolute.invalid-export-abi PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.export-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/export-errors.abs"
    )
    set_tests_properties(absolute.export-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.c-abi-type-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/c-abi-type-errors.abs"
    )
    set_tests_properties(absolute.c-abi-type-errors PROPERTIES WILL_FAIL TRUE)
    add_test(
        NAME absolute.cfunc-errors
        COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/cfunc-errors.abs"
    )
    set_tests_properties(absolute.cfunc-errors PROPERTIES WILL_FAIL TRUE)

    find_program(ABSOLUTE_NODE_EXECUTABLE NAMES node nodejs)
    if(ABSOLUTE_NODE_EXECUTABLE)
        add_test(
            NAME absolute.cli-version
            COMMAND "${CMAKE_COMMAND}" -E env
                "ABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>"
                "${ABSOLUTE_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/absolute-dev.js"
                --version
        )
        set_tests_properties(absolute.cli-version PROPERTIES
            PASS_REGULAR_EXPRESSION "absolutec ${ABSOLUTE_VERSION}")
    endif()
    set(ABSOLUTE_BINDGEN_CLANG "")
    if(EXISTS "${PROJECT_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin/clang${CMAKE_EXECUTABLE_SUFFIX}")
        set(ABSOLUTE_BINDGEN_CLANG
            "${PROJECT_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin/clang${CMAKE_EXECUTABLE_SUFFIX}")
    else()
        find_program(ABSOLUTE_BINDGEN_CLANG NAMES clang clang-18 clang-19)
    endif()
    if(ABSOLUTE_NODE_EXECUTABLE AND ABSOLUTE_BINDGEN_CLANG)
        add_test(
            NAME absolute.bindgen-sample
            COMMAND "${ABSOLUTE_NODE_EXECUTABLE}"
                "${PROJECT_SOURCE_DIR}/tools/absolute-bindgen.js"
                "${PROJECT_SOURCE_DIR}/tests/bindgen/sample.h"
                --clang "${ABSOLUTE_BINDGEN_CLANG}"
                -o "${CMAKE_BINARY_DIR}/bindgen-sample.abs"
        )
        set_tests_properties(absolute.bindgen-sample PROPERTIES
            FIXTURES_SETUP absolute-bindgen-sample
        )
        add_test(
            NAME absolute.check-bindgen-sample
            COMMAND "${CMAKE_COMMAND}"
                -DGENERATED=${CMAKE_BINARY_DIR}/bindgen-sample.abs
                -P "${PROJECT_SOURCE_DIR}/tests/bindgen/check-bindgen-sample.cmake"
        )
        set_tests_properties(absolute.check-bindgen-sample PROPERTIES
            FIXTURES_REQUIRED absolute-bindgen-sample
        )
        if(ABSOLUTE_ENABLE_LLVM)
            add_test(
                NAME absolute.parse-bindgen-sample
                COMMAND Absolute-Compiler "${CMAKE_BINARY_DIR}/bindgen-sample.abs" --parse-only
            )
            set_tests_properties(absolute.parse-bindgen-sample PROPERTIES
                FIXTURES_REQUIRED absolute-bindgen-sample
            )
        endif()
    endif()

    if(ABSOLUTE_ENABLE_LLVM)
        add_test(
            NAME absolute.emit-native-ffi
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/native-ffi.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/native-ffi.ll"
        )
        set_tests_properties(absolute.emit-native-ffi PROPERTIES
            FIXTURES_SETUP absolute-native-ffi-ir
        )
        add_test(
            NAME absolute.emit-export-c-abi
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/export-c-abi.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/export-c-abi.ll"
        )
        set_tests_properties(absolute.emit-export-c-abi PROPERTIES
            FIXTURES_SETUP absolute-export-c-abi-ir
        )
        add_test(
            NAME absolute.check-export-c-abi
            COMMAND "${CMAKE_COMMAND}"
                -DINPUT=${CMAKE_BINARY_DIR}/export-c-abi.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-export-c-abi.cmake"
        )
        add_test(
            NAME absolute.emit-wasm-smoke-ir
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/wasm-smoke.abs"
                --target wasm32-unknown-unknown
                --emit-llvm -o "${CMAKE_BINARY_DIR}/wasm-smoke.ll"
        )
        set_tests_properties(absolute.emit-wasm-smoke-ir PROPERTIES
            FIXTURES_SETUP absolute-wasm-smoke-ir
        )
        add_test(
            NAME absolute.check-wasm-smoke-ir
            COMMAND "${CMAKE_COMMAND}"
                -DINPUT=${CMAKE_BINARY_DIR}/wasm-smoke.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-wasm-target-ir.cmake"
        )
        set_tests_properties(absolute.check-wasm-smoke-ir PROPERTIES
            FIXTURES_REQUIRED absolute-wasm-smoke-ir
        )
        add_test(
            NAME absolute.emit-wasm-smoke-object
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/wasm-smoke.abs"
                --target wasm32-unknown-unknown
                --emit-object -o "${CMAKE_BINARY_DIR}/wasm-smoke.o"
        )
        find_program(ABSOLUTE_NODE_FOR_WASM NAMES node nodejs)
        if(WIN32)
            find_program(ABSOLUTE_WASM_LD_FOR_TEST NAMES wasm-ld wasm-ld.exe
                HINTS
                    "${LLVM_TOOLS_BINARY_DIR}"
                    "${PROJECT_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin")
        else()
            find_program(ABSOLUTE_WASM_LD_FOR_TEST NAMES wasm-ld
                HINTS
                    "${LLVM_TOOLS_BINARY_DIR}"
                    "${PROJECT_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin")
        endif()
        if(Python3_Interpreter_FOUND)
            if(WIN32)
                get_filename_component(
                    ABSOLUTE_ABI_LINKER_NAME "${CMAKE_LINKER}" NAME)
                if(ABSOLUTE_ABI_LINKER_NAME MATCHES "^lld-link")
                    set(ABSOLUTE_ABI_NATIVE_BACKEND windows-lld-link)
                else()
                    set(ABSOLUTE_ABI_NATIVE_BACKEND windows-msvc)
                endif()
                set(ABSOLUTE_ABI_NATIVE_FORMAT pe)
            elseif(APPLE)
                set(ABSOLUTE_ABI_NATIVE_BACKEND macos-mach-o)
                set(ABSOLUTE_ABI_NATIVE_FORMAT macho)
            elseif(ANDROID OR ABSOLUTE_TERMUX)
                set(ABSOLUTE_ABI_NATIVE_BACKEND termux-elf)
                set(ABSOLUTE_ABI_NATIVE_FORMAT elf)
            else()
                set(ABSOLUTE_ABI_NATIVE_BACKEND linux-elf)
                set(ABSOLUTE_ABI_NATIVE_FORMAT elf)
            endif()
            add_test(
                NAME absolute.abi-linker-corpus-native
                COMMAND "${Python3_EXECUTABLE}"
                    "${PROJECT_SOURCE_DIR}/tools/testing/abi_linker_corpus.py"
                    --compiler "$<TARGET_FILE:Absolute-Compiler>"
                    --corpus
                        "${PROJECT_SOURCE_DIR}/tests/abi-linker-corpus.json"
                    --work-dir
                        "${CMAKE_BINARY_DIR}/abi-linker-corpus/native/$<CONFIG>"
                    --backend native
                    --backend-label "${ABSOLUTE_ABI_NATIVE_BACKEND}"
                    --expected-format "${ABSOLUTE_ABI_NATIVE_FORMAT}")
            set_tests_properties(
                absolute.abi-linker-corpus-native PROPERTIES
                PASS_REGULAR_EXPRESSION
                    "abi-linker-corpus=ok backend=${ABSOLUTE_ABI_NATIVE_BACKEND}"
                LABELS "abi;linker;native"
                TIMEOUT 300)
        endif()
        if(ABSOLUTE_NODE_FOR_WASM AND ABSOLUTE_WASM_LD_FOR_TEST)
            add_test(
                NAME absolute.run-wasm-export
                COMMAND "${CMAKE_COMMAND}"
                    -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                    -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-export-only.abs
                    -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-export-only.wasm
                    -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                    -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                    -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-export.cmake"
            )
            if(ABSOLUTE_WASM_SHIM_OBJECT)
                if(Python3_Interpreter_FOUND)
                    add_test(
                        NAME absolute.abi-linker-corpus-wasm
                        COMMAND "${Python3_EXECUTABLE}"
                            "${PROJECT_SOURCE_DIR}/tools/testing/abi_linker_corpus.py"
                            --compiler "$<TARGET_FILE:Absolute-Compiler>"
                            --corpus
                                "${PROJECT_SOURCE_DIR}/tests/abi-linker-corpus.json"
                            --work-dir
                                "${CMAKE_BINARY_DIR}/abi-linker-corpus/wasm/$<CONFIG>"
                            --backend wasm
                            --backend-label wasm-ld
                            --expected-format wasm
                            --node "${ABSOLUTE_NODE_FOR_WASM}"
                            --wasm-host
                                "${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js"
                            --wasm-ld "${ABSOLUTE_WASM_LD_FOR_TEST}")
                    set_tests_properties(
                        absolute.abi-linker-corpus-wasm PROPERTIES
                        PASS_REGULAR_EXPRESSION
                            "abi-linker-corpus=ok backend=wasm-ld"
                        LABELS "abi;linker;wasm"
                        TIMEOUT 300)
                    add_test(
                        NAME absolute.native-wasm-differential
                        COMMAND "${Python3_EXECUTABLE}"
                            "${PROJECT_SOURCE_DIR}/tools/testing/native_wasm_differential.py"
                            --compiler "$<TARGET_FILE:Absolute-Compiler>"
                            --node "${ABSOLUTE_NODE_FOR_WASM}"
                            --wasm-host
                                "${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js"
                            --wasm-ld "${ABSOLUTE_WASM_LD_FOR_TEST}"
                            --work-dir
                                "${CMAKE_BINARY_DIR}/differential/native-wasm/$<CONFIG>"
                            --configuration "$<CONFIG>")
                    set_tests_properties(
                        absolute.native-wasm-differential PROPERTIES
                        PASS_REGULAR_EXPRESSION
                            "native-wasm-differential=ok"
                        TIMEOUT 180)
                endif()
                add_test(
                    NAME absolute.run-wasm-smoke
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-smoke.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-smoke.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-smoke.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-managed
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-managed.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-managed.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-managed.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-std-string
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/std-string-wasm.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/std-string.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-std-string.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-std-deque
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/std-deque.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/std-deque.wasm
                        -DMARKER=std-deque=ok
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-marker.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-std-priority-queue
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/std-priority-queue.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/std-priority-queue.wasm
                        -DMARKER=std-priority-queue=ok
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-marker.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-generic-callback-abi
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/generic-callback-abi.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/generic-callback-abi.wasm
                        -DMARKER=generic-callback-abi=ok
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-marker.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-std-hash-collections
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/std-hash-collections.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/std-hash-collections.wasm
                        -DMARKER=std-hash-collections=ok
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-marker.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-std-typed-channel
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/std-typed-channel.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/std-typed-channel.wasm
                        -DMARKER=std-typed-channel=ok
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-marker.cmake"
                )
                if(TARGET Absolute-Math-Plugin)
                    add_test(
                        NAME absolute.run-wasm-math-plugin
                        COMMAND "${CMAKE_COMMAND}"
                            -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                            -DSOURCE=${PROJECT_SOURCE_DIR}/tests/math-plugin-wasm.abs
                            -DPLUGIN=${ABSOLUTE_MATH_MANIFEST}
                            -DOUTPUT=${CMAKE_BINARY_DIR}/math-plugin.wasm
                            -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                            -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                            -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-math-plugin.cmake"
                    )
                endif()
                add_test(
                    NAME absolute.run-wasm-task
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-task.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-task.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-task.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-task-mt
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-task-mt.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-task-mt.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-task-mt.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-fs
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-fs.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-fs.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-fs.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-full-runtime
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-full-runtime.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-full-runtime.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-full-runtime.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-http
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-http.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-http.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-http.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-browser-host
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-http.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-browser-host.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DBROWSER_HOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-browser-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-browser-host.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-browser-session
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-http.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-browser-session.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DREPO_ROOT=${PROJECT_SOURCE_DIR}
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-browser-session.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-browser-task-pool
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-task-mt.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-browser-task-pool.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-browser-task-pool.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-net
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-net.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-net.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-net.cmake"
                )
                add_test(
                    NAME absolute.run-wasm-net-real
                    COMMAND "${CMAKE_COMMAND}"
                        -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                        -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-net-real.abs
                        -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-net-real.wasm
                        -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                        -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                        -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-net-real.cmake"
                )
                if(ABSOLUTE_WASM_SHARED_OBJECT)
                    add_test(
                        NAME absolute.run-wasm-shared-memory
                        COMMAND "${CMAKE_COMMAND}"
                            -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                            -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-smoke.abs
                            -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-shared-memory.wasm
                            -DSHARED_RUNTIME=${ABSOLUTE_WASM_SHARED_OBJECT}
                            -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                            -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                            -DWASM_LD=${ABSOLUTE_WASM_LD_FOR_TEST}
                            -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-shared-memory.cmake"
                    )
                    add_test(
                        NAME absolute.run-wasm-shared-tasks
                        COMMAND "${CMAKE_COMMAND}"
                            -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                            -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-task-mt.abs
                            -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-shared-tasks.wasm
                            -DSHARED_RUNTIME=${ABSOLUTE_WASM_SHARED_OBJECT}
                            -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                            -DHOST_JS=${PROJECT_SOURCE_DIR}/tools/absolute-wasm-host.js
                            -DWASM_LD=${ABSOLUTE_WASM_LD_FOR_TEST}
                            -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-shared-tasks.cmake"
                    )
                endif()
                if(ABSOLUTE_WASM_WASI_OBJECT)
                    add_test(
                        NAME absolute.build-wasm-wasi-smoke
                        COMMAND "${CMAKE_COMMAND}"
                            -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                            -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-smoke.abs
                            -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-smoke-wasi.wasm
                            -DWASI_RUNTIME=${ABSOLUTE_WASM_WASI_OBJECT}
                            -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                            -DWASM_LD=${ABSOLUTE_WASM_LD_FOR_TEST}
                            -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-wasi.cmake"
                    )
                    if(ABSOLUTE_WASI_LIBC)
                        add_test(
                            NAME absolute.run-wasm-wasi-libc
                            COMMAND "${CMAKE_COMMAND}"
                                -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                                -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-smoke.abs
                                -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-wasi-libc.wasm
                                -DWASI_RUNTIME=${ABSOLUTE_WASM_WASI_OBJECT}
                                -DWASI_LIBC=${ABSOLUTE_WASI_LIBC}
                                -DWASI_SYSROOT=${ABSOLUTE_WASI_SYSROOT}
                                -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                                -DWASM_LD=${ABSOLUTE_WASM_LD_FOR_TEST}
                                -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-wasi-libc.cmake"
                        )
                    endif()
                    add_test(
                        NAME absolute.run-wasm-wasi-services
                        COMMAND "${CMAKE_COMMAND}"
                            -DABSOLUTEC=$<TARGET_FILE:Absolute-Compiler>
                            -DSOURCE=${PROJECT_SOURCE_DIR}/tests/wasm-wasi-services.abs
                            -DOUTPUT=${CMAKE_BINARY_DIR}/wasm-wasi-services.wasm
                            -DWASI_RUNTIME=${ABSOLUTE_WASM_WASI_OBJECT}
                            -DNODE=${ABSOLUTE_NODE_FOR_WASM}
                            -DWASM_LD=${ABSOLUTE_WASM_LD_FOR_TEST}
                            -P "${PROJECT_SOURCE_DIR}/tests/run-wasm-wasi-services.cmake"
                    )
                endif()
            endif()
        endif()

        add_test(
            NAME absolute.emit-export-c-bool
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/export-c-bool.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/export-c-bool.ll"
        )
        set_tests_properties(absolute.emit-export-c-bool PROPERTIES
            FIXTURES_SETUP absolute-export-c-bool-ir
        )
        add_test(
            NAME absolute.check-export-c-bool
            COMMAND "${CMAKE_COMMAND}"
                -DINPUT=${CMAKE_BINARY_DIR}/export-c-bool.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-export-c-bool.cmake"
        )
        set_tests_properties(absolute.check-export-c-bool PROPERTIES
            FIXTURES_REQUIRED absolute-export-c-bool-ir
        )
        set_tests_properties(absolute.check-export-c-abi PROPERTIES
            FIXTURES_REQUIRED absolute-export-c-abi-ir
        )
        add_test(
            NAME absolute.emit-cfunc-callbacks
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/cfunc-callbacks.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/cfunc-callbacks.ll"
        )
        set_tests_properties(absolute.emit-cfunc-callbacks PROPERTIES
            FIXTURES_SETUP absolute-cfunc-callbacks-ir
        )
        add_test(
            NAME absolute.build-cfunc-callbacks
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/cfunc-callbacks.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/cfunc-callbacks-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-cfunc-callbacks PROPERTIES
            FIXTURES_SETUP absolute-cfunc-callbacks-executable
        )
        add_test(
            NAME absolute.run-cfunc-callbacks
            COMMAND "${CMAKE_BINARY_DIR}/cfunc-callbacks-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-cfunc-callbacks PROPERTIES
            FIXTURES_REQUIRED absolute-cfunc-callbacks-executable
            PASS_REGULAR_EXPRESSION "cfunc-callbacks=ok"
        )
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/NativeHandle-$<CONFIG>.absproj" CONTENT
"{
  \"name\": \"NativeHandle\",
  \"entry\": \"${PROJECT_SOURCE_DIR}/tests/projects/native-handle/src/main.abs\",
  \"sources\": [\"${PROJECT_SOURCE_DIR}/tests/projects/native-handle/src\"],
  \"nativeLibraries\": [\"$<TARGET_FILE:absolute-native-test>\"],
  \"nativeSearchPaths\": []
}
")
        add_test(
            NAME absolute.build-native-handle
            COMMAND Absolute-Compiler build "${CMAKE_BINARY_DIR}/NativeHandle-$<CONFIG>.absproj"
                --build-exe -o "${CMAKE_BINARY_DIR}/native-handle-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-native-handle PROPERTIES
            FIXTURES_SETUP absolute-native-handle-executable
        )
        add_test(
            NAME absolute.run-native-handle
            COMMAND "${CMAKE_BINARY_DIR}/native-handle-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-native-handle PROPERTIES
            FIXTURES_REQUIRED absolute-native-handle-executable
            PASS_REGULAR_EXPRESSION "native-handle=ok"
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

        add_library(absolute-native-test STATIC
            tests/native/native_math.cpp
            tests/native/native_handle.cpp
            tests/native/cfunc_apply.cpp)
        add_library(absolute-load-test-library SHARED tests/native/load_library.cpp)
        set_target_properties(absolute-load-test-library PROPERTIES
            OUTPUT_NAME "absolute-load-test"
        )
        file(GENERATE
            OUTPUT "${CMAKE_BINARY_DIR}/dynamic-load-$<CONFIG>.abs"
            CONTENT
"int32 main() {
    string path = \"./$<TARGET_FILE_NAME:absolute-load-test-library>\";
    assert(!isLoaded(path), \"library must not start in the loader cache\");
    assert(load(path), \"existing dynamic library must load\");
    assert(isLoaded(path), \"loaded library must be visible in the cache\");
    assert(load(path), \"loading the same library must be idempotent\");
    assert(!load(\"./absolute-load-test-missing\"), \"missing library must return false\");
    println(format(\"load-error={}\", loadError()));
    println(\"dynamic-load=ok\");
    return 0;
}
"
        )
        add_test(
            NAME absolute.dynamic-load-semantic
            COMMAND Absolute-Compiler "${CMAKE_BINARY_DIR}/dynamic-load-$<CONFIG>.abs"
        )
        add_test(
            NAME absolute.build-dynamic-load
            COMMAND Absolute-Compiler "${CMAKE_BINARY_DIR}/dynamic-load-$<CONFIG>.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/dynamic-load-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-dynamic-load PROPERTIES
            FIXTURES_SETUP absolute-dynamic-load-executable
        )
        add_test(
            NAME absolute.run-dynamic-load
            COMMAND "${CMAKE_BINARY_DIR}/dynamic-load-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-dynamic-load PROPERTIES
            FIXTURES_REQUIRED absolute-dynamic-load-executable
            WORKING_DIRECTORY "$<TARGET_FILE_DIR:absolute-load-test-library>"
            PASS_REGULAR_EXPRESSION "load-error=.+"
        )
        set(ABSOLUTE_EXPORT_TEST_OBJECT
            "${CMAKE_BINARY_DIR}/absolute-export-library${CMAKE_CXX_OUTPUT_EXTENSION}")
        add_test(NAME absolute.build-library
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/export-library.abs"
                --build-library
                -o "${CMAKE_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}absolute-export${CMAKE_SHARED_LIBRARY_SUFFIX}")
        if(ABSOLUTE_NODE_EXECUTABLE)
            add_test(NAME absolute.cli-project-types
                COMMAND "${CMAKE_COMMAND}"
                    -DCOMPILER=$<TARGET_FILE:Absolute-Compiler>
                    -DNODE=${ABSOLUTE_NODE_EXECUTABLE}
                    -DCLI=${PROJECT_SOURCE_DIR}/tools/absolute-dev.js
                    -DSTD_ENV=${PROJECT_SOURCE_DIR}/std/env.abs
                    -DWORK_DIR=${CMAKE_BINARY_DIR}/cli-project-types
                    -DAPP_SUFFIX=${CMAKE_EXECUTABLE_SUFFIX}
                    -DLIB_PREFIX=${CMAKE_SHARED_LIBRARY_PREFIX}
                    -DLIB_SUFFIX=${CMAKE_SHARED_LIBRARY_SUFFIX}
                    -P "${PROJECT_SOURCE_DIR}/tests/run-cli-project-types.cmake")
        endif()
        add_custom_command(
            OUTPUT "${ABSOLUTE_EXPORT_TEST_OBJECT}"
            COMMAND $<TARGET_FILE:Absolute-Compiler>
                "${PROJECT_SOURCE_DIR}/tests/export-library.abs"
                --emit-object -o "${ABSOLUTE_EXPORT_TEST_OBJECT}"
            DEPENDS Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/export-library.abs"
            VERBATIM
        )
        set_source_files_properties("${ABSOLUTE_EXPORT_TEST_OBJECT}" PROPERTIES
            GENERATED TRUE
            EXTERNAL_OBJECT TRUE
        )
        add_executable(absolute-export-consumer
            tests/native/export_consumer.cpp
            "${ABSOLUTE_EXPORT_TEST_OBJECT}"
        )
        add_test(NAME absolute.run-native-export-consumer
            COMMAND $<TARGET_FILE:absolute-export-consumer>)
        set_tests_properties(absolute.run-native-export-consumer PROPERTIES
            PASS_REGULAR_EXPRESSION "native-export=42"
        )
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/NativeFfi-$<CONFIG>.absproj" CONTENT
"{
  \"name\": \"NativeFfi\",
  \"entry\": \"${PROJECT_SOURCE_DIR}/tests/projects/native-ffi/src/main.abs\",
  \"sources\": [\"${PROJECT_SOURCE_DIR}/tests/projects/native-ffi/src\"],
  \"nativeLibraries\": [\"$<TARGET_FILE:absolute-native-test>\"],
  \"nativeSearchPaths\": []
}
")
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/SanitizerUaf-$<CONFIG>.absproj" CONTENT
"{
  \"name\": \"SanitizerUaf\",
  \"entry\": \"${PROJECT_SOURCE_DIR}/tests/projects/sanitizer-uaf/src/main.abs\",
  \"sources\": [\"${PROJECT_SOURCE_DIR}/tests/projects/sanitizer-uaf/src\"],
  \"nativeLibraries\": [\"$<TARGET_FILE:absolute-native-test>\"],
  \"nativeSearchPaths\": []
}
")
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/SanitizerDoubleFree-$<CONFIG>.absproj" CONTENT
"{
  \"name\": \"SanitizerDoubleFree\",
  \"entry\": \"${PROJECT_SOURCE_DIR}/tests/projects/sanitizer-double-free/src/main.abs\",
  \"sources\": [\"${PROJECT_SOURCE_DIR}/tests/projects/sanitizer-double-free/src\"],
  \"nativeLibraries\": [\"$<TARGET_FILE:absolute-native-test>\"],
  \"nativeSearchPaths\": []
}
")
        file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/SanitizerLeak-$<CONFIG>.absproj" CONTENT
"{
  \"name\": \"SanitizerLeak\",
  \"entry\": \"${PROJECT_SOURCE_DIR}/tests/projects/sanitizer-leak/src/main.abs\",
  \"sources\": [\"${PROJECT_SOURCE_DIR}/tests/projects/sanitizer-leak/src\"],
  \"nativeLibraries\": [\"$<TARGET_FILE:absolute-native-test>\"],
  \"nativeSearchPaths\": []
}
")
        if(WIN32 OR ABSOLUTE_TERMUX)
            set(ABSOLUTE_SANITIZER_DETECT_LEAKS OFF)
        else()
            set(ABSOLUTE_SANITIZER_DETECT_LEAKS ON)
        endif()

        add_test(
            NAME absolute.build-sanitizer-uaf
            COMMAND Absolute-Compiler build "${CMAKE_BINARY_DIR}/SanitizerUaf-$<CONFIG>.absproj"
                --sanitize=address --build-exe
                -o "${CMAKE_BINARY_DIR}/sanitizer-uaf-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-sanitizer-uaf PROPERTIES
            FIXTURES_SETUP absolute-sanitizer-uaf-executable
        )
        add_test(
            NAME absolute.run-sanitizer-uaf
            COMMAND "${CMAKE_COMMAND}"
                -DEXECUTABLE=${CMAKE_BINARY_DIR}/sanitizer-uaf-app${CMAKE_EXECUTABLE_SUFFIX}
                -DEXPECT=heap-use-after-free
                -DDETECT_LEAKS=${ABSOLUTE_SANITIZER_DETECT_LEAKS}
                -P "${PROJECT_SOURCE_DIR}/tests/run-sanitizer-case.cmake"
        )
        set_tests_properties(absolute.run-sanitizer-uaf PROPERTIES
            FIXTURES_REQUIRED absolute-sanitizer-uaf-executable
        )
        add_test(
            NAME absolute.build-sanitizer-double-free
            COMMAND Absolute-Compiler build "${CMAKE_BINARY_DIR}/SanitizerDoubleFree-$<CONFIG>.absproj"
                --sanitize=address --build-exe
                -o "${CMAKE_BINARY_DIR}/sanitizer-double-free-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-sanitizer-double-free PROPERTIES
            FIXTURES_SETUP absolute-sanitizer-double-free-executable
        )
        add_test(
            NAME absolute.run-sanitizer-double-free
            COMMAND "${CMAKE_COMMAND}"
                -DEXECUTABLE=${CMAKE_BINARY_DIR}/sanitizer-double-free-app${CMAKE_EXECUTABLE_SUFFIX}
                -DEXPECT=double-free
                -DDETECT_LEAKS=${ABSOLUTE_SANITIZER_DETECT_LEAKS}
                -P "${PROJECT_SOURCE_DIR}/tests/run-sanitizer-case.cmake"
        )
        set_tests_properties(absolute.run-sanitizer-double-free PROPERTIES
            FIXTURES_REQUIRED absolute-sanitizer-double-free-executable
        )
        add_test(
            NAME absolute.build-sanitizer-leak
            COMMAND Absolute-Compiler build "${CMAKE_BINARY_DIR}/SanitizerLeak-$<CONFIG>.absproj"
                --sanitize=address --build-exe
                -o "${CMAKE_BINARY_DIR}/sanitizer-leak-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-sanitizer-leak PROPERTIES
            FIXTURES_SETUP absolute-sanitizer-leak-executable
        )
        add_test(
            NAME absolute.run-sanitizer-leak
            COMMAND "${CMAKE_COMMAND}"
                -DEXECUTABLE=${CMAKE_BINARY_DIR}/sanitizer-leak-app${CMAKE_EXECUTABLE_SUFFIX}
                -DEXPECT=memory.leak.detected
                -DDETECT_LEAKS=${ABSOLUTE_SANITIZER_DETECT_LEAKS}
                -P "${PROJECT_SOURCE_DIR}/tests/run-sanitizer-case.cmake"
        )
        set_tests_properties(absolute.run-sanitizer-leak PROPERTIES
            FIXTURES_REQUIRED absolute-sanitizer-leak-executable
        )
        if(ABSOLUTE_TERMUX)
            # Android's ASan runtime explicitly does not implement LeakSanitizer.
            # The compiler's static leak diagnostics remain covered separately.
            set_tests_properties(absolute.run-sanitizer-leak PROPERTIES DISABLED TRUE)
        endif()
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
            NAME absolute.run-package-manager-plugins-native
            COMMAND "${CMAKE_COMMAND}"
                -DCOMPILER=$<TARGET_FILE:Absolute-Compiler>
                -DMATH_PLUGIN=$<TARGET_FILE:Absolute-Math-Plugin>
                -DNATIVE_LIBRARY=$<TARGET_FILE:absolute-native-test>
                -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/package-manager-plugins-native
                -P "${PROJECT_SOURCE_DIR}/tests/run-package-manager-plugins-native.cmake"
        )
        add_test(
            NAME absolute.run-plugin-p0-p1-features
            COMMAND "${CMAKE_COMMAND}"
                -DCOMPILER=$<TARGET_FILE:Absolute-Compiler>
                -DMATH_PLUGIN=$<TARGET_FILE:Absolute-Math-Plugin>
                -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/plugin-p0-p1-features
                -P "${PROJECT_SOURCE_DIR}/tests/run-plugin-p0-p1-features.cmake"
        )
        add_test(
            NAME absolute.run-plugin-p3-features
            COMMAND "${CMAKE_COMMAND}"
                -DCOMPILER=$<TARGET_FILE:Absolute-Compiler>
                -DMATH_PLUGIN=$<TARGET_FILE:Absolute-Math-Plugin>
                -DWORK_DIR=${CMAKE_BINARY_DIR}/tests/plugin-p3-features
                -P "${PROJECT_SOURCE_DIR}/tests/run-plugin-p3-features.cmake"
        )
        add_executable(plugin-api-unit-tests tests/native/plugin_api_unit_tests.cpp Absolute-Compiler/src/package_manager.cpp Absolute-Compiler/src/plugin_loader.cpp)
        target_include_directories(plugin-api-unit-tests PRIVATE
            Absolute-Compiler/include
            Absolute-Parser/include
            Absolute-Analyzer/include
            Absolute-CodeGen/include
        )
        target_link_libraries(plugin-api-unit-tests PRIVATE Absolute-Parser Absolute-Analyzer Absolute-CodeGen Absolute-Runtime)
        add_test(NAME absolute.plugin-api-unit-tests COMMAND plugin-api-unit-tests)
        add_test(
            NAME absolute.build-export-c-abi
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/export-c-abi.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/export-c-abi-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-export-c-abi PROPERTIES
            FIXTURES_SETUP absolute-export-c-abi-executable
        )
        add_test(NAME absolute.run-export-c-abi
            COMMAND "${CMAKE_BINARY_DIR}/export-c-abi-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-export-c-abi PROPERTIES
            FIXTURES_REQUIRED absolute-export-c-abi-executable
            PASS_REGULAR_EXPRESSION "export=42"
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
            NAME absolute.build-resource-fields
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/resource-fields.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/resource-fields-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-resource-fields PROPERTIES
            FIXTURES_SETUP absolute-resource-fields-executable
        )
        add_test(NAME absolute.run-resource-fields
            COMMAND "${CMAKE_BINARY_DIR}/resource-fields-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-resource-fields PROPERTIES
            FIXTURES_REQUIRED absolute-resource-fields-executable
            PASS_REGULAR_EXPRESSION "resource-fields=ok"
        )
        add_test(
            NAME absolute.build-std-deque
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-deque.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-deque-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-deque PROPERTIES
            FIXTURES_SETUP absolute-std-deque-executable
        )
        add_test(NAME absolute.run-std-deque
            COMMAND "${CMAKE_BINARY_DIR}/std-deque-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-deque PROPERTIES
            FIXTURES_REQUIRED absolute-std-deque-executable
            PASS_REGULAR_EXPRESSION "std-deque=ok"
        )
        add_test(
            NAME absolute.build-std-priority-queue
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-priority-queue.abs"
                --build-exe
                -o "${CMAKE_BINARY_DIR}/std-priority-queue-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-priority-queue PROPERTIES
            FIXTURES_SETUP absolute-std-priority-queue-executable
        )
        add_test(NAME absolute.run-std-priority-queue
            COMMAND "${CMAKE_BINARY_DIR}/std-priority-queue-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-priority-queue PROPERTIES
            FIXTURES_REQUIRED absolute-std-priority-queue-executable
            PASS_REGULAR_EXPRESSION "std-priority-queue=ok"
        )
        add_test(
            NAME absolute.build-std-hash-collections
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-hash-collections.abs"
                --build-exe
                -o "${CMAKE_BINARY_DIR}/std-hash-collections-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-hash-collections PROPERTIES
            FIXTURES_SETUP absolute-std-hash-collections-executable
        )
        add_test(NAME absolute.run-std-hash-collections
            COMMAND "${CMAKE_BINARY_DIR}/std-hash-collections-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-hash-collections PROPERTIES
            FIXTURES_REQUIRED absolute-std-hash-collections-executable
            PASS_REGULAR_EXPRESSION "std-hash-collections=ok"
        )
        add_test(
            NAME absolute.build-collection-snapshot-iteration
            COMMAND Absolute-Compiler
                "${PROJECT_SOURCE_DIR}/tests/collection-snapshot-iteration.abs"
                --build-exe
                -o "${CMAKE_BINARY_DIR}/collection-snapshot-iteration-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-collection-snapshot-iteration PROPERTIES
            FIXTURES_SETUP absolute-collection-snapshot-iteration-executable
        )
        add_test(
            NAME absolute.run-collection-snapshot-iteration
            COMMAND
                "${CMAKE_BINARY_DIR}/collection-snapshot-iteration-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-collection-snapshot-iteration PROPERTIES
            FIXTURES_REQUIRED absolute-collection-snapshot-iteration-executable
            PASS_REGULAR_EXPRESSION "collection-snapshot=ok"
        )
        add_test(
            NAME absolute.build-collection-builder-capability
            COMMAND Absolute-Compiler
                "${PROJECT_SOURCE_DIR}/tests/collection-builder-capability.abs"
                --build-exe
                -o "${CMAKE_BINARY_DIR}/collection-builder-capability-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-collection-builder-capability PROPERTIES
            FIXTURES_SETUP absolute-collection-builder-capability-executable
        )
        add_test(
            NAME absolute.run-collection-builder-capability
            COMMAND
                "${CMAKE_BINARY_DIR}/collection-builder-capability-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-collection-builder-capability PROPERTIES
            FIXTURES_REQUIRED absolute-collection-builder-capability-executable
        )
        add_test(
            NAME absolute.build-collection-algorithms
            COMMAND Absolute-Compiler
                "${PROJECT_SOURCE_DIR}/tests/collection-algorithms.abs"
                --build-exe
                -o "${CMAKE_BINARY_DIR}/collection-algorithms-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-collection-algorithms PROPERTIES
            FIXTURES_SETUP absolute-collection-algorithms-executable
        )
        add_test(
            NAME absolute.run-collection-algorithms
            COMMAND
                "${CMAKE_BINARY_DIR}/collection-algorithms-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.run-collection-algorithms PROPERTIES
            FIXTURES_REQUIRED absolute-collection-algorithms-executable
            PASS_REGULAR_EXPRESSION "collection-algorithms=ok"
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
            NAME absolute.build-properties-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/properties-codegen.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/properties-codegen-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-properties-codegen PROPERTIES
            FIXTURES_SETUP absolute-properties-codegen-executable
        )
        add_test(NAME absolute.run-properties-codegen
            COMMAND "${CMAKE_BINARY_DIR}/properties-codegen-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-properties-codegen PROPERTIES
            FIXTURES_REQUIRED absolute-properties-codegen-executable
            PASS_REGULAR_EXPRESSION "properties=ok"
        )
        add_test(
            NAME absolute.build-indexers-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/indexers-codegen.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/indexers-codegen-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-indexers-codegen PROPERTIES
            FIXTURES_SETUP absolute-indexers-codegen-executable
        )
        add_test(NAME absolute.run-indexers-codegen
            COMMAND "${CMAKE_BINARY_DIR}/indexers-codegen-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-indexers-codegen PROPERTIES
            FIXTURES_REQUIRED absolute-indexers-codegen-executable
            PASS_REGULAR_EXPRESSION "indexers=ok"
        )
        add_test(
            NAME absolute.build-interface-static-codegen
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/interface-static-codegen.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/interface-static-codegen-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-interface-static-codegen PROPERTIES
            FIXTURES_SETUP absolute-interface-static-codegen-executable
        )
        add_test(NAME absolute.run-interface-static-codegen
            COMMAND "${CMAKE_BINARY_DIR}/interface-static-codegen-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-interface-static-codegen PROPERTIES
            FIXTURES_REQUIRED absolute-interface-static-codegen-executable
            PASS_REGULAR_EXPRESSION "interface-static=ok"
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
            NAME absolute.build-implicit-conversions
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/implicit-conversions.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/implicit-conversions-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-implicit-conversions PROPERTIES
            FIXTURES_SETUP absolute-implicit-conversions-executable
        )
        add_test(NAME absolute.run-implicit-conversions
            COMMAND "${CMAKE_BINARY_DIR}/implicit-conversions-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-implicit-conversions PROPERTIES
            FIXTURES_REQUIRED absolute-implicit-conversions-executable
            PASS_REGULAR_EXPRESSION "implicit-conversions=ok"
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
            NAME absolute.build-generic-callback-abi
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generic-callback-abi.abs"
                --build-exe
                -o "${CMAKE_BINARY_DIR}/generic-callback-abi-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-generic-callback-abi PROPERTIES
            FIXTURES_SETUP absolute-generic-callback-abi-executable
        )
        add_test(NAME absolute.run-generic-callback-abi
            COMMAND "${CMAKE_BINARY_DIR}/generic-callback-abi-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-generic-callback-abi PROPERTIES
            FIXTURES_REQUIRED absolute-generic-callback-abi-executable
            PASS_REGULAR_EXPRESSION "generic-callback-abi=ok"
        )
        add_test(
            NAME absolute.build-generic-interfaces
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/generics-interfaces.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/generic-interfaces-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-generic-interfaces PROPERTIES
            FIXTURES_SETUP absolute-generic-interfaces-executable
        )
        add_test(NAME absolute.run-generic-interfaces
            COMMAND "${CMAKE_BINARY_DIR}/generic-interfaces-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-generic-interfaces PROPERTIES
            FIXTURES_REQUIRED absolute-generic-interfaces-executable
            PASS_REGULAR_EXPRESSION "generic-interfaces=ok"
        )
        add_test(
            NAME absolute.build-function-values
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/function-values.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/function-values-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-function-values PROPERTIES
            FIXTURES_SETUP absolute-function-values-executable
        )
        add_test(NAME absolute.run-function-values
            COMMAND "${CMAKE_BINARY_DIR}/function-values-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-function-values PROPERTIES
            FIXTURES_REQUIRED absolute-function-values-executable
            PASS_REGULAR_EXPRESSION "function-values=ok"
        )

        add_test(
            NAME absolute.build-std-time
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-time.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-time-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-time PROPERTIES
            FIXTURES_SETUP absolute-std-time-executable
        )
        add_test(NAME absolute.run-std-time
            COMMAND "${CMAKE_BINARY_DIR}/std-time-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-time PROPERTIES
            FIXTURES_REQUIRED absolute-std-time-executable
            PASS_REGULAR_EXPRESSION "std-time=ok"
        )

        add_test(
            NAME absolute.build-std-core
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-core.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-core-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-core PROPERTIES
            FIXTURES_SETUP absolute-std-core-executable
        )
        add_test(NAME absolute.run-std-core
            COMMAND "${CMAKE_BINARY_DIR}/std-core-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-core PROPERTIES
            FIXTURES_REQUIRED absolute-std-core-executable
            PASS_REGULAR_EXPRESSION "std-core=ok"
        )

        add_test(
            NAME absolute.build-std-random
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-random.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-random-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-random PROPERTIES
            FIXTURES_SETUP absolute-std-random-executable
        )
        add_test(NAME absolute.run-std-random
            COMMAND "${CMAKE_BINARY_DIR}/std-random-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-random PROPERTIES
            FIXTURES_REQUIRED absolute-std-random-executable
            PASS_REGULAR_EXPRESSION "std-random=ok"
        )

        add_test(
            NAME absolute.build-std-fs
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-fs.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-fs-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-fs PROPERTIES
            FIXTURES_SETUP absolute-std-fs-executable
        )
        add_test(NAME absolute.run-std-fs
            COMMAND "${CMAKE_BINARY_DIR}/std-fs-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-fs PROPERTIES
            FIXTURES_REQUIRED absolute-std-fs-executable
            PASS_REGULAR_EXPRESSION "std-fs=ok"
        )

        add_test(
            NAME absolute.build-std-net
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-net.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-net-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-net PROPERTIES
            FIXTURES_SETUP absolute-std-net-executable
        )
        add_test(NAME absolute.run-std-net
            COMMAND "${CMAKE_BINARY_DIR}/std-net-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-net PROPERTIES
            FIXTURES_REQUIRED absolute-std-net-executable
            PASS_REGULAR_EXPRESSION "std-net=ok"
        )

        add_test(
            NAME absolute.build-std-env
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-env.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-env-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-env PROPERTIES
            FIXTURES_SETUP absolute-std-env-executable
        )
        add_test(NAME absolute.run-std-env
            COMMAND "${CMAKE_BINARY_DIR}/std-env-app${CMAKE_EXECUTABLE_SUFFIX}"
                positional "--mode=fast" --output "hello world" --verbose)
        set_tests_properties(absolute.run-std-env PROPERTIES
            FIXTURES_REQUIRED absolute-std-env-executable
            PASS_REGULAR_EXPRESSION "std-env=ok"
        )

        add_test(
            NAME absolute.build-std-process
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-process.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-process-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-process PROPERTIES
            FIXTURES_SETUP absolute-std-process-executable
        )
        add_test(NAME absolute.run-std-process
            COMMAND "${CMAKE_BINARY_DIR}/std-process-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-process PROPERTIES
            FIXTURES_REQUIRED absolute-std-process-executable
            PASS_REGULAR_EXPRESSION "std-process=ok"
        )

        add_test(
            NAME absolute.build-std-datetime
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-datetime.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-datetime-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-datetime PROPERTIES
            FIXTURES_SETUP absolute-std-datetime-executable
        )
        add_test(NAME absolute.run-std-datetime
            COMMAND "${CMAKE_BINARY_DIR}/std-datetime-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-datetime PROPERTIES
            FIXTURES_REQUIRED absolute-std-datetime-executable
            PASS_REGULAR_EXPRESSION "std-datetime=ok"
        )

        add_test(
            NAME absolute.build-std-string
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-string.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-string-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-string PROPERTIES
            FIXTURES_SETUP absolute-std-string-executable
        )
        add_test(NAME absolute.run-std-string
            COMMAND "${CMAKE_BINARY_DIR}/std-string-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-string PROPERTIES
            FIXTURES_REQUIRED absolute-std-string-executable
            PASS_REGULAR_EXPRESSION "std-string=ok"
        )

        add_test(
            NAME absolute.build-string-interpolation
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/string-interpolation.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/string-interpolation-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-string-interpolation PROPERTIES
            FIXTURES_SETUP absolute-string-interpolation-executable
        )
        add_test(NAME absolute.run-string-interpolation
            COMMAND "${CMAKE_BINARY_DIR}/string-interpolation-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-string-interpolation PROPERTIES
            FIXTURES_REQUIRED absolute-string-interpolation-executable
            PASS_REGULAR_EXPRESSION "string-interpolation=ok"
        )

        add_test(
            NAME absolute.build-std-json
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-json.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-json-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-json PROPERTIES
            FIXTURES_SETUP absolute-std-json-executable
        )
        add_test(NAME absolute.run-std-json
            COMMAND "${CMAKE_BINARY_DIR}/std-json-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-json PROPERTIES
            FIXTURES_REQUIRED absolute-std-json-executable
            PASS_REGULAR_EXPRESSION "std-json=ok"
        )

        add_test(
            NAME absolute.build-std-binary
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-binary.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-binary-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-binary PROPERTIES
            FIXTURES_SETUP absolute-std-binary-executable
        )
        add_test(NAME absolute.run-std-binary
            COMMAND "${CMAKE_BINARY_DIR}/std-binary-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-binary PROPERTIES
            FIXTURES_REQUIRED absolute-std-binary-executable
            PASS_REGULAR_EXPRESSION "std-binary=ok"
        )

        add_test(
            NAME absolute.build-std-net-udp
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-net-udp.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-net-udp-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-net-udp PROPERTIES
            FIXTURES_SETUP absolute-std-net-udp-executable
        )
        add_test(NAME absolute.run-std-net-udp
            COMMAND "${CMAKE_BINARY_DIR}/std-net-udp-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-net-udp PROPERTIES
            FIXTURES_REQUIRED absolute-std-net-udp-executable
            PASS_REGULAR_EXPRESSION "std-net-udp=ok"
        )

        add_test(
            NAME absolute.build-std-uri
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-uri.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-uri-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-uri PROPERTIES
            FIXTURES_SETUP absolute-std-uri-executable
        )
        add_test(NAME absolute.run-std-uri
            COMMAND "${CMAKE_BINARY_DIR}/std-uri-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-uri PROPERTIES
            FIXTURES_REQUIRED absolute-std-uri-executable
            PASS_REGULAR_EXPRESSION "std-uri=ok"
        )

        add_test(
            NAME absolute.build-std-http
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-http.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-http-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-http PROPERTIES
            FIXTURES_SETUP absolute-std-http-executable
        )
        add_test(NAME absolute.run-std-http
            COMMAND "${CMAKE_BINARY_DIR}/std-http-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-http PROPERTIES
            FIXTURES_REQUIRED absolute-std-http-executable
            PASS_REGULAR_EXPRESSION "std-http=ok"
        )

        add_test(
            NAME absolute.build-std-log
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-log.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-log-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-log PROPERTIES
            FIXTURES_SETUP absolute-std-log-executable
        )
        add_test(NAME absolute.run-std-log
            COMMAND "${CMAKE_BINARY_DIR}/std-log-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-log PROPERTIES
            FIXTURES_REQUIRED absolute-std-log-executable
            PASS_REGULAR_EXPRESSION "std-log=ok"
        )

        add_test(
            NAME absolute.build-std-assert
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-assert.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-assert-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-assert PROPERTIES
            FIXTURES_SETUP absolute-std-assert-executable
        )
        add_test(NAME absolute.run-std-assert
            COMMAND "${CMAKE_BINARY_DIR}/std-assert-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-assert PROPERTIES
            FIXTURES_REQUIRED absolute-std-assert-executable
            PASS_REGULAR_EXPRESSION "std-assert=ok"
        )

        add_test(
            NAME absolute.build-std-testing
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-testing.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-testing-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-testing PROPERTIES
            FIXTURES_SETUP absolute-std-testing-executable
        )
        add_test(NAME absolute.run-std-testing
            COMMAND "${CMAKE_BINARY_DIR}/std-testing-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-testing PROPERTIES
            FIXTURES_REQUIRED absolute-std-testing-executable
            PASS_REGULAR_EXPRESSION "std-testing=ok"
        )

        add_test(
            NAME absolute.build-script-main
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/script-main.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/script-main-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-script-main PROPERTIES
            FIXTURES_SETUP absolute-script-main-executable
        )
        add_test(NAME absolute.run-script-main
            COMMAND "${CMAKE_BINARY_DIR}/script-main-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-script-main PROPERTIES
            FIXTURES_REQUIRED absolute-script-main-executable
            PASS_REGULAR_EXPRESSION "script-main=42"
        )

        add_test(
            NAME absolute.build-lambda-closures
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/lambda-closures.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/lambda-closures-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-lambda-closures PROPERTIES
            FIXTURES_SETUP absolute-lambda-closures-executable
        )
        add_test(NAME absolute.run-lambda-closures
            COMMAND "${CMAKE_BINARY_DIR}/lambda-closures-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-lambda-closures PROPERTIES
            FIXTURES_REQUIRED absolute-lambda-closures-executable
            PASS_REGULAR_EXPRESSION "lambda-closures=ok"
        )
        add_test(
            NAME absolute.lambda-closure-errors
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/lambda-closure-errors.abs"
        )
        set_tests_properties(absolute.lambda-closure-errors PROPERTIES WILL_FAIL TRUE)
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
        add_test(
            NAME absolute.build-task-scheduling
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/task-scheduling.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/task-scheduling-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-task-scheduling PROPERTIES
            FIXTURES_SETUP absolute-task-scheduling-executable
        )
        add_test(NAME absolute.run-task-scheduling
            COMMAND "${CMAKE_BINARY_DIR}/task-scheduling-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-task-scheduling PROPERTIES
            FIXTURES_REQUIRED absolute-task-scheduling-executable
            PASS_REGULAR_EXPRESSION "task-scheduling=ok"
        )
        add_test(
            NAME absolute.build-task-group
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/task-group.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/task-group-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-task-group PROPERTIES
            FIXTURES_SETUP absolute-task-group-executable
        )
        add_test(NAME absolute.run-task-group
            COMMAND "${CMAKE_BINARY_DIR}/task-group-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-task-group PROPERTIES
            FIXTURES_REQUIRED absolute-task-group-executable
            PASS_REGULAR_EXPRESSION "task-group=ok"
        )
        add_test(
            NAME absolute.build-async-methods
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-methods.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/async-methods-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-async-methods PROPERTIES
            FIXTURES_SETUP absolute-async-methods-executable
        )
        add_test(NAME absolute.run-async-methods
            COMMAND "${CMAKE_BINARY_DIR}/async-methods-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-async-methods PROPERTIES
            FIXTURES_REQUIRED absolute-async-methods-executable
            PASS_REGULAR_EXPRESSION "async-methods=ok"
        )
        add_test(
            NAME absolute.emit-value-references
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/value-references.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/value-references.ll"
        )
        set_tests_properties(absolute.emit-value-references PROPERTIES
            FIXTURES_SETUP absolute-value-references-ir
        )
        add_test(
            NAME absolute.check-value-references-ir
            COMMAND ${CMAKE_COMMAND}
                -DIR_FILE=${CMAKE_BINARY_DIR}/value-references.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-value-references-ir.cmake"
        )
        set_tests_properties(absolute.check-value-references-ir PROPERTIES
            FIXTURES_REQUIRED absolute-value-references-ir
        )
        add_test(
            NAME absolute.build-value-references
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/value-references.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/value-references-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-value-references PROPERTIES
            FIXTURES_SETUP absolute-value-references-executable
        )
        add_test(NAME absolute.run-value-references
            COMMAND "${CMAKE_BINARY_DIR}/value-references-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-value-references PROPERTIES
            FIXTURES_REQUIRED absolute-value-references-executable
            PASS_REGULAR_EXPRESSION "value-references=ok"
        )
        add_test(
            NAME absolute.emit-weak-managed
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/weak-managed-references.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/weak-managed.ll"
        )
        set_tests_properties(absolute.emit-weak-managed PROPERTIES
            FIXTURES_SETUP absolute-weak-managed-ir
        )
        add_test(
            NAME absolute.check-weak-managed-ir
            COMMAND ${CMAKE_COMMAND}
                -DIR_FILE=${CMAKE_BINARY_DIR}/weak-managed.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-weak-managed-ir.cmake"
        )
        set_tests_properties(absolute.check-weak-managed-ir PROPERTIES
            FIXTURES_REQUIRED absolute-weak-managed-ir
        )
        add_test(
            NAME absolute.build-weak-managed
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/weak-managed-references.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/weak-managed-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-weak-managed PROPERTIES
            FIXTURES_SETUP absolute-weak-managed-executable
        )
        add_test(NAME absolute.run-weak-managed
            COMMAND "${CMAKE_BINARY_DIR}/weak-managed-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-weak-managed PROPERTIES
            FIXTURES_REQUIRED absolute-weak-managed-executable
            PASS_REGULAR_EXPRESSION "weak-managed=ok"
        )
        add_test(
            NAME absolute.emit-managed-object-graphs
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-object-graphs.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/managed-object-graphs.ll"
        )
        set_tests_properties(absolute.emit-managed-object-graphs PROPERTIES
            FIXTURES_SETUP absolute-managed-object-graphs-ir
        )
        add_test(
            NAME absolute.check-managed-object-graphs-ir
            COMMAND ${CMAKE_COMMAND}
                -DIR_FILE=${CMAKE_BINARY_DIR}/managed-object-graphs.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-managed-object-graphs-ir.cmake"
        )
        set_tests_properties(absolute.check-managed-object-graphs-ir PROPERTIES
            FIXTURES_REQUIRED absolute-managed-object-graphs-ir
        )
        add_test(
            NAME absolute.build-managed-object-graphs
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-object-graphs.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/managed-object-graphs-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-managed-object-graphs PROPERTIES
            FIXTURES_SETUP absolute-managed-object-graphs-executable
        )
        add_test(NAME absolute.run-managed-object-graphs
            COMMAND "${CMAKE_BINARY_DIR}/managed-object-graphs-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-managed-object-graphs PROPERTIES
            FIXTURES_REQUIRED absolute-managed-object-graphs-executable
            PASS_REGULAR_EXPRESSION "managed-graphs=ok"
            FAIL_REGULAR_EXPRESSION "managed pointer\\(s\\) leaked;memory leak detected"
        )
        add_test(
            NAME absolute.emit-managed-pointer-move
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-pointer-move.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/managed-pointer-move.ll"
        )
        set_tests_properties(absolute.emit-managed-pointer-move PROPERTIES
            FIXTURES_SETUP absolute-managed-pointer-move-ir
        )
        add_test(
            NAME absolute.check-managed-pointer-move-ir
            COMMAND ${CMAKE_COMMAND}
                -DIR_FILE=${CMAKE_BINARY_DIR}/managed-pointer-move.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-managed-pointer-move-ir.cmake"
        )
        set_tests_properties(absolute.check-managed-pointer-move-ir PROPERTIES
            FIXTURES_REQUIRED absolute-managed-pointer-move-ir
        )
        add_test(
            NAME absolute.build-managed-pointer-move
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/managed-pointer-move.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/managed-pointer-move-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-managed-pointer-move PROPERTIES
            FIXTURES_SETUP absolute-managed-pointer-move-executable
        )
        add_test(NAME absolute.run-managed-pointer-move
            COMMAND "${CMAKE_BINARY_DIR}/managed-pointer-move-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-managed-pointer-move PROPERTIES
            FIXTURES_REQUIRED absolute-managed-pointer-move-executable
            PASS_REGULAR_EXPRESSION "managed-move=ok"
            FAIL_REGULAR_EXPRESSION "managed pointer\\(s\\) leaked;memory leak detected"
        )
        add_test(
            NAME absolute.emit-simd-vectorization
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/simd-vectorization.abs"
                --emit-llvm -o "${CMAKE_BINARY_DIR}/simd-vectorization.ll"
        )
        set_tests_properties(absolute.emit-simd-vectorization PROPERTIES
            FIXTURES_SETUP absolute-simd-vectorization-ir
        )
        add_test(
            NAME absolute.check-simd-vectorization-ir
            COMMAND ${CMAKE_COMMAND}
                -DIR_FILE=${CMAKE_BINARY_DIR}/simd-vectorization.ll
                -P "${PROJECT_SOURCE_DIR}/tests/check-simd-vectorization-ir.cmake"
        )
        set_tests_properties(absolute.check-simd-vectorization-ir PROPERTIES
            FIXTURES_REQUIRED absolute-simd-vectorization-ir
        )
        add_test(
            NAME absolute.build-simd-vectorization
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/simd-vectorization.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/simd-vectorization-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-simd-vectorization PROPERTIES
            FIXTURES_SETUP absolute-simd-vectorization-executable
        )
        add_test(NAME absolute.run-simd-vectorization
            COMMAND "${CMAKE_BINARY_DIR}/simd-vectorization-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-simd-vectorization PROPERTIES
            FIXTURES_REQUIRED absolute-simd-vectorization-executable
            PASS_REGULAR_EXPRESSION "simd-vectorization=ok"
        )
        add_test(
            NAME absolute.build-async-concurrency-advanced
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/async-concurrency-advanced.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/async-concurrency-advanced-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-async-concurrency-advanced PROPERTIES
            FIXTURES_SETUP absolute-async-concurrency-advanced-executable
        )
        add_test(NAME absolute.run-async-concurrency-advanced
            COMMAND "${CMAKE_BINARY_DIR}/async-concurrency-advanced-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-async-concurrency-advanced PROPERTIES
            FIXTURES_REQUIRED absolute-async-concurrency-advanced-executable
            PASS_REGULAR_EXPRESSION "async-concurrency-advanced=ok"
        )
        add_test(
            NAME absolute.build-std-typed-channel
            COMMAND Absolute-Compiler "${PROJECT_SOURCE_DIR}/tests/std-typed-channel.abs"
                --build-exe -o "${CMAKE_BINARY_DIR}/std-typed-channel-app${CMAKE_EXECUTABLE_SUFFIX}"
        )
        set_tests_properties(absolute.build-std-typed-channel PROPERTIES
            FIXTURES_SETUP absolute-std-typed-channel-executable
        )
        add_test(NAME absolute.run-std-typed-channel
            COMMAND "${CMAKE_BINARY_DIR}/std-typed-channel-app${CMAKE_EXECUTABLE_SUFFIX}")
        set_tests_properties(absolute.run-std-typed-channel PROPERTIES
            FIXTURES_REQUIRED absolute-std-typed-channel-executable
            PASS_REGULAR_EXPRESSION "std-typed-channel=ok"
        )
    endif()
endif()
