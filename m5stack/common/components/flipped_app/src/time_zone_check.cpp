#include <array>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <optional>
#include <string>
#include <string_view>

#include "esp_log.h"

#include "flipped/app/app.h"
#include "flipped/core/time.h"
#include "flipped/core/tz_table.h"

namespace flipped::app {

namespace {

const char *TAG = "tz_check";

struct Expected {
    const char *iana;
    core::Instant instant;
    const char *wall;
    int32_t offsetSeconds;
};

constexpr std::array<Expected, 40> EXPECTED{{
    {"Australia/Sydney", 1775318399, "2026-04-05T02:59:59", 39600},
    {"Australia/Sydney", 1775318400, "2026-04-05T02:00:00", 36000},
    {"Australia/Sydney", 1791043199, "2026-10-04T01:59:59", 36000},
    {"Australia/Sydney", 1791043200, "2026-10-04T03:00:00", 39600},
    {"Australia/Sydney", 2185372799, "2039-04-03T02:59:59", 39600},
    {"Australia/Sydney", 2185372800, "2039-04-03T02:00:00", 36000},
    {"Australia/Sydney", 2201097599, "2039-10-02T01:59:59", 36000},
    {"Australia/Sydney", 2201097600, "2039-10-02T03:00:00", 39600},
    {"Australia/Brisbane", 1775318399, "2026-04-05T01:59:59", 36000},
    {"Australia/Brisbane", 1775318400, "2026-04-05T02:00:00", 36000},
    {"Australia/Brisbane", 1791043199, "2026-10-04T01:59:59", 36000},
    {"Australia/Brisbane", 1791043200, "2026-10-04T02:00:00", 36000},
    {"Australia/Brisbane", 2185372799, "2039-04-03T01:59:59", 36000},
    {"Australia/Brisbane", 2185372800, "2039-04-03T02:00:00", 36000},
    {"Australia/Brisbane", 2201097599, "2039-10-02T01:59:59", 36000},
    {"Australia/Brisbane", 2201097600, "2039-10-02T02:00:00", 36000},
    {"Australia/Adelaide", 1775320199, "2026-04-05T02:59:59", 37800},
    {"Australia/Adelaide", 1775320200, "2026-04-05T02:00:00", 34200},
    {"Australia/Adelaide", 1791044999, "2026-10-04T01:59:59", 34200},
    {"Australia/Adelaide", 1791045000, "2026-10-04T03:00:00", 37800},
    {"Australia/Adelaide", 2185374599, "2039-04-03T02:59:59", 37800},
    {"Australia/Adelaide", 2185374600, "2039-04-03T02:00:00", 34200},
    {"Australia/Adelaide", 2201099399, "2039-10-02T01:59:59", 34200},
    {"Australia/Adelaide", 2201099400, "2039-10-02T03:00:00", 37800},
    {"Australia/Darwin", 1775320199, "2026-04-05T01:59:59", 34200},
    {"Australia/Darwin", 1775320200, "2026-04-05T02:00:00", 34200},
    {"Australia/Darwin", 1791044999, "2026-10-04T01:59:59", 34200},
    {"Australia/Darwin", 1791045000, "2026-10-04T02:00:00", 34200},
    {"Australia/Darwin", 2185374599, "2039-04-03T01:59:59", 34200},
    {"Australia/Darwin", 2185374600, "2039-04-03T02:00:00", 34200},
    {"Australia/Darwin", 2201099399, "2039-10-02T01:59:59", 34200},
    {"Australia/Darwin", 2201099400, "2039-10-02T02:00:00", 34200},
    {"Australia/Perth", 1775318399, "2026-04-04T23:59:59", 28800},
    {"Australia/Perth", 1775318400, "2026-04-05T00:00:00", 28800},
    {"Australia/Perth", 1791043199, "2026-10-03T23:59:59", 28800},
    {"Australia/Perth", 1791043200, "2026-10-04T00:00:00", 28800},
    {"Australia/Perth", 2185372799, "2039-04-02T23:59:59", 28800},
    {"Australia/Perth", 2185372800, "2039-04-03T00:00:00", 28800},
    {"Australia/Perth", 2201097599, "2039-10-01T23:59:59", 28800},
    {"Australia/Perth", 2201097600, "2039-10-02T00:00:00", 28800},
}};

}

void checkTimeZones()
{
    size_t mismatches = 0;
    for (const Expected &row : EXPECTED) {
        const std::optional<core::TimeZone> zone = core::timeZoneFor(row.iana);
        if (!zone) {
            ESP_LOGE(TAG, "%s is not in the time zone table", row.iana);
            abort();
        }
        const core::LocalTime local = core::toLocal(row.instant, *zone);
        if (local.wall() == row.wall && local.offsetSeconds == row.offsetSeconds) {
            continue;
        }
        ++mismatches;
        const std::string wall(local.wall());
        ESP_LOGE(TAG, "%s (TZ=%s) at %lld: got %s offset %ld s, expected %s offset %ld s", row.iana, zone->posix,
                 static_cast<long long>(row.instant), wall.c_str(), static_cast<long>(local.offsetSeconds), row.wall,
                 static_cast<long>(row.offsetSeconds));
    }
    if (mismatches > 0) {
        ESP_LOGE(TAG, "%u of %u conversions differ from the IANA rules", static_cast<unsigned>(mismatches),
                 static_cast<unsigned>(EXPECTED.size()));
        abort();
    }
    ESP_LOGI(TAG, "%u conversions match the IANA rules (time_t %u bytes)", static_cast<unsigned>(EXPECTED.size()),
             static_cast<unsigned>(sizeof(time_t)));
}

}
