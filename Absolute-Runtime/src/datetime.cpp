#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <time.h>
#endif

namespace {
    thread_local std::string lastDateTimeError;
    thread_local std::string lastDateTimeResult;

    // Howard Hinnant's civil calendar algorithm (days since 1970-01-01 <-> year/month/day)
    void CivilFromDays(int64_t days, int32_t& year, int32_t& month, int32_t& day) {
        days += 719468;
        const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
        const uint32_t doe = static_cast<uint32_t>(days - era * 146097);
        const uint32_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t y = static_cast<int64_t>(yoe) + era * 400;
        const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const uint32_t mp = (5 * doy + 2) / 153;
        day = static_cast<int32_t>(doy - (153 * mp + 2) / 5 + 1);
        month = static_cast<int32_t>(mp < 10 ? mp + 3 : mp - 9);
        year = static_cast<int32_t>(y + (month <= 2 ? 1 : 0));
    }

    int64_t DaysFromCivil(int32_t year, int32_t month, int32_t day) {
        if (month <= 2) year -= 1;
        const int64_t era = (year >= 0 ? year : year - 399) / 400;
        const uint32_t yoe = static_cast<uint32_t>(year - era * 400);
        const uint32_t doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + static_cast<uint32_t>(day) - 1;
        const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + static_cast<int64_t>(doe) - 719468;
    }
}

extern "C" const char* absolute_datetime_error() {
    return lastDateTimeError.c_str();
}

extern "C" int32_t absolute_datetime_local_offset_minutes() {
#if defined(_WIN32)
    DYNAMIC_TIME_ZONE_INFORMATION dtzi;
    DWORD res = GetDynamicTimeZoneInformation(&dtzi);
    long bias = dtzi.Bias;
    if (res == TIME_ZONE_ID_DAYLIGHT) bias += dtzi.DaylightBias;
    else if (res == TIME_ZONE_ID_STANDARD) bias += dtzi.StandardBias;
    return static_cast<int32_t>(-bias);
#else
    time_t t = time(nullptr);
    struct tm tm_local{};
    localtime_r(&t, &tm_local);
    return static_cast<int32_t>(tm_local.tm_gmtoff / 60);
#endif
}

extern "C" const char* absolute_datetime_local_zone_name() {
    int32_t offset = absolute_datetime_local_offset_minutes();
    char buf[32];
    int32_t absOff = offset >= 0 ? offset : -offset;
    int32_t hrs = absOff / 60;
    int32_t mins = absOff % 60;
    std::snprintf(buf, sizeof(buf), "%s%02d:%02d", offset >= 0 ? "+" : "-", hrs, mins);
    lastDateTimeResult = buf;
    return lastDateTimeResult.c_str();
}

extern "C" void absolute_datetime_civil_from_unix_millis(
    int64_t unixMillis, int32_t offsetMinutes,
    int32_t* outYear, int32_t* outMonth, int32_t* outDay,
    int32_t* outHour, int32_t* outMinute, int32_t* outSecond, int32_t* outMillis) {

    int64_t adjustedMillis = unixMillis + static_cast<int64_t>(offsetMinutes) * 60'000;
    int64_t seconds = adjustedMillis / 1000;
    int32_t millis = static_cast<int32_t>(adjustedMillis % 1000);
    if (millis < 0) {
        millis += 1000;
        seconds -= 1;
    }

    int64_t days = seconds / 86400;
    int32_t secsOfDay = static_cast<int32_t>(seconds % 86400);
    if (secsOfDay < 0) {
        secsOfDay += 86400;
        days -= 1;
    }

    int32_t year = 0, month = 0, day = 0;
    CivilFromDays(days, year, month, day);

    int32_t hour = secsOfDay / 3600;
    int32_t minute = (secsOfDay % 3600) / 60;
    int32_t second = secsOfDay % 60;

    if (outYear) *outYear = year;
    if (outMonth) *outMonth = month;
    if (outDay) *outDay = day;
    if (outHour) *outHour = hour;
    if (outMinute) *outMinute = minute;
    if (outSecond) *outSecond = second;
    if (outMillis) *outMillis = millis;
}

extern "C" int64_t absolute_datetime_civil_to_unix_millis(
    int32_t year, int32_t month, int32_t day,
    int32_t hour, int32_t minute, int32_t second, int32_t millis,
    int32_t offsetMinutes) {

    int64_t days = DaysFromCivil(year, month, day);
    int64_t seconds = days * 86400 + hour * 3600 + minute * 60 + second;
    int64_t unixMillis = seconds * 1000 + millis - static_cast<int64_t>(offsetMinutes) * 60'000;
    return unixMillis;
}

extern "C" const char* absolute_datetime_format_iso(
    int32_t year, int32_t month, int32_t day,
    int32_t hour, int32_t minute, int32_t second, int32_t millis,
    int32_t offsetMinutes) {

    // A negative year keeps its four digits: "%04d" spends one of them on the
    // sign, so year -1 printed as "-001" and could not be read back.
    char yearText[16];
    if (year < 0) {
        std::snprintf(yearText, sizeof(yearText), "-%04lld",
            static_cast<long long>(-static_cast<int64_t>(year)));
    } else {
        std::snprintf(yearText, sizeof(yearText), "%04d", year);
    }

    char zone[16];
    if (offsetMinutes == 0) {
        std::snprintf(zone, sizeof(zone), "Z");
    } else {
        int32_t absOff = offsetMinutes >= 0 ? offsetMinutes : -offsetMinutes;
        std::snprintf(zone, sizeof(zone), "%c%02d:%02d",
            offsetMinutes >= 0 ? '+' : '-', absOff / 60, absOff % 60);
    }

    char buf[64];
    if (millis > 0) {
        std::snprintf(buf, sizeof(buf), "%s-%02d-%02dT%02d:%02d:%02d.%03d%s",
            yearText, month, day, hour, minute, second, millis, zone);
    } else {
        std::snprintf(buf, sizeof(buf), "%s-%02d-%02dT%02d:%02d:%02d%s",
            yearText, month, day, hour, minute, second, zone);
    }
    lastDateTimeResult = buf;
    return lastDateTimeResult.c_str();
}

