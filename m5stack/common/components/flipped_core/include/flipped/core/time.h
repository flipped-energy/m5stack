#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flipped/core/types.h"
#include "flipped/core/tz_table.h"

namespace flipped::core {

struct LocalTime {
    char text[20];
    int minuteOfDay;
    int32_t offsetSeconds;

    std::string_view wall() const { return std::string_view(text, 19); }
    std::string_view date() const { return std::string_view(text, 10); }
};

LocalTime toLocal(Instant instant, const TimeZone &zone);
std::vector<Instant> localOccurrences(std::string_view wall, const TimeZone &zone);
std::optional<Instant> localToInstant(std::string_view wall, const TimeZone &zone);
std::string nextDate(std::string_view date);

bool isWallText(std::string_view text);
bool isWallDateTime(std::string_view text);
std::optional<Instant> wallAsUtc(std::string_view wall);
int64_t daysFromCivil(int64_t year, int month, int day);

struct InstantText {
    std::optional<Instant> instant;
    bool missingOffset = false;
};

InstantText parseInstant(std::string_view text);
std::string formatInstant(Instant instant);

}
