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

