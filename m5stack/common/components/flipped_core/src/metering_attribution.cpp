#include "flipped/core/metering_attribution.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "flipped/core/flipped_cluster.h"
#include "flipped/core/time.h"

namespace flipped::core {

namespace {

[[noreturn]] void invalid(const std::string &what)
{
    std::fprintf(stderr, "%s\n", what.c_str());
    std::abort();
}

int localMinute(const EnergyEntry &interval)
{
    if (!isWallText(interval.local) || interval.local.size() < 16) {
        invalid("energy interval " + std::to_string(interval.start) + " has local time '" + interval.local + "'");
    }
    const auto digit = [&](size_t at) { return interval.local[at] - '0'; };
    return (digit(11) * 10 + digit(12)) * 60 + digit(14) * 10 + digit(15);
}

std::optional<uint16_t> startOf(const TariffTables &tables, uint32_t id)
{
    for (const DayEntryValue &entry : *tables.dayEntries) {
        if (entry.id == id) {
            return entry.startTime;
        }
    }
    return std::nullopt;
}

}

MeteringAttribution meteringAttribution(const EnergyGroup &energy, const TariffTables &tables, Instant tariffPublishedAt)
{
    MeteringAttribution attribution;
    if (energy.fault || !energy.latestIntervalEnd || !tables.periods || !tables.dayPatterns || !tables.dayEntries ||
        tables.dayPatterns->size() != 1) {
        return attribution;
    }
    const EnergyEntry *day = nullptr;
    for (const EnergyEntry &candidate : energy.days) {
        const Instant end = candidate.start + static_cast<Instant>(candidate.durationMinutes) * 60;
        if (end <= *energy.latestIntervalEnd && candidate.start >= tariffPublishedAt &&
            (day == nullptr || candidate.start > day->start)) {
            day = &candidate;
        }
    }
    if (day == nullptr) {
        return attribution;
    }
    const Instant dayEnd = day->start + static_cast<Instant>(day->durationMinutes) * 60;

    std::vector<std::pair<uint16_t, size_t>> starts;
    for (uint32_t id : tables.dayPatterns->front().dayEntryIds) {
        const std::optional<uint16_t> start = startOf(tables, id);
        if (!start) {
            invalid("DayPattern entry " + std::to_string(id) + " is not in DayEntries");
        }
        const auto owner = std::find_if(tables.periods->begin(), tables.periods->end(), [&](const TariffPeriodValue &p) {
            return std::find(p.dayEntryIds.begin(), p.dayEntryIds.end(), id) != p.dayEntryIds.end();
        });
        if (owner == tables.periods->end()) {
            invalid("DayEntry " + std::to_string(id) + " belongs to no TariffPeriod");
        }
        starts.emplace_back(*start, static_cast<size_t>(owner - tables.periods->begin()));
    }
    std::sort(starts.begin(), starts.end());
    if (starts.empty() || starts.front().first != 0) {
        invalid("the DayPattern does not start at minute 0");
    }

    std::vector<double> sums(tables.periods->size(), 0.0);
    std::vector<bool> seen(tables.periods->size(), false);
    for (const EnergyEntry &interval : energy.intervals) {
        if (interval.start < day->start || interval.start >= dayEnd) {
            continue;
        }
        const int minute = localMinute(interval);
        size_t period = starts.front().second;
        for (const auto &[start, owner] : starts) {
            if (start <= minute) {
                period = owner;
            }
        }
        sums[period] += interval.gridImportKwh;
        seen[period] = true;
    }

    std::vector<MeteredQuantityValue> quantities;
    for (size_t i = 0; i < sums.size(); ++i) {
        if (!seen[i]) {
            continue;
        }
        const double rounded = std::round(sums[i]);
        if (!(rounded >= static_cast<double>(std::numeric_limits<int64_t>::min())) ||
            !(rounded < static_cast<double>(std::numeric_limits<int64_t>::max()))) {
            attribution.problems.push_back("MeteredQuantity " + std::to_string(sums[i]) + " kWh does not fit int64");
            return attribution;
        }
        quantities.push_back(MeteredQuantityValue{(*tables.periods)[i].componentIds, static_cast<int64_t>(rounded), sums[i]});
    }
    if (quantities.size() > MAXIMUM_METERED_QUANTITIES) {
        attribution.problems.push_back("metered day " + day->local + " has " + std::to_string(quantities.size()) +
                                       " tariff periods; MaximumMeteredQuantities is " +
                                       std::to_string(MAXIMUM_METERED_QUANTITIES));
        return attribution;
    }
    const Instant seconds = dayEnd - MATTER_EPOCH_UNIX_S;
    if (seconds < 0 || seconds >= std::numeric_limits<uint32_t>::max()) {
        attribution.problems.push_back("metered day end " + std::to_string(dayEnd) +
                                       " is outside the Matter epoch-s range");
        return attribution;
    }
    attribution.meteredQuantity = quantities;
    attribution.meteredQuantityTimestamp = static_cast<uint32_t>(seconds);
    attribution.meteredDayStart = day->start;
    return attribution;
}

}