namespace {
    // A year is at least four digits and may be longer (an expanded year);
    // every other field is exactly as wide as it is written.
    bool ReadYearDigits(const char*& cursor, int32_t& value) {
        int64_t result = 0;
        int digits = 0;
        while (*cursor >= '0' && *cursor <= '9') {
            if (result > 100000000) return false;
            result = result * 10 + (*cursor - '0');
            ++digits;
            ++cursor;
        }
        if (digits < 4) return false;
        value = static_cast<int32_t>(result);
        return true;
    }

    bool ReadFixedDigits(const char*& cursor, int count, int32_t& value) {
        int32_t result = 0;
        for (int index = 0; index < count; ++index) {
            if (*cursor < '0' || *cursor > '9') return false;
            result = result * 10 + (*cursor - '0');
            ++cursor;
        }
        value = result;
        return true;
    }
}

extern "C" int32_t absolute_datetime_parse_iso(
    const char* text,
    int32_t* outYear, int32_t* outMonth, int32_t* outDay,
    int32_t* outHour, int32_t* outMinute, int32_t* outSecond, int32_t* outMillis,
    int32_t* outOffsetMinutes) {

    lastDateTimeError.clear();
    if (!text || !*text) {
        lastDateTimeError = "Empty ISO-8601 text";
        return 0;
    }

    const char* cursor = text;
    int32_t yearSign = 1;
    if (*cursor == '-' || *cursor == '+') {
        yearSign = (*cursor == '-') ? -1 : 1;
        ++cursor;
    }

    // The date is required, and it is all a bare "1999-12-31" consists of.
    // Reading it as a grammar rather than scanning for the last sign is what
    // keeps its day out of the zone: the value ends where the date ends, and
    // there is nothing left for an offset to be read out of.
    int32_t year = 0, month = 0, day = 0;
    if (!ReadYearDigits(cursor, year) || *cursor != '-' ||
        (++cursor, !ReadFixedDigits(cursor, 2, month)) || *cursor != '-' ||
        (++cursor, !ReadFixedDigits(cursor, 2, day))) {
        lastDateTimeError = "Invalid ISO-8601 format";
        return 0;
    }
    year *= yearSign;

    int32_t hour = 0, minute = 0, second = 0, millis = 0;
    int32_t offsetMinutes = 0;

    if (*cursor == 'T' || *cursor == 't' || *cursor == ' ') {
        ++cursor;
        if (!ReadFixedDigits(cursor, 2, hour) || *cursor != ':' ||
            (++cursor, !ReadFixedDigits(cursor, 2, minute))) {
            lastDateTimeError = "Invalid ISO-8601 time";
            return 0;
        }
        if (*cursor == ':') {
            ++cursor;
            if (!ReadFixedDigits(cursor, 2, second)) {
                lastDateTimeError = "Invalid ISO-8601 time";
                return 0;
            }
        }
        if (*cursor == '.' || *cursor == ',') {
            ++cursor;
            if (*cursor < '0' || *cursor > '9') {
                lastDateTimeError = "Invalid ISO-8601 fractional second";
                return 0;
            }
            // A fraction is a fraction: ".1" is 100 ms, not 1 ms. Digits past
            // the third are truncated, which is the resolution this type has.
            int digits = 0;
            while (*cursor >= '0' && *cursor <= '9') {
                if (digits < 3) millis = millis * 10 + (*cursor - '0');
                ++digits;
                ++cursor;
            }
            for (; digits < 3; ++digits) millis *= 10;
        }

        if (*cursor == 'Z' || *cursor == 'z') {
            ++cursor;
        } else if (*cursor == '+' || *cursor == '-') {
            int32_t sign = (*cursor == '-') ? -1 : 1;
            ++cursor;
            int32_t offsetHours = 0, offsetMins = 0;
            if (!ReadFixedDigits(cursor, 2, offsetHours)) {
                lastDateTimeError = "Invalid ISO-8601 zone offset";
                return 0;
            }
            if (*cursor == ':') ++cursor;
            if (*cursor >= '0' && *cursor <= '9') {
                if (!ReadFixedDigits(cursor, 2, offsetMins)) {
                    lastDateTimeError = "Invalid ISO-8601 zone offset";
                    return 0;
                }
            }
            if (offsetHours > 23 || offsetMins > 59) {
                lastDateTimeError = "Zone offset out of range";
                return 0;
            }
            offsetMinutes = sign * (offsetHours * 60 + offsetMins);
        }
    }

    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' || *cursor == '\n') ++cursor;
    if (*cursor != '\0') {
        lastDateTimeError = "Trailing text after ISO-8601 value";
        return 0;
    }

    if (outYear) *outYear = year;
    if (outMonth) *outMonth = month;
    if (outDay) *outDay = day;
    if (outHour) *outHour = hour;
    if (outMinute) *outMinute = minute;
    if (outSecond) *outSecond = second;
    if (outMillis) *outMillis = millis;
    if (outOffsetMinutes) *outOffsetMinutes = offsetMinutes;

    return 1;
}
