#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
#if defined(_WIN32)
    using LibraryHandle = HMODULE;
#else
    using LibraryHandle = void*;
#endif

    std::mutex libraryMutex;
    std::unordered_map<std::string, LibraryHandle> loadedLibraries;

#if defined(_WIN32)
    std::wstring Utf8ToWide(const char* value) {
        if (!value || !*value) return {};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value, -1, nullptr, 0);
        if (length <= 1) return {};
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value, -1, result.data(), length) == 0)
            return {};
        result.pop_back();
        return result;
    }
#endif

    LibraryHandle OpenLibrary(const char* path) {
#if defined(_WIN32)
        const std::wstring widePath = Utf8ToWide(path);
        return widePath.empty() ? nullptr : LoadLibraryW(widePath.c_str());
#else
        return dlopen(path, RTLD_NOW | RTLD_GLOBAL);
#endif
    }

    void CloseLibrary(LibraryHandle handle) {
#if defined(_WIN32)
        if (handle) FreeLibrary(handle);
#else
        if (handle) dlclose(handle);
#endif
    }
}

extern "C" std::int32_t absolute_load_library(const char* path) {
    if (!path || !*path) return 0;
    const std::string key(path);
    {
        const std::lock_guard lock(libraryMutex);
        if (loadedLibraries.contains(key)) return 1;
    }

    LibraryHandle handle = OpenLibrary(path);
    if (!handle) return 0;

    const std::lock_guard lock(libraryMutex);
    const auto insertion = loadedLibraries.emplace(key, handle);
    if (!insertion.second) CloseLibrary(handle);
    return 1;
}
