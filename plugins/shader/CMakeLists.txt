cmake_minimum_required(VERSION 3.20)
project(Absolute-Shader-Plugin LANGUAGES CXX)

add_library(Absolute-Shader-Plugin SHARED
    shader_plugin.cpp
    absolute-shader.prelude.abs
    absolute-shader.editor.json
    write_manifest.cmake
)
target_compile_features(Absolute-Shader-Plugin PRIVATE cxx_std_20)
target_include_directories(Absolute-Shader-Plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/Absolute-Parser/include"
)
set_target_properties(Absolute-Shader-Plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "absolute-shader"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/absolute-shader.prelude.abs;${CMAKE_CURRENT_SOURCE_DIR}/absolute-shader.editor.json;${CMAKE_CURRENT_SOURCE_DIR}/write_manifest.cmake"
)
add_custom_command(TARGET Absolute-Shader-Plugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/absolute-shader.editor.json"
        "$<TARGET_FILE_DIR:Absolute-Shader-Plugin>/absolute-shader.editor.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/absolute-shader.prelude.abs"
        "$<TARGET_FILE_DIR:Absolute-Shader-Plugin>/absolute-shader.prelude.abs"
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT=$<TARGET_FILE_DIR:Absolute-Shader-Plugin>/absolute-shader.absplugin"
        "-DPLUGIN_LIBRARY=$<TARGET_FILE:Absolute-Shader-Plugin>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/write_manifest.cmake"
    VERBATIM
)
