#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
    thread_local std::string lastEnvError;
    thread_local std::string lastEnvResult;

#if defined(_WIN32)
    std::wstring Utf8ToWide(const char* value) {
        if (!value) return {};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, nullptr, 0);
        if (length <= 0) return {};
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, result.data(), length) == 0)
            return {};
        result.pop_back();
        return result;
    }

    std::string WideToUtf8(const std::wstring& value) {
        if (value.empty()) return {};
        const int length = WideCharToMultiByte(CP_UTF8, 0, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (length <= 0) return {};
        std::string result(static_cast<std::size_t>(length), '\0');
        if (WideCharToMultiByte(CP_UTF8, 0, value.data(),
            static_cast<int>(value.size()), result.data(), length, nullptr, nullptr) == 0)
            return {};
        return result;
    }
#endif
}

extern "C" const char* absolute_env_error() {
    return lastEnvError.c_str();
}

extern "C" int32_t absolute_env_has(const char* name) {
    if (!name || !*name) return 0;
#if defined(_WIN32)
    const std::wstring wName = Utf8ToWide(name);
    DWORD res = GetEnvironmentVariableW(wName.c_str(), nullptr, 0);
    return (res > 0 || GetLastError() != ERROR_ENVVAR_NOT_FOUND) ? 1 : 0;
#else
    return getenv(name) != nullptr ? 1 : 0;
#endif
}

extern "C" const char* absolute_env_get(const char* name) {
    lastEnvError.clear();
    if (!name || !*name) {
        lastEnvResult.clear();
        return lastEnvResult.c_str();
    }
#if defined(_WIN32)
    const std::wstring wName = Utf8ToWide(name);
    DWORD required = GetEnvironmentVariableW(wName.c_str(), nullptr, 0);
    if (required == 0) {
        if (GetLastError() == ERROR_ENVVAR_NOT_FOUND) {
            lastEnvError = "Environment variable not found";
        } else {
            lastEnvError = "Failed to query environment variable";
        }
        lastEnvResult.clear();
        return lastEnvResult.c_str();
    }
    std::wstring buffer(required, L'\0');
    GetEnvironmentVariableW(wName.c_str(), buffer.data(), required);
    if (!buffer.empty() && buffer.back() == L'\0') buffer.pop_back();
    lastEnvResult = WideToUtf8(buffer);
    return lastEnvResult.c_str();
#else
    const char* val = getenv(name);
    if (!val) {
        lastEnvError = "Environment variable not found";
        lastEnvResult.clear();
        return lastEnvResult.c_str();
    }
    lastEnvResult = val;
    return lastEnvResult.c_str();
#endif
}

extern "C" int32_t absolute_env_set(const char* name, const char* value) {
    lastEnvError.clear();
    if (!name || !*name) {
        lastEnvError = "Invalid environment variable name";
        return 0;
    }
    const char* val = value ? value : "";
#if defined(_WIN32)
    const std::wstring wName = Utf8ToWide(name);
    const std::wstring wVal = Utf8ToWide(val);
    if (!SetEnvironmentVariableW(wName.c_str(), wVal.c_str())) {
        lastEnvError = "SetEnvironmentVariableW failed";
        return 0;
    }
    return 1;
#else
    if (setenv(name, val, 1) != 0) {
        lastEnvError = strerror(errno);
        return 0;
    }
    return 1;
#endif
}

extern "C" int32_t absolute_env_remove(const char* name) {
    lastEnvError.clear();
    if (!name || !*name) {
        lastEnvError = "Invalid environment variable name";
        return 0;
    }
#if defined(_WIN32)
    const std::wstring wName = Utf8ToWide(name);
    if (!SetEnvironmentVariableW(wName.c_str(), nullptr)) {
        if (GetLastError() != ERROR_ENVVAR_NOT_FOUND) {
            lastEnvError = "SetEnvironmentVariableW removal failed";
            return 0;
        }
    }
    return 1;
#else
    if (unsetenv(name) != 0) {
        lastEnvError = strerror(errno);
        return 0;
    }
    return 1;
#endif
}
