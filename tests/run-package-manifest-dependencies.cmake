if(NOT DEFINED COMPILER OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "COMPILER and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/packages/depA")
file(MAKE_DIRECTORY "${WORK_DIR}/packages/depB")

file(WRITE "${WORK_DIR}/package.abs" [=[
{
  "name": "AppProject",
  "version": "1.0.0",
  "type": "app",
  "entry": "src/main.abs",
  "dependencies": {
    "depA": "^1.0.0",
    "depB": ">=2.0.0"
  }
}
]=])

file(WRITE "${WORK_DIR}/packages/depA/package.abs" [=[
{
  "name": "depA",
  "version": "1.2.3",
  "type": "lib",
  "dependencies": {}
}
]=])

file(WRITE "${WORK_DIR}/packages/depB/package.abs" [=[
{
  "name": "depB",
  "version": "2.1.0",
  "type": "lib",
  "dependencies": {}
}
]=])

file(MAKE_DIRECTORY "${WORK_DIR}/src")
file(WRITE "${WORK_DIR}/src/main.abs" [=[
int32 main() {
    println("package-manifest=ok");
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
if(NOT RUN_STATUS EQUAL 0 OR NOT RUN_OUTPUT MATCHES "package-manifest=ok")
    message(FATAL_ERROR "Package app execution failed (${RUN_STATUS})\n${RUN_OUTPUT}")
endif()
