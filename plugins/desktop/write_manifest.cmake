if(NOT DEFINED OUTPUT OR NOT DEFINED PLUGIN_LIBRARY OR NOT DEFINED RUNTIME_LIBRARY)
    message(FATAL_ERROR "OUTPUT, PLUGIN_LIBRARY and RUNTIME_LIBRARY are required")
endif()

file(TO_CMAKE_PATH "${PLUGIN_LIBRARY}" PLUGIN_PATH)
file(TO_CMAKE_PATH "${RUNTIME_LIBRARY}" RUNTIME_PATH)
get_filename_component(PLUGIN_NAME "${PLUGIN_PATH}" NAME)
get_filename_component(RUNTIME_NAME "${RUNTIME_PATH}" NAME)
set(NATIVE_LIBRARIES "\"${RUNTIME_NAME}\"")
foreach(SYSTEM_LIBRARY IN LISTS SYSTEM_LIBRARIES)
    file(TO_CMAKE_PATH "${SYSTEM_LIBRARY}" SYSTEM_LIBRARY_PATH)
    string(APPEND NATIVE_LIBRARIES ", \"${SYSTEM_LIBRARY_PATH}\"")
endforeach()
file(WRITE "${OUTPUT}"
"{
  \"name\": \"absolute.desktop\",
  \"version\": \"1.0.0\",
  \"abi\": 1,
  \"library\": \"${PLUGIN_NAME}\",
  \"editor\": \"absolute-desktop.editor.json\",
  \"nativeLibraries\": [${NATIVE_LIBRARIES}],
  \"provides\": [\"desktop.window\", \"desktop.framebuffer\", \"desktop.input\"],
  \"dependencies\": {}
}
")
