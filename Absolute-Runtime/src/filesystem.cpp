#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <system_error>
#include <utility>

#include "scheduler_io.h"

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
    struct FileState {
        std::FILE* stream = nullptr;
        std::string readBuffer;
    };

    thread_local std::string lastFileSystemError;
    thread_local std::string lastFileSystemResult;

    template <class Result, class Operation>
    Result RunFileIo(Operation&& operation) {
        Result result{};
        std::string error;
        Absolute::RuntimeDetail::RunBlockingIo([&] {
            result = operation();
            error = lastFileSystemError;
        });
        lastFileSystemError = std::move(error);
        return result;
    }

#if defined(_WIN32)
    std::wstring Utf8ToWide(const char* value) {
        if (!value) return {};
        const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value, -1, nullptr, 0);
        if (length <= 0) return {};
        std::wstring result(static_cast<std::size_t>(length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            value, -1, result.data(), length) == 0)
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
            static_cast<int>(value.size()), result.data(), length,
            nullptr, nullptr) == 0)
            return {};
        return result;
    }
#endif

    std::filesystem::path NativePath(const char* value) {
#if defined(_WIN32)
        return std::filesystem::path(Utf8ToWide(value));
#else
        return std::filesystem::path(value ? value : "");
#endif
    }

    std::string PortablePath(const std::filesystem::path& value) {
#if defined(_WIN32)
        return WideToUtf8(value.wstring());
#else
        return value.string();
#endif
    }

    std::string PreferredPath(std::filesystem::path value) {
        value.make_preferred();
        return PortablePath(value);
    }

    void ClearError() {
        lastFileSystemError.clear();
    }

    void SetError(const std::error_code& error) {
        lastFileSystemError = error ? error.message() : "filesystem operation failed";
    }

    void SetError(const char* operation) {
        const int code = errno;
#if defined(_WIN32)
        char message[256]{};
        strerror_s(message, sizeof(message), code);
        const char* detail = code == 0 ? "operation failed" : message;
#else
        const char* detail = code == 0 ? "operation failed" : std::strerror(code);
#endif
        lastFileSystemError = std::string(operation) + ": " +
            detail;
    }

    std::FILE* OpenFile(const char* path, const char* mode) {
        if (!path || !mode) return nullptr;
#if defined(_WIN32)
        const std::wstring widePath = Utf8ToWide(path);
        const std::wstring wideMode = Utf8ToWide(mode);
        if (widePath.empty() || wideMode.empty()) return nullptr;
        std::FILE* stream = nullptr;
        return _wfopen_s(&stream, widePath.c_str(), wideMode.c_str()) == 0
            ? stream : nullptr;
#else
        return std::fopen(path, mode);
#endif
    }

    FileState* State(void* handle) {
        return static_cast<FileState*>(handle);
    }
}

extern "C" std::int32_t absolute_fs_exists(const char* path) {
    return RunFileIo<std::int32_t>([&] {
        std::error_code error;
        const bool result =
            path && std::filesystem::exists(NativePath(path), error);
        if (error) SetError(error); else ClearError();
        return result ? std::int32_t{1} : std::int32_t{0};
    });
}

extern "C" std::int32_t absolute_fs_is_file(const char* path) {
    return RunFileIo<std::int32_t>([&] {
        std::error_code error;
        const bool result =
            path && std::filesystem::is_regular_file(NativePath(path), error);
        if (error) SetError(error); else ClearError();
        return result ? std::int32_t{1} : std::int32_t{0};
    });
}

extern "C" std::int32_t absolute_fs_is_directory(const char* path) {
    return RunFileIo<std::int32_t>([&] {
        std::error_code error;
        const bool result =
            path && std::filesystem::is_directory(NativePath(path), error);
        if (error) SetError(error); else ClearError();
        return result ? std::int32_t{1} : std::int32_t{0};
    });
}

extern "C" std::int64_t absolute_fs_file_size(const char* path) {
    return RunFileIo<std::int64_t>([&] {
        std::error_code error;
        const auto result =
            path ? std::filesystem::file_size(NativePath(path), error) : 0;
        if (error || !path) {
            if (error) SetError(error);
            else lastFileSystemError = "file path is null";
            return std::int64_t{-1};
        }
        ClearError();
        return result > static_cast<std::uintmax_t>(INT64_MAX)
            ? std::int64_t{-1} : static_cast<std::int64_t>(result);
    });
}

