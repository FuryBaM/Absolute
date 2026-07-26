cmake_minimum_required(VERSION 3.20)
project(Absolute-Math-Plugin LANGUAGES CXX)

add_library(Absolute-Math-Plugin SHARED
    math_plugin.cpp
    absolute-math.prelude.abs
    absolute-math.editor.json
    write_manifest.cmake
)
target_compile_features(Absolute-Math-Plugin PRIVATE cxx_std_20)
target_include_directories(Absolute-Math-Plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/Absolute-Parser/include"
)
set_target_properties(Absolute-Math-Plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "absolute-math"
    LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/absolute-math.prelude.abs;${CMAKE_CURRENT_SOURCE_DIR}/absolute-math.editor.json;${CMAKE_CURRENT_SOURCE_DIR}/write_manifest.cmake"
)
add_custom_command(TARGET Absolute-Math-Plugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/absolute-math.editor.json"
        "$<TARGET_FILE_DIR:Absolute-Math-Plugin>/absolute-math.editor.json"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/absolute-math.prelude.abs"
        "$<TARGET_FILE_DIR:Absolute-Math-Plugin>/absolute-math.prelude.abs"
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT=$<TARGET_FILE_DIR:Absolute-Math-Plugin>/absolute-math.absplugin"
        "-DPLUGIN_LIBRARY=$<TARGET_FILE:Absolute-Math-Plugin>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/write_manifest.cmake"
    VERBATIM
)
