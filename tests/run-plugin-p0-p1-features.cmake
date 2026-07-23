file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/packages/pluginA")
file(MAKE_DIRECTORY "${WORK_DIR}/packages/pluginB")

file(WRITE "${WORK_DIR}/packages/pluginA/pluginA.absplugin" [=[
{
  "name": "absolute.math",
  "version": "1.0.0",
  "abi": 1,
  "library": "]=] "${MATH_PLUGIN}" [=[",
  "namespace": "math",
  "permissions": ["fs_read"],
  "targets": ["windows-x64", "windows"],
  "optional_dependencies": []
}
]=])

file(WRITE "${WORK_DIR}/packages/pluginB/pluginB.absplugin" [=[
{
  "name": "pluginB",
  "version": "1.0.0",
  "abi": 1,
  "library": "]=] "${MATH_PLUGIN}" [=[",
  "conflicts": ["absolute.math"]
}
]=])

file(MAKE_DIRECTORY "${WORK_DIR}/src")
file(WRITE "${WORK_DIR}/src/main.abs" [=[
int32 main() {
    println("p0_p1_plugins=ok");
    return 0;
}
]=])

file(WRITE "${WORK_DIR}/package.abs" [=[
{
  "name": "PluginP0P1Test",
  "version": "1.0.0",
  "type": "app",
  "entry": "src/main.abs",
  "plugins": ["]=] "${WORK_DIR}/packages/pluginA/pluginA.absplugin" [=["]
}
]=])

execute_process(
    COMMAND "${COMPILER}" build "${WORK_DIR}/package.abs" --build-exe -o "${WORK_DIR}/app.exe"
    RESULT_VARIABLE BUILD_RESULT
    OUTPUT_VARIABLE BUILD_OUTPUT
    ERROR_VARIABLE BUILD_ERROR
)

if (NOT BUILD_RESULT EQUAL 0)
    message(FATAL_ERROR "Plugin P0/P1 test build failed (${BUILD_RESULT}):\n${BUILD_OUTPUT}\n${BUILD_ERROR}")
endif()

execute_process(
    COMMAND "${WORK_DIR}/app.exe"
    RESULT_VARIABLE RUN_RESULT
    OUTPUT_VARIABLE RUN_OUTPUT
    ERROR_VARIABLE RUN_ERROR
)

if (NOT RUN_RESULT EQUAL 0)
    message(FATAL_ERROR "Plugin P0/P1 test run failed (${RUN_RESULT}):\n${RUN_OUTPUT}\n${RUN_ERROR}")
endif()

if (NOT RUN_OUTPUT MATCHES "p0_p1_plugins=ok")
    message(FATAL_ERROR "Unexpected output from Plugin P0/P1 test:\n${RUN_OUTPUT}")
endif()

message(STATUS "Plugin P0/P1 integration test passed successfully!")
