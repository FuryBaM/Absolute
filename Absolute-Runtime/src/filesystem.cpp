#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

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

    struct DirectoryState {
        std::vector<std::filesystem::path> entries;
        std::size_t index = 0;
        std::string current;
    };

    struct FileSnapshot {
        bool directory = false;
        std::uintmax_t size = 0;
        std::filesystem::file_time_type modified{};

        bool operator==(const FileSnapshot& other) const {
            return directory == other.directory && size == other.size &&
                modified == other.modified;
        }
    };

    struct WatchEvent {
        std::int32_t kind = 0;
        std::string path;
    };

    struct WatchState {
        std::filesystem::path root;
        bool recursive = false;
        std::unordered_map<std::string, FileSnapshot> snapshot;
        std::deque<WatchEvent> pending;
        std::string currentPath;
    };

    thread_local std::string lastFileSystemError;
    thread_local std::string lastFileSystemResult;
    std::atomic<std::uint64_t> temporaryCounter{0};

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

    // lexically_normal() ends "a/b/.." at "a/" and leaves "a/" as itself, so
    // two spellings of one path were two different strings and comparing
    // normalized paths did not work. A trailing separator is not a component:
    // the root keeps its own, nothing else does. This is what the wasm virtual
    // filesystem already did, and what POSIX basename/dirname do.
    std::filesystem::path CanonicalPath(std::filesystem::path value) {
        value = value.lexically_normal();
        std::string text = PortablePath(value);
        const std::string root = PortablePath(value.root_path());
        while (text.size() > root.size() &&
               (text.back() == '/' || text.back() == '\\')) {
            text.pop_back();
        }
        return NativePath(text.c_str());
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

    std::int64_t FileTimeMillis(
        const std::filesystem::file_time_type& value) {
        using namespace std::chrono;
        const auto systemValue = time_point_cast<milliseconds>(
            value - std::filesystem::file_time_type::clock::now() +
            system_clock::now());
        return duration_cast<milliseconds>(
            systemValue.time_since_epoch()).count();
    }

    std::unordered_map<std::string, FileSnapshot> SnapshotDirectory(
        const std::filesystem::path& root, bool recursive,
        std::error_code& error) {
        std::unordered_map<std::string, FileSnapshot> result;
        auto add = [&](const std::filesystem::directory_entry& entry) {
            FileSnapshot snapshot;
            snapshot.directory = entry.is_directory(error);
            if (error) return;
            if (!snapshot.directory) {
                snapshot.size = entry.file_size(error);
                if (error) return;
            }
            snapshot.modified = entry.last_write_time(error);
            if (error) return;
            result.emplace(PortablePath(entry.path()), snapshot);
        };
        if (recursive) {
            for (std::filesystem::recursive_directory_iterator iterator(root, error), end;
                !error && iterator != end; iterator.increment(error))
                add(*iterator);
        }
        else {
            for (std::filesystem::directory_iterator iterator(root, error), end;
                !error && iterator != end; iterator.increment(error))
                add(*iterator);
        }
        return result;
    }

    std::filesystem::path UniqueTemporaryPath(
        const char* prefix, const char* suffix, std::error_code& error) {
        const std::filesystem::path root =
            std::filesystem::temp_directory_path(error);
        if (error) return {};
        const std::string safePrefix =
            prefix && *prefix ? prefix : "absolute-";
        for (int attempt = 0; attempt < 128; ++attempt) {
            const auto tick = std::chrono::steady_clock::now()
                .time_since_epoch().count();
            const auto id = temporaryCounter.fetch_add(
                1, std::memory_order_relaxed);
            std::filesystem::path candidate = root /
                NativePath((safePrefix + std::to_string(tick) + "-" +
                    std::to_string(id) + suffix).c_str());
            if (!std::filesystem::exists(candidate, error)) return candidate;
            if (error) return {};
        }
        error = std::make_error_code(std::errc::file_exists);
        return {};
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
    lastFileSystemResult = PreferredPath(CanonicalPath(combined));
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_normalize(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PreferredPath(CanonicalPath(NativePath(path)));
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_parent(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PreferredPath(
        CanonicalPath(NativePath(path)).parent_path());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_filename(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PortablePath(
        CanonicalPath(NativePath(path)).filename());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_stem(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PortablePath(
        CanonicalPath(NativePath(path)).stem());
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_path_extension(const char* path) {
    if (!path) {
        lastFileSystemError = "path is null";
        return nullptr;
    }
    lastFileSystemResult = PortablePath(
        CanonicalPath(NativePath(path)).extension());
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
        std::error_code directoryCheck;
        // A directory opens as a stream and then fails on the first read, and
        // libstdc++ reports that failure by throwing std::ios_base::failure
        // out of the iterator -- past this function, past the generated code,
        // and into a std::terminate. Named here instead, so reading a
        // directory is an ordinary filesystem error like the others.
        if (std::filesystem::is_directory(NativePath(path), directoryCheck)) {
            lastFileSystemError = "read file: path is a directory";
        }
        else {
            std::ifstream input(NativePath(path), std::ios::binary);
            if (!input) {
                SetError("read file");
            }
            else {
                try {
                    result.assign(std::istreambuf_iterator<char>(input),
                        std::istreambuf_iterator<char>());
                    if (input.bad()) SetError("read file");
                    else {
                        ClearError();
                        success = true;
                    }
                }
                catch (const std::exception& failure) {
                    // Whatever the stream decided to throw stays inside the
                    // runtime; the caller gets a message, not a terminate.
                    lastFileSystemError = std::string("read file: ") + failure.what();
                }
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

extern "C" std::int32_t absolute_fs_metadata_type(const char* path) {
    return RunFileIo<std::int32_t>([&] {
        if (!path) {
            lastFileSystemError = "metadata path is null";
            return std::int32_t{-1};
        }
        std::error_code error;
        const auto status = std::filesystem::symlink_status(
            NativePath(path), error);
        if (error) {
            SetError(error);
            return std::int32_t{-1};
        }
        ClearError();
        if (!std::filesystem::exists(status)) return std::int32_t{0};
        if (std::filesystem::is_regular_file(status)) return std::int32_t{1};
        if (std::filesystem::is_directory(status)) return std::int32_t{2};
        if (std::filesystem::is_symlink(status)) return std::int32_t{3};
        return std::int32_t{4};
    });
}

extern "C" std::int64_t absolute_fs_metadata_modified_millis(const char* path) {
    return RunFileIo<std::int64_t>([&] {
        if (!path) {
            lastFileSystemError = "metadata path is null";
            return std::int64_t{-1};
        }
        std::error_code error;
        const auto value = std::filesystem::last_write_time(
            NativePath(path), error);
        if (error) {
            SetError(error);
            return std::int64_t{-1};
        }
        ClearError();
        return FileTimeMillis(value);
    });
}

extern "C" std::int32_t absolute_fs_metadata_read_only(const char* path) {
    return RunFileIo<std::int32_t>([&] {
        if (!path) {
            lastFileSystemError = "metadata path is null";
            return std::int32_t{-1};
        }
        std::error_code error;
        const auto permissions = std::filesystem::status(
            NativePath(path), error).permissions();
        if (error) {
            SetError(error);
            return std::int32_t{-1};
        }
        const auto writable = std::filesystem::perms::owner_write |
            std::filesystem::perms::group_write |
            std::filesystem::perms::others_write;
        ClearError();
        return (permissions & writable) == std::filesystem::perms::none ? 1 : 0;
    });
}

extern "C" void* absolute_fs_directory_open(
    const char* path, std::int32_t recursive) {
    return RunFileIo<void*>([&]() -> void* {
        if (!path) {
            lastFileSystemError = "directory path is null";
            return nullptr;
        }
        std::error_code error;
        auto state = new (std::nothrow) DirectoryState;
        if (!state) {
            lastFileSystemError = "directory iterator allocation failed";
            return nullptr;
        }
        if (recursive) {
            for (std::filesystem::recursive_directory_iterator iterator(
                    NativePath(path), error), end;
                !error && iterator != end; iterator.increment(error))
                state->entries.push_back(iterator->path());
        }
        else {
            for (std::filesystem::directory_iterator iterator(
                    NativePath(path), error), end;
                !error && iterator != end; iterator.increment(error))
                state->entries.push_back(iterator->path());
        }
        if (error) {
            SetError(error);
            delete state;
            return nullptr;
        }
        std::sort(state->entries.begin(), state->entries.end(),
            [](const auto& left, const auto& right) {
                return PortablePath(left) < PortablePath(right);
            });
        ClearError();
        return state;
    });
}

extern "C" const char* absolute_fs_directory_next(void* handle) {
    DirectoryState* state = static_cast<DirectoryState*>(handle);
    if (!state) {
        lastFileSystemError = "directory iterator is closed";
        return nullptr;
    }
    if (state->index >= state->entries.size()) {
        ClearError();
        return "";
    }
    state->current = PortablePath(state->entries[state->index++]);
    ClearError();
    return state->current.c_str();
}

extern "C" void absolute_fs_directory_close(void* handle) {
    delete static_cast<DirectoryState*>(handle);
}

extern "C" const char* absolute_fs_temp_directory() {
    std::error_code error;
    auto path = std::filesystem::temp_directory_path(error);
    if (error) {
        SetError(error);
        return nullptr;
    }
    if (path.filename().empty() && path.has_relative_path())
        path = path.parent_path();
    lastFileSystemResult = PortablePath(path);
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_create_temp_file(const char* prefix) {
    std::error_code error;
    const auto path = UniqueTemporaryPath(prefix, ".tmp", error);
    if (!error) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output) error = std::make_error_code(std::errc::io_error);
    }
    if (error) {
        SetError(error);
        return nullptr;
    }
    lastFileSystemResult = PortablePath(path);
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" const char* absolute_fs_create_temp_directory(const char* prefix) {
    std::error_code error;
    const auto path = UniqueTemporaryPath(prefix, "", error);
    if (!error && !std::filesystem::create_directory(path, error) && !error)
        error = std::make_error_code(std::errc::file_exists);
    if (error) {
        SetError(error);
        return nullptr;
    }
    lastFileSystemResult = PortablePath(path);
    ClearError();
    return lastFileSystemResult.c_str();
}

extern "C" void* absolute_fs_watcher_open(
    const char* path, std::int32_t recursive) {
    return RunFileIo<void*>([&]() -> void* {
        if (!path) {
            lastFileSystemError = "watch path is null";
            return nullptr;
        }
        auto state = new (std::nothrow) WatchState;
        if (!state) {
            lastFileSystemError = "watcher allocation failed";
            return nullptr;
        }
        state->root = NativePath(path);
        state->recursive = recursive != 0;
        std::error_code error;
        state->snapshot = SnapshotDirectory(
            state->root, state->recursive, error);
        if (error) {
            SetError(error);
            delete state;
            return nullptr;
        }
        ClearError();
        return state;
    });
}

extern "C" std::int32_t absolute_fs_watcher_poll(void* handle) {
    return RunFileIo<std::int32_t>([&] {
        WatchState* state = static_cast<WatchState*>(handle);
        if (!state) {
            lastFileSystemError = "watcher is closed";
            return std::int32_t{-1};
        }
        if (state->pending.empty()) {
            std::error_code error;
            auto current = SnapshotDirectory(
                state->root, state->recursive, error);
            if (error) {
                SetError(error);
                return std::int32_t{-1};
            }
            std::vector<WatchEvent> events;
            for (const auto& [path, previous] : state->snapshot) {
                const auto found = current.find(path);
                if (found == current.end()) events.push_back({3, path});
                else if (!(found->second == previous))
                    events.push_back({2, path});
            }
            for (const auto& [path, value] : current) {
                if (!state->snapshot.contains(path))
                    events.push_back({1, path});
            }
            std::sort(events.begin(), events.end(),
                [](const WatchEvent& left, const WatchEvent& right) {
                    if (left.path != right.path) return left.path < right.path;
                    return left.kind < right.kind;
                });
            for (auto& event : events)
                state->pending.push_back(std::move(event));
            state->snapshot = std::move(current);
        }
        if (state->pending.empty()) {
            ClearError();
            return std::int32_t{0};
        }
        WatchEvent event = std::move(state->pending.front());
        state->pending.pop_front();
        state->currentPath = std::move(event.path);
        ClearError();
        return event.kind;
    });
}

extern "C" const char* absolute_fs_watcher_path(void* handle) {
    WatchState* state = static_cast<WatchState*>(handle);
    if (!state) {
        lastFileSystemError = "watcher is closed";
        return nullptr;
    }
    ClearError();
    return state->currentPath.c_str();
}

extern "C" void absolute_fs_watcher_close(void* handle) {
    delete static_cast<WatchState*>(handle);
}

extern "C" const char* absolute_fs_error() {
    return lastFileSystemError.c_str();
}
