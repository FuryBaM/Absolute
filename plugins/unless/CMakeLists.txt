cmake_minimum_required(VERSION 3.20)
project(Absolute-Unless-Plugin LANGUAGES CXX)

add_library(Absolute-Unless-Plugin SHARED unless_plugin.cpp)
target_compile_features(Absolute-Unless-Plugin PRIVATE cxx_std_20)
target_include_directories(Absolute-Unless-Plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/Absolute-Parser/include"
)
set_target_properties(Absolute-Unless-Plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "absolute-unless"
)

