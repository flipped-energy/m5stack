#include "flipped/core/tariff_projection.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "flipped/core/time.h"

namespace flipped::core {

namespace {

constexpr int MINUTES_PER_DAY = 1440;
constexpr uint16_t NO_ENTRY = 0xFFFF;

std::vector<uint16_t> minuteIndex(const std::vector<ScheduleEntry> &schedule)
{
    std::vector<uint16_t> index(MINUTES_PER_DAY, NO_ENTRY);
    for (size_t i = 0; i < schedule.size(); ++i) {
        const ScheduleEntry &entry = schedule[i];
        if (entry.startMinute < 0 || entry.endMinute > MINUTES_PER_DAY || entry.startMinute >= entry.endMinute) {
            std::fprintf(stderr, "schedule entry %zu covers minutes %d..%d\n", i, entry.startMinute, entry.endMinute);
            std::abort();
        }
        for (int minute = entry.startMinute; minute < entry.endMinute; ++minute) {
            index[static_cast<size_t>(minute)] = static_cast<uint16_t>(i);
        }
    }
    const auto gap = std::find(index.begin(), index.end(), NO_ENTRY);
    if (gap != index.end()) {
        std::fprintf(stderr, "schedule leaves minute %td uncovered\n", gap - index.begin());
        std::abort();
    }
    return index;
}

}

std::vector<ProjectedPeriod> tariffProjection(Instant from, int64_t horizonSeconds, const TariffGroup &tariff,
                                              const TimeZone &zone)
{
    std::vector<ProjectedPeriod> periods;
    if (tariff.fault || tariff.schedule.empty() || horizonSeconds <= 0) {
        return periods;
    }
    Instant stop = from + horizonSeconds;
    if (tariff.planChangeInstant && *tariff.planChangeInstant < stop) {
        stop = *tariff.planChangeInstant;
    }
    if (stop <= from) {
        return periods;
    }
    const std::vector<uint16_t> index = minuteIndex(tariff.schedule);
    const auto entryAt = [&](Instant instant) {
        return static_cast<size_t>(index[static_cast<size_t>(toLocal(instant, zone).minuteOfDay)]);
    };
    size_t current = entryAt(from);
    Instant start = from;
    const Instant firstMinute = from - ((from % 60) + 60) % 60 + 60;
    for (Instant candidate = firstMinute; candidate < stop; candidate += 60) {
        const size_t entry = entryAt(candidate);
        if (tariff.schedule[entry].segment != tariff.schedule[current].segment) {
            periods.push_back(ProjectedPeriod{start, candidate, current});
            start = candidate;
            current = entry;
        }
    }
    periods.push_back(ProjectedPeriod{start, stop, current});
    const auto wholesale = std::find_if(periods.begin(), periods.end(), [&](const ProjectedPeriod &period) {
        return tariff.schedule[period.scheduleIndex].segment.wholesaleLinked;
    });
    periods.erase(wholesale, periods.end());
    return periods;
}

}
