#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "flipped/core/signals.h"
#include "flipped/core/tz_table.h"
#include "flipped/core/types.h"

namespace flipped::core {

struct ProjectedPeriod {
    Instant start = 0;
    Instant end = 0;
    size_t scheduleIndex = 0;
};

std::vector<ProjectedPeriod> tariffProjection(Instant from, int64_t horizonSeconds, const TariffGroup &tariff,
                                              const TimeZone &zone);

}
