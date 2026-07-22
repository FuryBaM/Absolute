#include <chrono>
#include <cstdint>
#include <ctime>
#include <cstring>
#include <thread>

// Returns Unix timestamp in seconds (seconds since 1970-01-01 00:00:00 UTC).
extern "C" int64_t absolute_time_unix_seconds() {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<seconds>(system_clock::now().time_since_epoch()).count());
}

// Returns Unix timestamp in milliseconds.
extern "C" int64_t absolute_time_unix_millis() {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

// Returns a high-resolution monotonic clock in nanoseconds (for benchmarking).
extern "C" int64_t absolute_time_monotonic_nanos() {
    using namespace std::chrono;
    return static_cast<int64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

// Writes the current local date/time as "YYYY-MM-DD HH:MM:SS" into buf (must be at least 20 bytes).
extern "C" void absolute_time_local_str(char* buf, int32_t bufSize) {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);
    struct tm localTime;
#if defined(_WIN32)
    localtime_s(&localTime, &tt);
#else
    localtime_r(&tt, &localTime);
#endif
    if (bufSize >= 20) {
        // YYYY-MM-DD HH:MM:SS
        buf[0]  = '0' + (localTime.tm_year + 1900) / 1000;
        buf[1]  = '0' + (localTime.tm_year + 1900) / 100 % 10;
        buf[2]  = '0' + (localTime.tm_year + 1900) / 10 % 10;
        buf[3]  = '0' + (localTime.tm_year + 1900) % 10;
        buf[4]  = '-';
        buf[5]  = '0' + (localTime.tm_mon + 1) / 10;
        buf[6]  = '0' + (localTime.tm_mon + 1) % 10;
        buf[7]  = '-';
        buf[8]  = '0' + localTime.tm_mday / 10;
        buf[9]  = '0' + localTime.tm_mday % 10;
        buf[10] = ' ';
        buf[11] = '0' + localTime.tm_hour / 10;
        buf[12] = '0' + localTime.tm_hour % 10;
        buf[13] = ':';
        buf[14] = '0' + localTime.tm_min / 10;
        buf[15] = '0' + localTime.tm_min % 10;
        buf[16] = ':';
        buf[17] = '0' + localTime.tm_sec / 10;
        buf[18] = '0' + localTime.tm_sec % 10;
        buf[19] = '\0';
    } else if (bufSize > 0) {
        buf[0] = '\0';
    }
}

// Writes the current local date as "YYYY-MM-DD" into buf (must be at least 11 bytes).
extern "C" void absolute_time_date_str(char* buf, int32_t bufSize) {
    using namespace std::chrono;
    auto now = system_clock::now();
    std::time_t tt = system_clock::to_time_t(now);
    struct tm localTime;
#if defined(_WIN32)
    localtime_s(&localTime, &tt);
#else
    localtime_r(&tt, &localTime);
#endif
    if (bufSize >= 11) {
        buf[0] = '0' + (localTime.tm_year + 1900) / 1000;
        buf[1] = '0' + (localTime.tm_year + 1900) / 100 % 10;
        buf[2] = '0' + (localTime.tm_year + 1900) / 10 % 10;
        buf[3] = '0' + (localTime.tm_year + 1900) % 10;
        buf[4] = '-';
        buf[5] = '0' + (localTime.tm_mon + 1) / 10;
        buf[6] = '0' + (localTime.tm_mon + 1) % 10;
        buf[7] = '-';
        buf[8] = '0' + localTime.tm_mday / 10;
        buf[9] = '0' + localTime.tm_mday % 10;
        buf[10] = '\0';
    } else if (bufSize > 0) {
        buf[0] = '\0';
    }
}

// Sleep for a given number of milliseconds.
extern "C" void absolute_time_sleep(int32_t milliseconds) {
    if (milliseconds <= 0) return;
#if defined(_WIN32)
    // Use chrono-based sleep to avoid including <windows.h>
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
#endif
}
