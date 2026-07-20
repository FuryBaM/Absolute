if(NOT DEFINED COMPILER OR NOT DEFINED MATH_PLUGIN OR NOT DEFINED SHADER_PLUGIN OR
   NOT DEFINED SOURCE OR NOT DEFINED WORK_DIR)
    message(FATAL_ERROR "COMPILER, MATH_PLUGIN, SHADER_PLUGIN, SOURCE and WORK_DIR are required")
endif()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")
file(TO_CMAKE_PATH "${MATH_PLUGIN}" MATH_LIBRARY)
file(TO_CMAKE_PATH "${SHADER_PLUGIN}" SHADER_LIBRARY)

file(WRITE "${WORK_DIR}/absolute.math.absplugin" [=[
{
  "dependencies": {},
  "name": "absolute.math",
  "version": "1.2.0",
  "abi": 1,
  "library": "]=] "${MATH_LIBRARY}" [=[",
  "provides": ["absolute.math.types"]
}
]=])

# Object-map dependencies are resolved by name through --plugin-path.
file(WRITE "${WORK_DIR}/absolute.shader.absplugin" [=[
{
  "dependencies": {
    "absolute.math": "^1.1.0"
  },
  "name": "absolute.shader",
  "version": "2.0.0",
  "abi": 1,
  "library": "]=] "${SHADER_LIBRARY}" [=[",
  "requires": ["absolute.math.types"]
}
]=])

execute_process(
    COMMAND "${COMPILER}" "${SOURCE}"
        --plugin "${WORK_DIR}/absolute.shader.absplugin"
        --plugin-path "${WORK_DIR}"
    RESULT_VARIABLE STATUS
    OUTPUT_VARIABLE OUTPUT
    ERROR_VARIABLE ERROR_OUTPUT
)
if(NOT STATUS EQUAL 0)
    message(FATAL_ERROR "Dependency load failed (${STATUS})\n${OUTPUT}\n${ERROR_OUTPUT}")
endif()
string(FIND "${OUTPUT}" "Loaded syntax plugin absolute.math v1.2.0" MATH_POSITION)
string(FIND "${OUTPUT}" "Loaded syntax plugin absolute.shader v2.0.0" SHADER_POSITION)
if(MATH_POSITION LESS 0 OR SHADER_POSITION LESS 0 OR NOT MATH_POSITION LESS SHADER_POSITION)
    message(FATAL_ERROR "Dependencies were not loaded before the root plugin\n${OUTPUT}")
endif()

# The same graph can be declared relative to an .absproj file.
file(MAKE_DIRECTORY "${WORK_DIR}/project/src")
file(COPY "${SOURCE}" DESTINATION "${WORK_DIR}/project/src")
get_filename_component(SOURCE_NAME "${SOURCE}" NAME)
file(WRITE "${WORK_DIR}/project/PluginDependencies.absproj" [=[
{
  "name": "PluginDependencies",
  "entry": "src/]=] "${SOURCE_NAME}" [=[",
  "sources": ["src"],
  "plugins": ["../absolute.shader.absplugin"],
  "pluginSearchPaths": [".."]
}
]=])
execute_process(
    COMMAND "${COMPILER}" build "${WORK_DIR}/project/PluginDependencies.absproj"
    RESULT_VARIABLE PROJECT_STATUS
    OUTPUT_VARIABLE PROJECT_OUTPUT
    ERROR_VARIABLE PROJECT_ERROR
)
if(NOT PROJECT_STATUS EQUAL 0 OR NOT PROJECT_OUTPUT MATCHES "Loaded syntax plugin absolute.math v1.2.0")
    message(FATAL_ERROR "Project dependency load failed (${PROJECT_STATUS})\n${PROJECT_OUTPUT}\n${PROJECT_ERROR}")
endif()

# Array dependencies can pin an explicit manifest path and must enforce ranges.
file(WRITE "${WORK_DIR}/bad-version.absplugin" [=[
{
  "name": "absolute.shader",
  "version": "2.0.0",
  "abi": 1,
  "library": "]=] "${SHADER_LIBRARY}" [=[",
  "dependencies": [
    {"name": "absolute.math", "version": ">=2.0.0 <3.0.0", "path": "absolute.math.absplugin"}
  ]
}
]=])
execute_process(
    COMMAND "${COMPILER}" "${SOURCE}" --plugin "${WORK_DIR}/bad-version.absplugin"
    RESULT_VARIABLE VERSION_STATUS
    OUTPUT_VARIABLE VERSION_OUTPUT
    ERROR_VARIABLE VERSION_ERROR
)
if(VERSION_STATUS EQUAL 0 OR NOT VERSION_ERROR MATCHES "does not satisfy required range")
    message(FATAL_ERROR "Version conflict was not diagnosed\n${VERSION_OUTPUT}\n${VERSION_ERROR}")
endif()

# Cycles are reported before any library is opened.
file(WRITE "${WORK_DIR}/cycle-a.absplugin" [=[
{
  "name": "cycle.a",
  "version": "1.0.0",
  "abi": 1,
  "library": "missing-a.dll",
  "dependencies": [{"name": "cycle.b", "path": "cycle-b.absplugin"}]
}
]=])
file(WRITE "${WORK_DIR}/cycle-b.absplugin" [=[
{
  "name": "cycle.b",
  "version": "1.0.0",
  "abi": 1,
  "library": "missing-b.dll",
  "dependencies": [{"name": "cycle.a", "path": "cycle-a.absplugin"}]
}
]=])
execute_process(
    COMMAND "${COMPILER}" "${SOURCE}" --plugin "${WORK_DIR}/cycle-a.absplugin"
    RESULT_VARIABLE CYCLE_STATUS
    OUTPUT_VARIABLE CYCLE_OUTPUT
    ERROR_VARIABLE CYCLE_ERROR
)
if(CYCLE_STATUS EQUAL 0 OR NOT CYCLE_ERROR MATCHES "Plugin dependency cycle")
    message(FATAL_ERROR "Dependency cycle was not diagnosed\n${CYCLE_OUTPUT}\n${CYCLE_ERROR}")
endif()

message(STATUS "Plugin dependency resolution, versions, capabilities and cycles passed")
