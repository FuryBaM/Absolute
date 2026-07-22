cmake_minimum_required(VERSION 3.20)
project(Absolute-Runtime LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(Absolute-Runtime STATIC src/errors.cpp src/pointers.cpp src/tasks.cpp src/time.cpp)
target_compile_features(Absolute-Runtime PUBLIC cxx_std_20)
target_link_libraries(Absolute-Runtime PUBLIC Threads::Threads)

if(MSVC)
    target_compile_options(Absolute-Runtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(Absolute-Runtime PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(Absolute-Runtime PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
