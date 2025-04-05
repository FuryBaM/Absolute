cmake_minimum_required(VERSION 3.20)
project(Absolute-Analyzer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Конфигурации
set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Configs" FORCE)

# Пути к исходникам
set(SOURCE_FILES
    src/analyzer.cpp
    src/analyzer_pch.cpp
    src/type.cpp
    src/variable.cpp
)

set(HEADER_FILES
    include/analyzer.h
    include/analyzer_pch.h
    include/type.h
    include/variable.h
)

# Создание статической библиотеки
add_library(Absolute-Analyzer STATIC ${SOURCE_FILES} ${HEADER_FILES})

# Предкомпилированные заголовки
target_precompile_headers(Absolute-Analyzer PRIVATE include/analyzer_pch.h)

# Пути к заголовочным файлам
target_include_directories(${PROJECT_NAME} PUBLIC include)
target_include_directories(Absolute-Analyzer PRIVATE include Absolute-Parser/include)

# Линковка с Absolute-Parser
target_link_libraries(Absolute-Analyzer PRIVATE Absolute-Parser)

target_compile_definitions(Absolute-Analyzer PRIVATE _CONSOLE)
target_compile_options(Absolute-Analyzer PRIVATE /W3 /permissive- /std:c++latest /sdl)
	
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_definitions(Absolute-Analyzer PRIVATE _DEBUG)
else()
    target_compile_definitions(Absolute-Analyzer PRIVATE NDEBUG)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)