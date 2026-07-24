cmake_minimum_required(VERSION 3.20)
project(Absolute-Desktop-Plugin LANGUAGES CXX)

option(ABSOLUTE_DESKTOP_ENABLE_X11 "Build the Linux X11 window backend when available" ON)

if(WIN32)
    set(ABSOLUTE_DESKTOP_RUNTIME_SOURCE desktop_runtime.cpp)
    set(ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES user32.lib gdi32.lib Xinput9_1_0.lib opengl32.lib windowscodecs.lib ole32.lib winmm.lib d3d11.lib dxgi.lib)
else()
    if(ABSOLUTE_DESKTOP_ENABLE_X11)
        find_package(X11 QUIET)
    endif()
    if(X11_FOUND)
        message(STATUS "absolute.desktop backend: X11")
        set(ABSOLUTE_DESKTOP_RUNTIME_SOURCE desktop_runtime_x11.cpp)
        set(ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES "${X11_X11_LIB}")
        find_package(OpenGL QUIET)
        if(OpenGL_OpenGL_FOUND OR OpenGL_FOUND OR TARGET OpenGL::GL)
            message(STATUS "absolute.desktop GPU: GLX")
            set(ABSOLUTE_DESKTOP_HAS_GLX ON)
            list(APPEND ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES GL)
        else()
            message(STATUS "absolute.desktop GPU: stub (OpenGL/GLX not found)")
        endif()
    else()
        if(ABSOLUTE_DESKTOP_ENABLE_X11)
            message(STATUS "absolute.desktop backend: headless (optional X11 development files not found)")
        else()
            message(STATUS "absolute.desktop backend: headless (X11 disabled)")
        endif()
        set(ABSOLUTE_DESKTOP_RUNTIME_SOURCE desktop_runtime_headless.cpp)
        set(ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES "")
    endif()
    find_package(ZLIB QUIET)
endif()

add_library(Absolute-Desktop-Runtime STATIC
    ${ABSOLUTE_DESKTOP_RUNTIME_SOURCE}
    desktop_soft_sprites.cpp
    desktop_soft_png.cpp
    desktop_soft_text.cpp
    desktop_soft_font.cpp
    desktop_soft_batch.cpp
    desktop_gpu_gl.cpp
    desktop_gpu_d3d11.cpp
    desktop_audio.cpp
    desktop_mesh_obj.cpp
)
target_compile_features(Absolute-Desktop-Runtime PRIVATE cxx_std_20)
if(WIN32)
    target_compile_definitions(Absolute-Desktop-Runtime PRIVATE UNICODE _UNICODE)
    target_link_libraries(Absolute-Desktop-Runtime PUBLIC opengl32 windowscodecs ole32 winmm d3d11 dxgi)
elseif(X11_FOUND)
    target_include_directories(Absolute-Desktop-Runtime PRIVATE ${X11_INCLUDE_DIR})
    if(ABSOLUTE_DESKTOP_HAS_GLX)
        target_compile_definitions(Absolute-Desktop-Runtime PRIVATE ABSOLUTE_DESKTOP_HAS_GLX=1)
        if(TARGET OpenGL::GL)
            target_link_libraries(Absolute-Desktop-Runtime PUBLIC OpenGL::GL)
        else()
            target_link_libraries(Absolute-Desktop-Runtime PUBLIC GL)
        endif()
    endif()
endif()
if(ZLIB_FOUND)
    message(STATUS "absolute.desktop PNG: zlib portable decoder enabled")
    target_compile_definitions(Absolute-Desktop-Runtime PRIVATE ABSOLUTE_DESKTOP_HAS_ZLIB=1)
    target_link_libraries(Absolute-Desktop-Runtime PUBLIC ZLIB::ZLIB)
    if(ZLIB_LIBRARIES)
        list(APPEND ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES ${ZLIB_LIBRARIES})
    else()
        list(APPEND ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES z)
    endif()
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
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/absolute-desktop.editor.json"
        "$<TARGET_FILE_DIR:Absolute-Desktop-Plugin>/absolute-desktop.editor.json"
    COMMAND "${CMAKE_COMMAND}"
        "-DOUTPUT=$<TARGET_FILE_DIR:Absolute-Desktop-Plugin>/absolute-desktop.absplugin"
        "-DPLUGIN_LIBRARY=$<TARGET_FILE:Absolute-Desktop-Plugin>"
        "-DRUNTIME_LIBRARY=$<TARGET_FILE:Absolute-Desktop-Runtime>"
        "-DSYSTEM_LIBRARIES=${ABSOLUTE_DESKTOP_SYSTEM_LIBRARIES}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/write_manifest.cmake"
    VERBATIM
)
