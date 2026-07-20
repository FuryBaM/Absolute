cmake_minimum_required(VERSION 3.20)
project(Absolute-Parser LANGUAGES CXX)

# Опции компиляции
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB HEADER_FILES CONFIGURE_DEPENDS
    "include/*.h"
)

file(GLOB SOURCE_FILES CONFIGURE_DEPENDS
    "src/*.cpp"
    "src/parser/*.cpp"
)

# Создаём статическую библиотеку
add_library(${PROJECT_NAME} STATIC ${SOURCE_FILES} ${HEADER_FILES})

# Указываем директории с заголовочными файлами
target_include_directories(${PROJECT_NAME} PRIVATE src)
target_include_directories(${PROJECT_NAME} PUBLIC include)

# Добавляем поддержку PCH
target_precompile_headers(${PROJECT_NAME} PRIVATE include/parser_pch.h)

# Определяем конфигурации (Debug/Release)
target_compile_definitions(${PROJECT_NAME} PRIVATE 
    $<$<CONFIG:Debug>:_DEBUG>
    $<$<CONFIG:Release>:NDEBUG>
    _CONSOLE
)

# Опции компилятора
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W3>
    $<$<CXX_COMPILER_ID:MSVC>:/permissive->
    $<$<CXX_COMPILER_ID:MSVC>:/MP>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wextra>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wpedantic>
)

# Опции линковки
target_link_options(${PROJECT_NAME} PRIVATE
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:REF>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<CONFIG:Release>>:/OPT:ICF>
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)
