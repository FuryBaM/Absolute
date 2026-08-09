if(NOT DEFINED COMPILER OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "COMPILER and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}/packages/depA")
file(MAKE_DIRECTORY "${WORK_DIR}/packages/depSub")

file(WRITE "${WORK_DIR}/package.abs" [=[
{
  "name": "AppProject",
  "version": "1.0.0",
  "type": "app",
  "entry": "src/main.abs",
  "registries": ["packages"],
  "dependencies": {
    "depA": "^1.0.0"
  }
}
]=])

# depA transitively depends on depSub
file(WRITE "${WORK_DIR}/packages/depA/package.abs" [=[
{
  "name": "depA",
  "version": "1.2.3",
  "type": "lib",
  "dependencies": {
    "depSub": "~2.1.0"
  }
}
]=])

file(WRITE "${WORK_DIR}/packages/depSub/package.abs" [=[
{
  "name": "depSub",
  "version": "2.1.5",
  "type": "lib",
  "dependencies": {}
}
]=])

file(MAKE_DIRECTORY "${WORK_DIR}/src")
file(WRITE "${WORK_DIR}/src/main.abs" [=[
int32 main() {
    println("package-manifest-transitive=ok");
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

file(READ "${WORK_DIR}/abspackage.lock" LOCKFILE_CONTENT)
if(NOT LOCKFILE_CONTENT MATCHES "depSub" OR NOT LOCKFILE_CONTENT MATCHES "2.1.5")
    message(FATAL_ERROR "Transitive dependency depSub version 2.1.5 not found in lockfile:\n${LOCKFILE_CONTENT}")
endif()

execute_process(
    COMMAND "${WORK_DIR}/app.exe"
    RESULT_VARIABLE RUN_STATUS
    OUTPUT_VARIABLE RUN_OUTPUT
)
if(NOT RUN_STATUS EQUAL 0 OR NOT RUN_OUTPUT MATCHES "package-manifest-transitive=ok")
    message(FATAL_ERROR "Package app execution failed (${RUN_STATUS})\n${RUN_OUTPUT}")
endif()
