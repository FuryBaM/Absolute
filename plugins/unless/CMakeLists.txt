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
add_custom_command(TARGET Absolute-Unless-Plugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/absolute-unless.editor.json"
        "$<TARGET_FILE_DIR:Absolute-Unless-Plugin>/absolute-unless.editor.json"
)
