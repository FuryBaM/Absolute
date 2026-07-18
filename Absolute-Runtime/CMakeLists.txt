cmake_minimum_required(VERSION 3.20)
project(Absolute-Runtime LANGUAGES CXX)

add_library(Absolute-Runtime STATIC src/pointers.cpp)
target_compile_features(Absolute-Runtime PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(Absolute-Runtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(Absolute-Runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(Absolute-Runtime PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
