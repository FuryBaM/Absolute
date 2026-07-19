cmake_minimum_required(VERSION 3.20)
project(Absolute-Math-Plugin LANGUAGES CXX)

add_library(Absolute-Math-Plugin SHARED math_plugin.cpp)
target_compile_features(Absolute-Math-Plugin PRIVATE cxx_std_20)
target_include_directories(Absolute-Math-Plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/Absolute-Parser/include"
)
set_target_properties(Absolute-Math-Plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "absolute-math"
)
add_custom_command(TARGET Absolute-Math-Plugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/absolute-math.editor.json"
        "$<TARGET_FILE_DIR:Absolute-Math-Plugin>/absolute-math.editor.json"
)
