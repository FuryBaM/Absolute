cmake_minimum_required(VERSION 3.20)
project(Absolute-Compiler LANGUAGES CXX)

# Устанавливаем стандарт C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Включаем расширенные предупреждения
if(MSVC)
    add_compile_options(/W4 /permissive-)  # Включаем предупреждения для MSVC
else()
    add_compile_options(-Wall -Wextra -Wpedantic)  # Для других компиляторов
endif()

# Добавляем исполняемый файл
add_executable(Absolute-Compiler 
    src/Absolute-Compiler.cpp
    src/pch.cpp
)

# Подключаем предкомпилированный заголовок (pch)
target_precompile_headers(Absolute-Compiler PRIVATE include/pch.h)

# Подключаем заголовочные файлы
target_include_directories(Absolute-Compiler PRIVATE include)

# Линкуем библиотеки
target_link_libraries(Absolute-Compiler PRIVATE Absolute-Parser Absolute-Analyzer)

# Настройки для конфигурации Debug
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} /Zi /W3")  # Флаг для отладочных символов (PDB) и предупреждения
set(CMAKE_EXE_LINKER_FLAGS_DEBUG "${CMAKE_EXE_LINKER_FLAGS_DEBUG} /PDB:${CMAKE_BINARY_DIR}/Debug/Absolute-Compiler.pdb")  # Для генерации PDB

# Настройки для конфигурации Release
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /DNDEBUG /Zi")  # Оптимизация и отладочные символы
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /PDB:${CMAKE_BINARY_DIR}/Release/Absolute-Compiler.pdb")  # Для генерации PDB

# Указание выходной директории для библиотек и исполняемых файлов
set_target_properties(Absolute-Compiler PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)