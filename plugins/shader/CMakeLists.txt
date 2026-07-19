cmake_minimum_required(VERSION 3.20)
project(Absolute-Shader-Plugin LANGUAGES CXX)

add_library(Absolute-Shader-Plugin SHARED shader_plugin.cpp)
target_compile_features(Absolute-Shader-Plugin PRIVATE cxx_std_20)
target_include_directories(Absolute-Shader-Plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/Absolute-Parser/include"
)
set_target_properties(Absolute-Shader-Plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "absolute-shader"
)