extern "C" std::int32_t absolute_fs_create_directories(const char* path) {
    return RunFileIo<std::int32_t>([&] {
        if (!path || !*path) {
            lastFileSystemError = "directory path is empty";
            return std::int32_t{0};
        }
        std::error_code error;
        const std::filesystem::path native = NativePath(path);
        const bool created = std::filesystem::create_directories(native, error);
        const bool exists = !error &&
            (created || std::filesystem::is_directory(native, error));
        if (error) SetError(error); else ClearError();
        return exists ? std::int32_t{1} : std::int32_t{0};
    });
}

extern "C" std::int32_t absolute_fs_remove(const char* path) {
    return RunFileIo<std::int32_t>([&] {
        if (!path || !*path) {
            lastFileSystemError = "path is empty";
            return std::int32_t{0};
        }
        std::error_code error;
        const bool removed = std::filesystem::remove(NativePath(path), error);
        if (error) SetError(error); else ClearError();
        return removed ? std::int32_t{1} : std::int32_t{0};
    });
}

extern "C" std::int32_t absolute_fs_rename(const char* source, const char* destination) {
    return RunFileIo<std::int32_t>([&] {
        if (!source || !destination) {
            lastFileSystemError = "rename path is null";
            return std::int32_t{0};
        }
        std::error_code error;
        std::filesystem::rename(
            NativePath(source), NativePath(destination), error);
        if (error) {
            SetError(error);
            return std::int32_t{0};
        }
        ClearError();
        return std::int32_t{1};
    });
}

extern "C" std::int32_t absolute_fs_copy_file(
    const char* source, const char* destination, std::int32_t overwrite) {
    return RunFileIo<std::int32_t>([&] {
        if (!source || !destination) {
            lastFileSystemError = "copy path is null";
            return std::int32_t{0};
        }
        std::error_code error;
        const auto options = overwrite
            ? std::filesystem::copy_options::overwrite_existing
            : std::filesystem::copy_options::none;
        const bool copied = std::filesystem::copy_file(
            NativePath(source), NativePath(destination), options, error);
        if (error) SetError(error); else ClearError();
        return copied ? std::int32_t{1} : std::int32_t{0};
    });
}

