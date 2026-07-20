if(NOT DEFINED COMPILER OR NOT DEFINED PLUGIN OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "COMPILER, PLUGIN and WORK_DIR are required")
endif()

file(TO_CMAKE_PATH "${PLUGIN}" PLUGIN_PATH)
file(MAKE_DIRECTORY "${WORK_DIR}/src")
file(WRITE "${WORK_DIR}/src/main.abs" [=[
int32 main() {
    int32 result = 0;
    unless (false) result = 42;
    return result;
}
]=])
file(WRITE "${WORK_DIR}/PluginProject.absproj"
    "{\n"
    "  \"name\": \"PluginProject\",\n"
    "  \"entry\": \"src/main.abs\",\n"
    "  \"sources\": [\"src\"],\n"
    "  \"plugins\": [\"${PLUGIN_PATH}\"]\n"
    "}\n")

execute_process(
    COMMAND "${COMPILER}" build "${WORK_DIR}/PluginProject.absproj"
    RESULT_VARIABLE STATUS
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERROR_OUTPUT
)
if(NOT STATUS EQUAL 0)
    message(FATAL_ERROR "Plugin project failed (${STATUS})\n${OUTPUT}\n${ERROR_OUTPUT}")
endif()
if(NOT OUTPUT MATCHES "Loaded syntax plugin absolute.unless")
    message(FATAL_ERROR "Plugin project did not load absolute.unless\n${OUTPUT}")
endif()

