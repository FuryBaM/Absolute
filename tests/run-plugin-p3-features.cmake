file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/packages/pluginP3")

file(WRITE "${WORK_DIR}/packages/pluginP3/pluginP3.absplugin" [=[
{
  "name": "absolute.math",
  "version": "1.0.0",
  "abi": 1,
  "library": "]=] "${MATH_PLUGIN}" [=[",
  "namespace": "math",
  "permissions": ["fs_read", "filesystem.read"],
  "targets": ["windows-x64", "windows"],
  "optional_dependencies": []
}
]=])

file(MAKE_DIRECTORY "${WORK_DIR}/src")
file(WRITE "${WORK_DIR}/src/main.abs" [=[
import MathVirtualModule;

int32 main() {
    int32 val = MathVirtualModule.vmodule_add(100, 200);
    println(format("p3_plugin_features=ok, val={}", val));
    return 0;
}
]=])

file(WRITE "${WORK_DIR}/package.abs" [=[
{
  "name": "PluginP3Test",
  "version": "1.0.0",
  "type": "app",
  "entry": "src/main.abs",
  "plugins": ["]=] "${WORK_DIR}/packages/pluginP3/pluginP3.absplugin" [=["]
}
]=])

execute_process(
    COMMAND "${COMPILER}" build "${WORK_DIR}/package.abs" --build-exe -o "${WORK_DIR}/app.exe"
    RESULT_VARIABLE BUILD_RESULT
    OUTPUT_VARIABLE BUILD_OUTPUT
    ERROR_VARIABLE BUILD_ERROR
)

if (NOT BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Plugin P3 test build failed (${BUILD_RESULT}):\n${BUILD_OUTPUT}\n${BUILD_ERROR}")
endif()

execute_process(
    COMMAND "${WORK_DIR}/app.exe"
    RESULT_VARIABLE RUN_RESULT
    OUTPUT_VARIABLE RUN_OUTPUT
    ERROR_VARIABLE RUN_ERROR
)

if (NOT RUN_RESULT EQUAL 0)
    message(FATAL_ERROR "Plugin P3 test run failed (${RUN_RESULT}):\n${RUN_OUTPUT}\n${RUN_ERROR}")
endif()

if (NOT RUN_OUTPUT MATCHES "p3_plugin_features=ok, val=300")
    message(FATAL_ERROR "Unexpected output from Plugin P3 test:\n${RUN_OUTPUT}")
endif()

message(STATUS "Plugin P3 integration test passed successfully!")