extern "C" const char* absolute_fs_current_directory() {
    std::string result;
    bool success = false;
    std::string errorText;
    Absolute::RuntimeDetail::RunBlockingIo([&] {
        std::error_code error;
        const std::filesystem::path current =
            std::filesystem::current_path(error);
        if (error) {
            SetError(error);
        }
        else {
            result = PortablePath(current);
            ClearError();
            success = true;
        }
        errorText = lastFileSystemError;
    });
    lastFileSystemError = std::move(errorText);
    if (!success) return nullptr;
    lastFileSystemResult = std::move(result);
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_absolute(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    std::string result;
    bool success = false;
    std::string errorText;
    Absolute::RuntimeDetail::RunBlockingIo([&] {
        std::error_code error;
        const std::filesystem::path absolute =
            std::filesystem::absolute(NativePath(path), error);
        if (error) {
            SetError(error);
        }
        else {
            result = PortablePath(absolute.lexically_normal());
            ClearError();
            success = true;
        }
        errorText = lastFileSystemError;
    });
    lastFileSystemError = std::move(errorText);
    if (!success) return nullptr;
    lastFileSystemResult = std::move(result);
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_separator() {
#if defined(_WIN32)
    return "\\";
#else
    return "/";
#endif
}

extern "C" const char* absolute_fs_path_join(const char* left, const char* right) {
    const std::filesystem::path lhs = NativePath(left ? left : "");
    const std::filesystem::path rhs = NativePath(right ? right : "");
    const std::filesystem::path combined = lhs.empty()
        ? rhs
        : (rhs.empty() ? lhs : lhs / rhs);
    lastFileSystemResult = PreferredPath(combined.lexically_normal());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_normalize(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PreferredPath(NativePath(path).lexically_normal());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_parent(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PreferredPath(
        NativePath(path).lexically_normal().parent_path());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_filename(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PortablePath(
        NativePath(path).lexically_normal().filename());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_stem(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PortablePath(
        NativePath(path).lexically_normal().stem());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_extension(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PortablePath(
        NativePath(path).lexically_normal().extension());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" std::int32_t absolute_fs_path_is_absolute(const char* path) {
    ClearError();
    return path && NativePath(path).is_absolute() ? 1 : 0;
}

extern "C" const char* absolute_fs_read_text(const char* path) {
    if (!path) {
        lastFileSystemError = "file path is null";
        return nullptr;
    }
    std::string result;
    bool success = false;
    std::string errorText;
    Absolute::RuntimeDetail::RunBlockingIo([&] {
        std::ifstream input(NativePath(path), std::ios::binary);
        if (!input) {
            SetError("read file");
        }
        else {
            result.assign(std::istreambuf_iterator<char>(input),
                std::istreambuf_iterator<char>());
            if (input.bad()) {
                SetError("read file");
            }
            else {
                ClearError();
                success = true;
            }
        }
        errorText = lastFileSystemError;
    });
    lastFileSystemError = std::move(errorText);
    if (!success) return nullptr;
    lastFileSystemResult = std::move(result);
    return lastFileSystemResult.c_str();
}

extern "C" std::int32_t absolute_fs_write_text(
    const char* path, const char* text, std::int32_t append) {
    return RunFileIo<std::int32_t>([&] {
        if (!path || !text) {
            lastFileSystemError = "write path or text is null";
            return std::int32_t{0};
        }
        std::ofstream output(NativePath(path), std::ios::binary |
            (append ? std::ios::app : std::ios::trunc));
        if (!output) {
            SetError("open file for writing");
            return std::int32_t{0};
        }
        output.write(text, static_cast<std::streamsize>(std::strlen(text)));
        output.flush();
        if (!output) {
            SetError("write file");
            return std::int32_t{0};
        }
        ClearError();
        return std::int32_t{1};
    });
}

extern "C" void* absolute_fs_file_open(const char* path, const char* mode) {
    return RunFileIo<void*>([&]() -> void* {
        std::FILE* stream = OpenFile(path, mode);
        if (!stream) {
            SetError("open file");
            return nullptr;
        }
        FileState* state = new (std::nothrow) FileState;
        if (!state) {
            std::fclose(stream);
            lastFileSystemError = "file handle allocation failed";
            return nullptr;
        }
        state->stream = stream;
        ClearError();
        return state;
    });
}

extern "C" void absolute_fs_file_close(void* handle) {
    FileState* state = State(handle);
    if (!state) return;
    Absolute::RuntimeDetail::RunBlockingIo([&] {
        if (state->stream) std::fclose(state->stream);
        delete state;
    });
}

extern "C" const char* absolute_fs_file_read_line(void* handle) {
    return RunFileIo<const char*>([&]() -> const char* {
        FileState* state = State(handle);
        if (!state || !state->stream) {
            lastFileSystemError = "file is closed";
            return nullptr;
        }
        state->readBuffer.clear();
        for (;;) {
            const int value = std::fgetc(state->stream);
            if (value == EOF || value == '\n') break;
            if (value != '\r')
                state->readBuffer.push_back(static_cast<char>(value));
        }
        if (std::ferror(state->stream)) {
            SetError("read line");
            return nullptr;
        }
        ClearError();
        return state->readBuffer.c_str();
    });
}

extern "C" const char* absolute_fs_file_read_all(void* handle) {
    return RunFileIo<const char*>([&]() -> const char* {
        FileState* state = State(handle);
        if (!state || !state->stream) {
            lastFileSystemError = "file is closed";
            return nullptr;
        }
        state->readBuffer.clear();
        char buffer[4096];
        for (;;) {
            const std::size_t count =
                std::fread(buffer, 1, sizeof(buffer), state->stream);
            state->readBuffer.append(buffer, count);
            if (count != sizeof(buffer)) break;
        }
        if (std::ferror(state->stream)) {
            SetError("read file");
            return nullptr;
        }
        ClearError();
        return state->readBuffer.c_str();
    });
}

extern "C" std::int32_t absolute_fs_file_write(void* handle, const char* text) {
    return RunFileIo<std::int32_t>([&] {
        FileState* state = State(handle);
        if (!state || !state->stream || !text) {
            lastFileSystemError = "file is closed or text is null";
            return std::int32_t{0};
        }
        const std::size_t length = std::strlen(text);
        if (std::fwrite(text, 1, length, state->stream) != length) {
            SetError("write file");
            return std::int32_t{0};
        }
        ClearError();
        return std::int32_t{1};
    });
}

extern "C" std::int32_t absolute_fs_file_flush(void* handle) {
    return RunFileIo<std::int32_t>([&] {
        FileState* state = State(handle);
        if (!state || !state->stream || std::fflush(state->stream) != 0) {
            SetError("flush file");
            return std::int32_t{0};
        }
        ClearError();
        return std::int32_t{1};
    });
}

extern "C" std::int32_t absolute_fs_file_eof(void* handle) {
    FileState* state = State(handle);
    return !state || !state->stream || std::feof(state->stream) ? 1 : 0;
}

extern "C" const char* absolute_fs_error() {
    return lastFileSystemError.c_str();
}
