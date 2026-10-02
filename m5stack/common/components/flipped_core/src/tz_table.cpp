#include "flipped/core/tz_table.h"

#include <array>

namespace flipped::core {

namespace {

constexpr const char *EASTERN_DAYLIGHT = "AEST-10AEDT,M10.1.0,M4.1.0/3";
constexpr const char *EASTERN_STANDARD = "AEST-10";
constexpr const char *CENTRAL_DAYLIGHT = "ACST-9:30ACDT,M10.1.0,M4.1.0/3";
constexpr const char *CENTRAL_STANDARD = "ACST-9:30";
constexpr const char *WESTERN_STANDARD = "AWST-8";

constexpr std::array<TimeZone, 19> TABLE = {{
    {"Australia/Sydney", EASTERN_DAYLIGHT},
    {"Australia/Melbourne", EASTERN_DAYLIGHT},
    {"Australia/Canberra", EASTERN_DAYLIGHT},
    {"Australia/ACT", EASTERN_DAYLIGHT},
    {"Australia/NSW", EASTERN_DAYLIGHT},
    {"Australia/Victoria", EASTERN_DAYLIGHT},
    {"Australia/Hobart", EASTERN_DAYLIGHT},
    {"Australia/Tasmania", EASTERN_DAYLIGHT},
    {"Australia/Brisbane", EASTERN_STANDARD},
    {"Australia/Queensland", EASTERN_STANDARD},
    {"Australia/Lindeman", EASTERN_STANDARD},
    {"Australia/Adelaide", CENTRAL_DAYLIGHT},
    {"Australia/South", CENTRAL_DAYLIGHT},
    {"Australia/Broken_Hill", CENTRAL_DAYLIGHT},
    {"Australia/Yancowinna", CENTRAL_DAYLIGHT},
    {"Australia/Darwin", CENTRAL_STANDARD},
    {"Australia/North", CENTRAL_STANDARD},
    {"Australia/Perth", WESTERN_STANDARD},
    {"Australia/West", WESTERN_STANDARD},
}};

}

std::optional<TimeZone> timeZoneFor(std::string_view iana)
{
    for (const TimeZone &zone : TABLE) {
        if (zone.iana == iana) {
            return zone;
        }
    }
    return std::nullopt;
}

}
