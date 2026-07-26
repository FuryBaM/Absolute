if(NOT DEFINED OUTPUT OR NOT DEFINED PLUGIN_LIBRARY)
    message(FATAL_ERROR "OUTPUT and PLUGIN_LIBRARY are required")
endif()

file(TO_CMAKE_PATH "${PLUGIN_LIBRARY}" PLUGIN_PATH)
get_filename_component(PLUGIN_NAME "${PLUGIN_PATH}" NAME)
file(WRITE "${OUTPUT}"
"{
  \"name\": \"absolute.math\",
  \"version\": \"2.0.0\",
  \"abi\": 1,
  \"library\": \"${PLUGIN_NAME}\",
  \"prelude\": \"absolute-math.prelude.abs\",
  \"editor\": \"absolute-math.editor.json\",
  \"provides\": [\"math.scalar\", \"math.vector\", \"math.matrix\", \"math.gpu-packing\"],
  \"dependencies\": {}
}
")
