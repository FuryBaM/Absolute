cmake_minimum_required(VERSION 3.20)
project(Absolute-Desktop-Plugin LANGUAGES CXX)

option(ABSOLUTE_DESKTOP_ENABLE_X11 "Build the Linux X11 window backend when available" ON)

if(WIN32)
    set(ABSOLUTE_DESKTOP_RUNTIME_SOURCE desktop_runtime.cpp)
    set(ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES user32.lib gdi32.lib)
else()
    if(ABSOLUTE_DESKTOP_ENABLE_X11)
        find_package(X11 QUIET)
    endif()
    if(X11_FOUND)
        set(ABSOLUTE_DESKTOP_RUNTIME_SOURCE desktop_runtime_x11.cpp)
        set(ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES "${X11_X11_LIB}")
    else()
        message(WARNING "X11 development files not found; absolute.desktop will use its headless backend")
        set(ABSOLUTE_DESKTOP_RUNTIME_SOURCE desktop_runtime_headless.cpp)
        set(ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES "")
    endif()
endif()

add_library(Absolute-Desktop-Runtime STATIC ${ABSOLUTE_DESKTOP_RUNTIME_SOURCE})
target_compile_features(Absolute-Desktop-Runtime PRIVATE cxx_std_20)
if(WIN32)
    target_compile_definitions(Absolute-Desktop-Runtime PRIVATE UNICODE _UNICODE)
elseif(X11_FOUND)
    target_include_directories(Absolute-Desktop-Runtime PRIVATE ${X11_INCLUDE_DIR})
endif()
set_target_properties(Absolute-Desktop-Runtime PROPERTIES OUTPUT_NAME "absolute-desktop-runtime")

add_library(Absolute-Desktop-Plugin SHARED desktop_plugin.cpp)
target_compile_features(Absolute-Desktop-Plugin PRIVATE cxx_std_20)
target_include_directories(Absolute-Desktop-Plugin PRIVATE
    "${CMAKE_SOURCE_DIR}/Absolute-Parser/include"
)
set_target_properties(Absolute-Desktop-Plugin PROPERTIES
    PREFIX ""
    OUTPUT_NAME "absolute-desktop"
)
add_dependencies(Absolute-Desktop-Plugin Absolute-Desktop-Runtime)

add_custom_command(TARGET Absolute-Desktop-Plugin POST_BUILD
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT=$<TARGET_FILE_DIR:Absolute-Desktop-Plugin>/absolute-desktop.absplugin"
        "-DPLUGIN_LIBRARY=$<TARGET_FILE:Absolute-Desktop-Plugin>"
        "-DRUNTIME_LIBRARY=$<TARGET_FILE:Absolute-Desktop-Runtime>"
        "-DSYSTEM_LIBRARIES=${ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/write_manifest.cmake"
    VERBATIM
)
