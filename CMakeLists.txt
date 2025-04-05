cmake_minimum_required(VERSION 3.16)
project(Absolute)

# Добавляем подпроекты
add_subdirectory(Absolute-Parser)
add_subdirectory(Absolute-Analyzer)
add_subdirectory(Absolute-Compiler)

# Указание общего места для установки, если требуется
set(CMAKE_INSTALL_PREFIX ${PROJECT_SOURCE_DIR}/install)

# Можно добавить настройку для компиляции и установку зависимости
target_link_libraries(Absolute-Analyzer PRIVATE Absolute-Parser)
target_link_libraries(Absolute-Compiler PRIVATE Absolute-Parser Absolute-Analyzer)