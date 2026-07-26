if(NOT DEFINED OUTPUT OR NOT DEFINED PLUGIN_LIBRARY)
    message(FATAL_ERROR "OUTPUT and PLUGIN_LIBRARY are required")
endif()

file(TO_CMAKE_PATH "${PLUGIN_LIBRARY}" PLUGIN_PATH)
get_filename_component(PLUGIN_NAME "${PLUGIN_PATH}" NAME)
file(WRITE "${OUTPUT}"
"{
  \"name\": \"absolute.shader\",
  \"version\": \"1.0.0\",
  \"abi\": 1,
  \"library\": \"${PLUGIN_NAME}\",
  \"prelude\": \"absolute-shader.prelude.abs\",
  \"editor\": \"absolute-shader.editor.json\",
  \"provides\": [\"shader.ast\", \"shader.compute\"],
  \"dependencies\": {}
}
")
