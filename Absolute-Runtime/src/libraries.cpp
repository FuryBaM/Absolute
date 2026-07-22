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
    thread_local std::string lastLibraryError;

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

    std::string WideToUtf8(const wchar_t* value, int length) {
        if (!value || length <= 0) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, value, length,
            nullptr, 0, nullptr, nullptr);
        if (size <= 0) return {};
        std::string result(static_cast<std::size_t>(size), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, value, length,
            result.data(), size, nullptr, nullptr) == 0)
            return {};
        while (!result.empty() && (result.back() == '\r' || result.back() == '\n'))
            result.pop_back();
        return result;
    }

    void CaptureLoadError() {
        const DWORD code = GetLastError();
        wchar_t* message = nullptr;
        const DWORD length = FormatMessageW(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                FORMAT_MESSAGE_IGNORE_INSERTS,
            nullptr, code, 0, reinterpret_cast<wchar_t*>(&message), 0, nullptr);
        lastLibraryError = WideToUtf8(message, static_cast<int>(length));
        if (message) LocalFree(message);
        if (lastLibraryError.empty())
            lastLibraryError = "Windows loader error " + std::to_string(code);
    }
#else
    void CaptureLoadError() {
        const char* message = dlerror();
        lastLibraryError = message ? message : "dynamic loader returned no error text";
    }
#endif

    LibraryHandle OpenLibrary(const char* path) {
#if defined(_WIN32)
        const std::wstring widePath = Utf8ToWide(path);
        return widePath.empty() ? nullptr : LoadLibraryW(widePath.c_str());
#else
        dlerror();
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
    if (!path || !*path) {
        lastLibraryError = "library path is empty";
        return 0;
    }
    const std::string key(path);
    {
        const std::lock_guard lock(libraryMutex);
        if (loadedLibraries.contains(key)) {
            lastLibraryError.clear();
            return 1;
        }
    }

    LibraryHandle handle = OpenLibrary(path);
    if (!handle) {
        CaptureLoadError();
        return 0;
    }

    const std::lock_guard lock(libraryMutex);
    const auto insertion = loadedLibraries.emplace(key, handle);
    if (!insertion.second) CloseLibrary(handle);
    lastLibraryError.clear();
    return 1;
}

extern "C" std::int32_t absolute_library_is_loaded(const char* path) {
    if (!path || !*path) return 0;
    const std::lock_guard lock(libraryMutex);
    return loadedLibraries.contains(path) ? 1 : 0;
}

extern "C" const char* absolute_load_error() {
    return lastLibraryError.c_str();
}
