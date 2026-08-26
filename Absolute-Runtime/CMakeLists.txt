cmake_minimum_required(VERSION 3.20)
project(Absolute-Runtime LANGUAGES CXX)

find_package(Threads REQUIRED)

set(ABSOLUTE_RUNTIME_SOURCES
    src/android_pthread_affinity_compat.h
    src/binary.cpp
    src/bytes.cpp
    src/datetime.cpp
    src/env.cpp
    src/errors.cpp
    src/filesystem.cpp
    src/http_tls.cpp
    src/json.cpp
    src/libraries.cpp
    src/network.cpp
    src/pointers.cpp
    src/process.cpp
    src/random.cpp
    src/real_text.cpp
    src/real_text.h
    src/scheduler_fiber.h
    src/scheduler_io.h
    src/socket_reactor.cpp
    src/socket_reactor.h
    src/socket_reactor_kqueue.cpp
    src/string.cpp
    src/tasks.cpp
    src/time.cpp
)

add_library(Absolute-Runtime STATIC ${ABSOLUTE_RUNTIME_SOURCES})
target_compile_features(Absolute-Runtime PUBLIC cxx_std_20)
# The archive is linked into shared libraries as well as executables: a program
# built with --build-library pulls in whatever of the runtime it uses, and a
# string's storage is reference counted by the runtime now, so string.cpp is
# part of that for any library that holds one. Without position-independent
# code its thread-local accesses cannot be relocated into a shared object.
set_target_properties(Absolute-Runtime PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(Absolute-Runtime PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
# lli JITs a module by itself, so a test that touches a managed pointer only
# resolves `absolute_managed_*` if the runtime is handed to it. The static
# archive cannot serve that purpose: its thread-local accesses carry
# R_X86_64_GOTTPOFF relocations the JIT linker rejects. A shared build loaded
# through the real dynamic loader resolves them normally.
option(ABSOLUTE_BUILD_RUNTIME_SHARED
    "Also build the Absolute runtime as a shared library for lli-based tests" OFF)
if(ABSOLUTE_BUILD_RUNTIME_SHARED)
    add_library(Absolute-Runtime-Shared SHARED ${ABSOLUTE_RUNTIME_SOURCES})
    target_compile_features(Absolute-Runtime-Shared PUBLIC cxx_std_20)
    target_link_libraries(Absolute-Runtime-Shared PUBLIC
        Threads::Threads ${CMAKE_DL_LIBS})
    set_target_properties(Absolute-Runtime-Shared PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
endif()

# A second copy of the runtime, instrumented for ThreadSanitizer. It exists
# because TSan is whole-program: a race between generated code and the runtime's
# own scheduler is only visible if both sides are instrumented, and an
# uninstrumented library on the other side of a lock produces false reports
# rather than silence. Probed rather than assumed -- TSan needs a supported
# target and a compiler that ships its runtime -- and skipped where it is not
# available, which is what the ABSOLUTE_RUNTIME_TSAN_AVAILABLE answer is for.
# Building it costs a second compile of the runtime, so it follows
# BUILD_TESTING by default: a build that runs the suite gets the option
# checked, a plain release build does not pay for it unless it asks.
option(ABSOLUTE_BUILD_RUNTIME_TSAN
    "Build a ThreadSanitizer copy of the runtime, enabling --sanitize=thread"
    ${BUILD_TESTING})
include(CheckCXXSourceCompiles)
set(ABSOLUTE_TSAN_FLAGS -fsanitize=thread -fno-omit-frame-pointer)
if(NOT ABSOLUTE_BUILD_RUNTIME_TSAN OR WIN32 OR ANDROID OR
        DEFINED ENV{TERMUX_VERSION} OR CMAKE_CROSSCOMPILING)
    set(ABSOLUTE_RUNTIME_TSAN_AVAILABLE OFF)
else()
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=thread")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=thread")
    check_cxx_source_compiles("int main() { return 0; }"
        ABSOLUTE_TSAN_USABLE)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    set(ABSOLUTE_RUNTIME_TSAN_AVAILABLE ${ABSOLUTE_TSAN_USABLE})
endif()
set(ABSOLUTE_RUNTIME_TSAN_AVAILABLE ${ABSOLUTE_RUNTIME_TSAN_AVAILABLE}
    CACHE INTERNAL "ThreadSanitizer runtime variant can be built")
if(ABSOLUTE_RUNTIME_TSAN_AVAILABLE)
    add_library(Absolute-Runtime-Tsan STATIC ${ABSOLUTE_RUNTIME_SOURCES})
    target_compile_features(Absolute-Runtime-Tsan PUBLIC cxx_std_20)
    target_compile_options(Absolute-Runtime-Tsan PRIVATE ${ABSOLUTE_TSAN_FLAGS})
    target_link_options(Absolute-Runtime-Tsan PUBLIC -fsanitize=thread)
    target_link_libraries(Absolute-Runtime-Tsan PUBLIC
        Threads::Threads ${CMAKE_DL_LIBS})
    set_target_properties(Absolute-Runtime-Tsan PROPERTIES
        POSITION_INDEPENDENT_CODE ON)
endif()

if(ANDROID OR DEFINED ENV{TERMUX_VERSION})
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(ABSOLUTE_UCONTEXT QUIET IMPORTED_TARGET libucontext)
    endif()
    if(TARGET PkgConfig::ABSOLUTE_UCONTEXT)
        target_link_libraries(Absolute-Runtime PUBLIC
            PkgConfig::ABSOLUTE_UCONTEXT)
    else()
        find_path(ABSOLUTE_LIBUCONTEXT_INCLUDE_DIR
            NAMES libucontext/libucontext.h
            HINTS "$ENV{PREFIX}/include")
        find_library(ABSOLUTE_LIBUCONTEXT_LIBRARY
            NAMES ucontext
            HINTS "$ENV{PREFIX}/lib")
        if(NOT ABSOLUTE_LIBUCONTEXT_INCLUDE_DIR OR
                NOT ABSOLUTE_LIBUCONTEXT_LIBRARY)
            message(FATAL_ERROR
                "Termux/Android Scheduler v2 requires libucontext. "
                "Run 'pkg install libucontext' in Termux or "
                "'bash build-termux.sh --bootstrap --clean --test'.")
        endif()
        target_include_directories(Absolute-Runtime PRIVATE
            "${ABSOLUTE_LIBUCONTEXT_INCLUDE_DIR}")
        target_link_libraries(Absolute-Runtime PUBLIC
            "${ABSOLUTE_LIBUCONTEXT_LIBRARY}")
    endif()
endif()
if(WIN32)
    target_link_libraries(Absolute-Runtime PUBLIC ws2_32 shell32 winhttp)
endif()

if(MSVC)
    target_compile_options(Absolute-Runtime PRIVATE /W4 /permissive-)
else()
    target_compile_options(Absolute-Runtime PRIVATE -Wall -Wextra -Wpedantic)
    # Termux/Android bionic has sched_{get,set}affinity(), but not glibc's
    # pthread_{get,set}affinity_np() declarations used by tasks.cpp. Force-include
    # a no-op-on-other-platforms compatibility header for this translation unit.
    set_source_files_properties(src/tasks.cpp PROPERTIES
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/android_pthread_affinity_compat.h")
endif()

if(APPLE)
    # Darwin gates the ucontext fiber routines, and the layout of ucontext_t
    # itself, behind _XOPEN_SOURCE. Feature macros only apply to headers included
    # after them, so this has to reach the compiler before any header rather than
    # from inside scheduler_fiber.h, which is included after the standard library.
    # _DARWIN_C_SOURCE restores the BSD extensions _XOPEN_SOURCE would otherwise
    # hide from the rest of the translation unit.
    set_source_files_properties(src/tasks.cpp PROPERTIES
        COMPILE_DEFINITIONS "_XOPEN_SOURCE=700;_DARWIN_C_SOURCE")
endif()

set_target_properties(Absolute-Runtime PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Optional wasm32 runtime subset (clang --target=wasm32-unknown-unknown).
# LLVM may not be configured yet when this directory is processed, so search the
# portable toolchain and PATH explicitly.
#
# Prefer a host-native clang. The Windows portable SDK under .absolute/toolchains
# is visible from WSL via /mnt/*; never select PE clang.exe when configuring on
# a non-Windows host (that produces "Exec format error" at build time).
set(ABSOLUTE_WASM_RT_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_runtime.c")
set(ABSOLUTE_WASM_SHIM_OBJECT "${CMAKE_BINARY_DIR}/absolute_wasm_runtime.o")
# Leave ABSOLUTE_WASM_CLANG unset until a real path is chosen. An empty normal
# variable would shadow find_program()'s cache result and hide PATH hits.
unset(ABSOLUTE_WASM_CLANG)
unset(ABSOLUTE_WASM_CLANG CACHE)
set(_absolute_wasm_clang_candidates)
if(DEFINED LLVM_TOOLS_BINARY_DIR AND LLVM_TOOLS_BINARY_DIR)
    list(APPEND _absolute_wasm_clang_candidates
        "${LLVM_TOOLS_BINARY_DIR}/clang${CMAKE_EXECUTABLE_SUFFIX}")
endif()
list(APPEND _absolute_wasm_clang_candidates
    "${CMAKE_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin/clang${CMAKE_EXECUTABLE_SUFFIX}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../.absolute/toolchains/llvm-18.1.8/bin/clang${CMAKE_EXECUTABLE_SUFFIX}")
if(WIN32)
    list(APPEND _absolute_wasm_clang_candidates
        "${CMAKE_SOURCE_DIR}/.absolute/toolchains/llvm-18.1.8/bin/clang.exe"
        "${CMAKE_CURRENT_SOURCE_DIR}/../.absolute/toolchains/llvm-18.1.8/bin/clang.exe")
endif()
foreach(_absolute_clang_candidate IN LISTS _absolute_wasm_clang_candidates)
    if(NOT ABSOLUTE_WASM_CLANG AND EXISTS "${_absolute_clang_candidate}")
        set(ABSOLUTE_WASM_CLANG "${_absolute_clang_candidate}")
    endif()
endforeach()
if(NOT ABSOLUTE_WASM_CLANG)
    if(WIN32)
        find_program(ABSOLUTE_WASM_CLANG NAMES clang clang.exe clang-18 clang-19)
    else()
        find_program(ABSOLUTE_WASM_CLANG NAMES clang-18 clang-19 clang)
    endif()
endif()
if(ABSOLUTE_WASM_CLANG)
    set(ABSOLUTE_WASM_RT_FLAGS
        --target=wasm32-unknown-unknown
        -O2
        -std=c11
        -fno-builtin-malloc
        -fno-builtin-calloc
        -fno-builtin-realloc
        -fno-builtin-free
        -fno-builtin-snprintf
    )
    set(ABSOLUTE_WASM_RT_SHARED_FLAGS
        ${ABSOLUTE_WASM_RT_FLAGS}
        -matomics
        -mbulk-memory
        -DABSOLUTE_WASM_SHARED=1
    )
    add_custom_command(
        OUTPUT "${ABSOLUTE_WASM_SHIM_OBJECT}"
        COMMAND "${ABSOLUTE_WASM_CLANG}"
            ${ABSOLUTE_WASM_RT_FLAGS}
            -c "${ABSOLUTE_WASM_RT_SOURCE}"
            -o "${ABSOLUTE_WASM_SHIM_OBJECT}"
        DEPENDS "${ABSOLUTE_WASM_RT_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_platform.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_std.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/real_text.h"
        COMMENT "Building Absolute wasm runtime (host env.absolute_log)"
        VERBATIM
    )
    set(ABSOLUTE_WASM_WASI_OBJECT "${CMAKE_BINARY_DIR}/absolute_wasm_runtime_wasi.o")
    add_custom_command(
        OUTPUT "${ABSOLUTE_WASM_WASI_OBJECT}"
        COMMAND "${ABSOLUTE_WASM_CLANG}"
            ${ABSOLUTE_WASM_RT_FLAGS}
            -DABSOLUTE_WASM_USE_WASI=1
            -c "${ABSOLUTE_WASM_RT_SOURCE}"
            -o "${ABSOLUTE_WASM_WASI_OBJECT}"
        DEPENDS "${ABSOLUTE_WASM_RT_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_platform.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_std.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/real_text.h"
        COMMENT "Building Absolute wasm runtime (WASI fd_write console)"
        VERBATIM
    )
    set(ABSOLUTE_WASM_SHARED_OBJECT "${CMAKE_BINARY_DIR}/absolute_wasm_runtime_shared.o")
    add_custom_command(
        OUTPUT "${ABSOLUTE_WASM_SHARED_OBJECT}"
        COMMAND "${ABSOLUTE_WASM_CLANG}"
            ${ABSOLUTE_WASM_RT_SHARED_FLAGS}
            -c "${ABSOLUTE_WASM_RT_SOURCE}"
            -o "${ABSOLUTE_WASM_SHARED_OBJECT}"
        DEPENDS "${ABSOLUTE_WASM_RT_SOURCE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_platform.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/wasm/absolute_wasm_std.c"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/real_text.h"
        COMMENT "Building Absolute wasm runtime (shared-memory + atomics heap lock)"
        VERBATIM
    )
    add_custom_target(Absolute-Runtime-WasmShim ALL
        DEPENDS "${ABSOLUTE_WASM_SHIM_OBJECT}" "${ABSOLUTE_WASM_WASI_OBJECT}"
            "${ABSOLUTE_WASM_SHARED_OBJECT}")
    set(ABSOLUTE_WASM_SHIM_OBJECT "${ABSOLUTE_WASM_SHIM_OBJECT}" CACHE FILEPATH
        "wasm32 Absolute runtime object (host imports)" FORCE)
    set(ABSOLUTE_WASM_WASI_OBJECT "${ABSOLUTE_WASM_WASI_OBJECT}" CACHE FILEPATH
        "wasm32 Absolute runtime object (WASI console)" FORCE)
    set(ABSOLUTE_WASM_SHARED_OBJECT "${ABSOLUTE_WASM_SHARED_OBJECT}" CACHE FILEPATH
        "wasm32 Absolute runtime object (shared memory)" FORCE)
    message(STATUS "Absolute wasm runtime: ${ABSOLUTE_WASM_SHIM_OBJECT}")
    message(STATUS "Absolute wasm WASI runtime: ${ABSOLUTE_WASM_WASI_OBJECT}")
    message(STATUS "Absolute wasm shared runtime: ${ABSOLUTE_WASM_SHARED_OBJECT}")
else()
    set(ABSOLUTE_WASM_SHIM_OBJECT "" CACHE FILEPATH
        "wasm32 Absolute runtime object (host imports)" FORCE)
    set(ABSOLUTE_WASM_WASI_OBJECT "" CACHE FILEPATH
        "wasm32 Absolute runtime object (WASI console)" FORCE)
    set(ABSOLUTE_WASM_SHARED_OBJECT "" CACHE FILEPATH
        "wasm32 Absolute runtime object (shared memory)" FORCE)
    message(STATUS "clang for wasm32 not found; Absolute wasm runtime will not be built")
endif()
