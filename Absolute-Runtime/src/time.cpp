#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <thread>

namespace {
    using Nanoseconds = std::chrono::nanoseconds;
    using Microseconds = std::chrono::microseconds;

    int64_t UnixNanosecondsNow() {
        return static_cast<int64_t>(std::chrono::duration_cast<Nanoseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    }

    void FormatLocalTime(char* buffer, int32_t bufferSize, const char* format) {
        if (!buffer || bufferSize <= 0) return;
        buffer[0] = '\0';

        const std::time_t value = std::chrono::system_clock::to_time_t(
            std::chrono::system_clock::now());
        std::tm local{};
#if defined(_WIN32)
        if (localtime_s(&local, &value) != 0) return;
#else
        if (!localtime_r(&value, &local)) return;
#endif
        if (std::strftime(buffer, static_cast<std::size_t>(bufferSize), format, &local) == 0)
            buffer[0] = '\0';
    }
}

extern "C" int64_t absolute_time_unix_nanos() {
    return UnixNanosecondsNow();
}

extern "C" int64_t absolute_time_unix_millis() {
    return UnixNanosecondsNow() / 1'000'000;
}

extern "C" int64_t absolute_time_unix_seconds() {
    return UnixNanosecondsNow() / 1'000'000'000;
}

extern "C" int64_t absolute_time_monotonic_nanos() {
    return static_cast<int64_t>(std::chrono::duration_cast<Nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

extern "C" void absolute_time_sleep_nanos(int64_t nanoseconds) {
    if (nanoseconds <= 0) return;
    std::this_thread::sleep_for(Nanoseconds(nanoseconds));
}

extern "C" void absolute_time_sleep_micros(int64_t microseconds) {
    if (microseconds <= 0) return;
    std::this_thread::sleep_for(Microseconds(microseconds));
}

extern "C" void absolute_time_sleep_millis(int64_t milliseconds) {
    if (milliseconds <= 0) return;
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

extern "C" void absolute_time_sleep_seconds(int64_t seconds) {
    if (seconds <= 0) return;
    std::this_thread::sleep_for(std::chrono::seconds(seconds));
}

// Kept for native ABI compatibility with callers compiled against the old
// runtime. The standard library now uses the int64 duration entry points.
extern "C" void absolute_time_sleep(int32_t milliseconds) {
    absolute_time_sleep_millis(milliseconds);
}

extern "C" void absolute_time_local_str(char* buffer, int32_t bufferSize) {
    FormatLocalTime(buffer, bufferSize, "%Y-%m-%d %H:%M:%S");
}

extern "C" void absolute_time_date_str(char* buffer, int32_t bufferSize) {
    FormatLocalTime(buffer, bufferSize, "%Y-%m-%d");
}
