#include "flipped/core/time.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

namespace flipped::core {

namespace {

std::mutex zoneMutex;

bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

int digits(std::string_view text, size_t position, size_t count)
{
    int value = 0;
    for (size_t i = 0; i < count; ++i) {
        value = value * 10 + (text[position + i] - '0');
    }
    return value;
}

bool matches(std::string_view text, std::string_view pattern)
{
    if (text.size() < pattern.size()) {
        return false;
    }
    for (size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == 'd') {
            if (!isDigit(text[i])) {
                return false;
            }
        } else if (pattern[i] != text[i]) {
            return false;
        }
    }
    return true;
}

void civilFromDays(int64_t days, int64_t &year, int &month, int &day)
{
    days += 719468;
    const int64_t era = (days >= 0 ? days : days - 146096) / 146097;
    const int64_t dayOfEra = days - era * 146097;
    const int64_t yearOfEra = (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
    const int64_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
    const int64_t monthPrime = (5 * dayOfYear + 2) / 153;
    day = static_cast<int>(dayOfYear - (153 * monthPrime + 2) / 5 + 1);
    month = static_cast<int>(monthPrime < 10 ? monthPrime + 3 : monthPrime - 9);
    year = yearOfEra + era * 400 + (month <= 2 ? 1 : 0);
}

int64_t floorDiv(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

}

int64_t daysFromCivil(int64_t year, int month, int day)
{
    year -= month <= 2 ? 1 : 0;
    const int64_t era = (year >= 0 ? year : year - 399) / 400;
    const int64_t yearOfEra = year - era * 400;
    const int64_t dayOfYear = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
    const int64_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
    return era * 146097 + dayOfEra - 719468;
}

LocalTime toLocal(Instant instant, const TimeZone &zone)
{
    struct tm parts;
    {
        std::lock_guard<std::mutex> lock(zoneMutex);
        const char *current = std::getenv("TZ");
        if (current == nullptr || std::strcmp(current, zone.posix) != 0) {
            if (setenv("TZ", zone.posix, 1) != 0) {
                std::fprintf(stderr, "setenv TZ %s failed\n", zone.posix);
                std::abort();
            }
        }
        tzset();
        const time_t seconds = static_cast<time_t>(instant);
        if (localtime_r(&seconds, &parts) == nullptr) {
            std::fprintf(stderr, "localtime_r failed for %lld in %s\n", static_cast<long long>(instant), zone.posix);
            std::abort();
        }
    }
    LocalTime local{};
    const int64_t year = static_cast<int64_t>(parts.tm_year) + 1900;
    char text[64];
    const int length = std::snprintf(text, sizeof text, "%04lld-%02d-%02dT%02d:%02d:%02d",
                                     static_cast<long long>(year), parts.tm_mon + 1, parts.tm_mday, parts.tm_hour,
                                     parts.tm_min, parts.tm_sec);
    if (length != 19) {
        std::fprintf(stderr, "local time %s of %lld is not YYYY-MM-DDTHH:mm:ss\n", text, static_cast<long long>(instant));
        std::abort();
    }
    std::memcpy(local.text, text, 20);
    local.minuteOfDay = parts.tm_hour * 60 + parts.tm_min;
    const int64_t wallSeconds = daysFromCivil(year, parts.tm_mon + 1, parts.tm_mday) * 86400 +
                                parts.tm_hour * 3600 + parts.tm_min * 60 + parts.tm_sec;
    local.offsetSeconds = static_cast<int32_t>(wallSeconds - instant);
    return local;
}

bool isWallText(std::string_view text)
{
    return matches(text, "dddd-dd-ddTdd:dd:dd");
}

std::optional<Instant> wallAsUtc(std::string_view wall)
{
    if (!isWallText(wall)) {
        return std::nullopt;
    }
    const int64_t year = digits(wall, 0, 4);
    const int month = digits(wall, 5, 2);
    const int day = digits(wall, 8, 2);
    return daysFromCivil(year, month, day) * 86400 + digits(wall, 11, 2) * 3600 + digits(wall, 14, 2) * 60 +
           digits(wall, 17, 2);
}

std::vector<Instant> localOccurrences(std::string_view wall, const TimeZone &zone)
{
    std::vector<Instant> found;
    const std::string_view w = wall.substr(0, 19);
    const std::optional<Instant> u = wallAsUtc(w);
    if (!u) {
        return found;
    }
    const int32_t before = toLocal(*u - 86400, zone).offsetSeconds;
    const int32_t after = toLocal(*u + 86400, zone).offsetSeconds;
    std::vector<int32_t> offsets{before};
    if (after != before) {
        offsets.push_back(after);
    }
    for (int32_t offset : offsets) {
        const Instant candidate = *u - offset;
        if (toLocal(candidate, zone).wall() == w) {
            found.push_back(candidate);
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

std::optional<Instant> localToInstant(std::string_view wall, const TimeZone &zone)
{
    const std::vector<Instant> found = localOccurrences(wall, zone);
    if (found.empty()) {
        return std::nullopt;
    }
    return found.front();
}

std::string nextDate(std::string_view date)
{
    int64_t year = digits(date, 0, 4);
    int month = digits(date, 5, 2);
    int day = digits(date, 8, 2);
    static constexpr int LENGTHS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int length = month == 2 && leap ? 29 : LENGTHS[month - 1];
    ++day;
    if (day > length) {
        day = 1;
        ++month;
        if (month > 12) {
            month = 1;
            ++year;
        }
    }
    char text[64];
    std::snprintf(text, sizeof text, "%04lld-%02d-%02d", static_cast<long long>(year), month, day);
    return text;
}

bool isWallDateTime(std::string_view text)
{
    if (!isWallText(text)) {
        return false;
    }
    const int64_t year = digits(text, 0, 4);
    const int month = digits(text, 5, 2);
    const int day = digits(text, 8, 2);
    if (month < 1 || month > 12 || day < 1) {
        return false;
    }
    static constexpr int LENGTHS[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    const int length = month == 2 && leap ? 29 : LENGTHS[month - 1];
    return day <= length && digits(text, 11, 2) <= 23 && digits(text, 14, 2) <= 59 && digits(text, 17, 2) <= 59;
}

InstantText parseInstant(std::string_view text)
{
    InstantText result;
    if (!isWallDateTime(text)) {
        return result;
    }
    const std::optional<Instant> wall = wallAsUtc(text);
    size_t position = 19;
    if (position < text.size() && text[position] == '.') {
        ++position;
        const size_t fractionStart = position;
        while (position < text.size() && isDigit(text[position])) {
            ++position;
        }
        if (position == fractionStart) {
            return result;
        }
    }
    const std::string_view rest = text.substr(position);
    if (rest.empty()) {
        result.missingOffset = true;
        return result;
    }
    if (rest == "Z") {
        result.instant = *wall;
        return result;
    }
    if (rest.size() == 6 && (rest[0] == '+' || rest[0] == '-') && matches(rest.substr(1), "dd:dd")) {
        const int64_t offset = digits(rest, 1, 2) * 3600 + digits(rest, 4, 2) * 60;
        result.instant = rest[0] == '+' ? *wall - offset : *wall + offset;
        return result;
    }
    return result;
}

std::string formatInstant(Instant instant)
{
    const int64_t days = floorDiv(instant, 86400);
    const int64_t secondOfDay = instant - days * 86400;
    int64_t year = 0;
    int month = 0;
    int day = 0;
    civilFromDays(days, year, month, day);
    char text[96];
    std::snprintf(text, sizeof text, "%04lld-%02d-%02dT%02d:%02d:%02dZ", static_cast<long long>(year), month, day,
                  static_cast<int>(secondOfDay / 3600), static_cast<int>(secondOfDay / 60 % 60),
                  static_cast<int>(secondOfDay % 60));
    return text;
}

}
