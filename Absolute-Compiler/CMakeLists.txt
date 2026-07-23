cmake_minimum_required(VERSION 3.20)
project(Absolute-Compiler LANGUAGES CXX)

# Устанавливаем стандарт C++
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Включаем расширенные предупреждения
if(MSVC)
    add_compile_options(/W4 /permissive-)  # Включаем предупреждения для MSVC
else()
    add_compile_options(-Wall -Wextra -Wpedantic)  # Для других компиляторов
endif()

# Добавляем исполняемый файл
add_executable(Absolute-Compiler 
    src/Absolute-Compiler.cpp
    src/plugin_loader.cpp
    src/package_manager.cpp
    src/pch.cpp
)

# Подключаем предкомпилированный заголовок (pch)
target_precompile_headers(Absolute-Compiler PRIVATE include/pch.h)

# Подключаем заголовочные файлы
target_include_directories(Absolute-Compiler PRIVATE
    include
    ../Absolute-Parser/include
    ../Absolute-Analyzer/include
)

# Линкуем библиотеки
target_link_libraries(Absolute-Compiler PRIVATE Absolute-Parser Absolute-Analyzer)
target_link_libraries(Absolute-Compiler PRIVATE ${CMAKE_DL_LIBS})
if(TARGET Absolute-CodeGen)
    target_link_libraries(Absolute-Compiler PRIVATE Absolute-CodeGen)
    target_compile_definitions(Absolute-Compiler PRIVATE
        ABSOLUTE_HAS_LLVM=1
        ABSOLUTE_HOST_CXX_COMPILER="${CMAKE_CXX_COMPILER}"
        ABSOLUTE_HOST_LINKER="${CMAKE_LINKER}"
        ABSOLUTE_RUNTIME_LIBRARY="$<TARGET_FILE:Absolute-Runtime>"
    )
    if(CMAKE_DL_LIBS)
        target_compile_definitions(Absolute-Compiler PRIVATE
            ABSOLUTE_RUNTIME_DL_LIBRARY="${CMAKE_DL_LIBS}"
        )
    endif()
    add_dependencies(Absolute-Compiler Absolute-Runtime)
endif()

if(MSVC)
    target_compile_options(Absolute-Compiler PRIVATE
        $<$<CONFIG:Debug>:/Zi>
        $<$<CONFIG:Release>:/O2>
        $<$<CONFIG:Release>:/Zi>
    )
    target_link_options(Absolute-Compiler PRIVATE
        $<$<CONFIG:Debug>:/PDB:${CMAKE_BINARY_DIR}/Debug/Absolute-Compiler.pdb>
        $<$<CONFIG:Release>:/PDB:${CMAKE_BINARY_DIR}/Release/Absolute-Compiler.pdb>
    )
endif()

# Указание выходной директории для библиотек и исполняемых файлов
set_target_properties(Absolute-Compiler PROPERTIES
    OUTPUT_NAME "absolutec"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/$<CONFIG>"
)
