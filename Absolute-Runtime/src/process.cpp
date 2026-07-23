#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#else
#include <unistd.h>
#include <sys/types.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

namespace {
    thread_local std::string lastProcessError;
    thread_local std::string lastProcessResult;

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

    std::vector<std::string> FetchCachedArgs() {
        std::vector<std::string> args;
#if defined(_WIN32)
        int argc = 0;
        LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (argvW) {
            for (int i = 0; i < argc; ++i) {
                args.push_back(WideToUtf8(argvW[i]));
            }
            LocalFree(argvW);
        }
#else
        FILE* f = std::fopen("/proc/self/cmdline", "rb");
        if (f) {
            char buffer[4096];
            size_t bytesRead = std::fread(buffer, 1, sizeof(buffer), f);
            std::fclose(f);
            size_t start = 0;
            for (size_t i = 0; i < bytesRead; ++i) {
                if (buffer[i] == '\0') {
                    if (i > start) {
                        args.push_back(std::string(&buffer[start], i - start));
                    }
                    start = i + 1;
                }
            }
        }
#endif
        return args;
    }
}

extern "C" const char* absolute_process_error() {
    return lastProcessError.c_str();
}

extern "C" int32_t absolute_process_pid() {
#if defined(_WIN32)
    return static_cast<int32_t>(GetCurrentProcessId());
#else
    return static_cast<int32_t>(getpid());
#endif
}

extern "C" void absolute_process_exit(int32_t code) {
    std::exit(code);
}

extern "C" void absolute_process_abort() {
    std::abort();
}

extern "C" const char* absolute_process_executable_path() {
    lastProcessError.clear();
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) {
        lastProcessError = "GetModuleFileNameW failed";
        lastProcessResult.clear();
        return lastProcessResult.c_str();
    }
    lastProcessResult = WideToUtf8(std::wstring(buf, len));
    return lastProcessResult.c_str();
#elif defined(__APPLE__)
    char buf[1024];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0) {
        lastProcessResult = buf;
    } else {
        lastProcessError = "_NSGetExecutablePath failed";
        lastProcessResult.clear();
    }
    return lastProcessResult.c_str();
#else
    std::error_code ec;
    auto path = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        lastProcessError = ec.message();
        lastProcessResult.clear();
        return lastProcessResult.c_str();
    }
    lastProcessResult = path.string();
    return lastProcessResult.c_str();
#endif
}

extern "C" const char* absolute_process_hostname() {
    lastProcessError.clear();
#if defined(_WIN32)
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1];
    DWORD size = sizeof(buf) / sizeof(buf[0]);
    if (GetComputerNameW(buf, &size)) {
        lastProcessResult = WideToUtf8(std::wstring(buf, size));
    } else {
        lastProcessError = "GetComputerNameW failed";
        lastProcessResult.clear();
    }
    return lastProcessResult.c_str();
#else
    char buf[256];
    if (gethostname(buf, sizeof(buf)) == 0) {
        buf[sizeof(buf) - 1] = '\0';
        lastProcessResult = buf;
    } else {
        lastProcessError = strerror(errno);
        lastProcessResult.clear();
    }
    return lastProcessResult.c_str();
#endif
}

extern "C" int32_t absolute_process_run(const char* command) {
    if (!command) return -1;
    return std::system(command);
}

extern "C" const char* absolute_process_run_capture(const char* command) {
    lastProcessError.clear();
    lastProcessResult.clear();
    if (!command || !*command) return lastProcessResult.c_str();

#if defined(_WIN32)
    const std::wstring wCmd = Utf8ToWide(command);
    FILE* pipe = _wpopen(wCmd.c_str(), L"r");
#else
    FILE* pipe = popen(command, "r");
#endif
    if (!pipe) {
        lastProcessError = "Failed to open process pipe";
        return lastProcessResult.c_str();
    }
    char buffer[256];
    while (std::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        lastProcessResult += buffer;
    }
#if defined(_WIN32)
    _pclose(pipe);
#else
    pclose(pipe);
#endif
    return lastProcessResult.c_str();
}

extern "C" int32_t absolute_process_args_count() {
    static const auto args = FetchCachedArgs();
    return static_cast<int32_t>(args.size());
}

extern "C" const char* absolute_process_arg_at(int32_t index) {
    static const auto args = FetchCachedArgs();
    if (index < 0 || static_cast<size_t>(index) >= args.size()) {
        lastProcessResult.clear();
        return lastProcessResult.c_str();
    }
    lastProcessResult = args[static_cast<size_t>(index)];
    return lastProcessResult.c_str();
}

extern "C" int32_t absolute_process_set_cwd(const char* path) {
    lastProcessError.clear();
    if (!path || !*path) return 0;
    std::error_code ec;
#if defined(_WIN32)
    const std::wstring wPath = Utf8ToWide(path);
    std::filesystem::current_path(std::filesystem::path(wPath), ec);
#else
    std::filesystem::current_path(std::filesystem::path(path), ec);
#endif
    if (ec) {
        lastProcessError = ec.message();
        return 0;
    }
    return 1;
}
