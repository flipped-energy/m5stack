#pragma once

#include <optional>
#include <string_view>

namespace flipped::core {

struct TimeZone {
    std::string_view iana;
    const char *posix;
};

std::optional<TimeZone> timeZoneFor(std::string_view iana);

}
