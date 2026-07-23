#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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

    char buf[64];
    if (offsetMinutes == 0) {
        if (millis > 0) {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                year, month, day, hour, minute, second, millis);
        } else {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                year, month, day, hour, minute, second);
        }
    } else {
        int32_t absOff = offsetMinutes >= 0 ? offsetMinutes : -offsetMinutes;
        int32_t hrs = absOff / 60;
        int32_t mins = absOff % 60;
        char sign = offsetMinutes >= 0 ? '+' : '-';
        if (millis > 0) {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02d:%02d",
                year, month, day, hour, minute, second, millis, sign, hrs, mins);
        } else {
            std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
                year, month, day, hour, minute, second, sign, hrs, mins);
        }
    }
    lastDateTimeResult = buf;
    return lastDateTimeResult.c_str();
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

    int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0, ms = 0;
    int tzSign = 1, tzH = 0, tzM = 0;
    char zChar = '\0';

    int readCount = std::sscanf(text, "%d-%d-%dT%d:%d:%d.%d", &y, &m, &d, &hh, &mm, &ss, &ms);
    if (readCount < 3) {
        lastDateTimeError = "Invalid ISO-8601 format";
        return 0;
    }

    // Check timezone at the end of string
    const char* p = std::strchr(text, 'T');
    if (!p) p = text;
    const char* tzPos = std::strchr(p, 'Z');
    if (!tzPos) tzPos = std::strchr(p, 'z');
    if (tzPos) {
        tzH = 0;
        tzM = 0;
    } else {
        const char* plusPos = std::strrchr(p, '+');
        const char* minusPos = std::strrchr(p, '-');
        const char* signPos = plusPos ? plusPos : minusPos;
        if (signPos && signPos > p) {
            tzSign = (*signPos == '-') ? -1 : 1;
            std::sscanf(signPos + 1, "%d:%d", &tzH, &tzM);
        }
    }

    if (outYear) *outYear = y;
    if (outMonth) *outMonth = m;
    if (outDay) *outDay = d;
    if (outHour) *outHour = hh;
    if (outMinute) *outMinute = mm;
    if (outSecond) *outSecond = ss;
    if (outMillis) *outMillis = ms;
    if (outOffsetMinutes) *outOffsetMinutes = tzSign * (tzH * 60 + tzM);

    return 1;
}
