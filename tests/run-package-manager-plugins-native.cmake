if(NOT DEFINED COMPILER OR NOT DEFINED MATH_PLUGIN OR NOT DEFINED NATIVE_LIBRARY OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "COMPILER, MATH_PLUGIN, NATIVE_LIBRARY, and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/packages/depA")

file(TO_CMAKE_PATH "${MATH_PLUGIN}" MATH_PLUGIN_PATH)
file(TO_CMAKE_PATH "${NATIVE_LIBRARY}" NATIVE_LIB_PATH)

file(WRITE "${WORK_DIR}/package.abs" [=[
{
  "name": "PackageWithPluginsAndNative",
  "version": "1.0.0",
  "type": "app",
  "entry": "src/main.abs",
  "registries": ["packages"],
  "plugins": ["]=] "${MATH_PLUGIN_PATH}" [=["],
  "nativeLibraries": ["]=] "${NATIVE_LIB_PATH}" [=["],
  "dependencies": {
    "depA": "^1.0.0"
  }
}
]=])

file(WRITE "${WORK_DIR}/packages/depA/package.abs" [=[
{
  "name": "depA",
  "version": "1.2.0",
  "type": "lib",
  "dependencies": {}
}
]=])

file(MAKE_DIRECTORY "${WORK_DIR}/src")
file(WRITE "${WORK_DIR}/src/main.abs" [=[
extern "C" int32 native_add(int32 a, int32 b);

int32 main() {
    int32 nativeSum = native_add(20, 22);
    println(format("package-plugins-native=ok, sum={}", nativeSum));
    return 0;
}
]=])

execute_process(
    COMMAND "${COMPILER}" build "${WORK_DIR}/package.abs" --build-exe -o "${WORK_DIR}/app.exe"
    RESULT_VARIABLE STATUS
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERROR_OUTPUT
)
if(NOT STATUS EQUAL 0)
    message(FATAL_ERROR "Package build failed (${STATUS})\n${OUTPUT}\n${ERROR_OUTPUT}")
endif()

if(NOT EXISTS "${WORK_DIR}/abspackage.lock")
    message(FATAL_ERROR "Package lockfile abspackage.lock was not generated!")
endif()

execute_process(
    COMMAND "${WORK_DIR}/app.exe"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUTPUT
)
if(NOT RUN_STATUS EQUAL 0 OR NOT RUN_OUTPUT MATCHES "package-plugins-native=ok, sum=42")
    message(FATAL_ERROR "Package app execution failed (${RUN_STATUS})\n${RUN_OUTPUT}")
endif()
