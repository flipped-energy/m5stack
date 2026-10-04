#include <optional>
#include <string>
#include <vector>

#include "flipped/core/tariff_projection.h"
#include "flipped/core/time.h"
#include "flipped/core/tz_table.h"
#include "suite.h"

using namespace flipped::core;

namespace {

struct Want {
    const char *start;
    const char *end;
    size_t scheduleIndex;
};

Instant at(const char *text)
{
    return *parseInstant(text).instant;
}

Segment segment(Band band, const char *name, int64_t rateKey)
{
    Segment value;
    value.band = band;
    value.name = name;
    value.rateKey = rateKey;
    value.blocks.push_back(RateBlock{0, std::nullopt, rateKey});
    return value;
}

TariffGroup timeOfUse()
{
    const Segment offPeak = segment(Band::offPeak, "Off Peak", 180000000);
    const Segment shoulder = segment(Band::shoulder, "Shoulder", 270000000);
    const Segment peak = segment(Band::peak, "Peak", 450000000);
    TariffGroup tariff;
    tariff.structure = Structure::timeOfUse;
    tariff.schedule = {ScheduleEntry{0, 420, offPeak}, ScheduleEntry{420, 840, shoulder}, ScheduleEntry{840, 1200, peak},
                       ScheduleEntry{1200, 1320, shoulder}, ScheduleEntry{1320, 1440, offPeak}};
    return tariff;
}

std::string show(const std::vector<ProjectedPeriod> &periods)
{
    std::string text;
    for (const ProjectedPeriod &period : periods) {
        text += "[" + formatInstant(period.start) + " " + formatInstant(period.end) + " #" +
                std::to_string(period.scheduleIndex) + "]";
    }
    return text.empty() ? "no periods" : text;
}

std::string tiles(const std::vector<ProjectedPeriod> &periods, Instant from, Instant end)
{
    if (periods.empty() || periods.front().start != from || periods.back().end != end) {
        return "periods do not cover " + formatInstant(from) + " to " + formatInstant(end) + ": " + show(periods);
    }
    for (size_t i = 0; i < periods.size(); ++i) {
        if (periods[i].start >= periods[i].end || (i > 0 && periods[i].start != periods[i - 1].end)) {
            return "periods do not tile: " + show(periods);
        }
    }
    return "";
}

std::string expect(const std::vector<ProjectedPeriod> &periods, const std::vector<Want> &wants)
{
    bool same = periods.size() == wants.size();
    for (size_t i = 0; same && i < wants.size(); ++i) {
        same = periods[i].start == at(wants[i].start) && periods[i].end == at(wants[i].end) &&
               periods[i].scheduleIndex == wants[i].scheduleIndex;
    }
    return same ? "" : "got " + show(periods);
}

}

bool runProjectionTests()
{
    Suite suite("projection_tests");
    const TimeZone sydney = *timeZoneFor("Australia/Sydney");

    suite.run("periods tile 24 hours and a run across midnight is one period", [&]() -> std::string {
        const Instant from = at("2026-10-01T02:21:10Z");
        const std::vector<ProjectedPeriod> periods = tariffProjection(from, 86400, timeOfUse(), sydney);
        const std::string tiled = tiles(periods, from, from + 86400);
        if (!tiled.empty()) {
            return tiled;
        }
        return expect(periods, {{"2026-10-01T02:21:10Z", "2026-10-01T04:00:00Z", 1},
                                {"2026-10-01T04:00:00Z", "2026-10-01T10:00:00Z", 2},
                                {"2026-10-01T10:00:00Z", "2026-10-01T12:00:00Z", 3},
                                {"2026-10-01T12:00:00Z", "2026-10-01T21:00:00Z", 4},
                                {"2026-10-01T21:00:00Z", "2026-10-02T02:21:10Z", 1}});
    });

    suite.run("the night of the daylight-saving start is an hour shorter", [&]() -> std::string {
        const Instant from = at("2026-10-03T12:00:00Z");
        const std::vector<ProjectedPeriod> periods = tariffProjection(from, 12 * 3600, timeOfUse(), sydney);
        const std::string tiled = tiles(periods, from, from + 12 * 3600);
        if (!tiled.empty()) {
            return tiled;
        }
        return expect(periods, {{"2026-10-03T12:00:00Z", "2026-10-03T20:00:00Z", 4},
                                {"2026-10-03T20:00:00Z", "2026-10-04T00:00:00Z", 1}});
    });

    suite.run("the list stops at planChangeInstant", [&]() -> std::string {
        TariffGroup tariff = timeOfUse();
        tariff.planChangeInstant = at("2026-10-01T06:30:00Z");
        const Instant from = at("2026-10-01T02:21:10Z");
        const std::vector<ProjectedPeriod> periods = tariffProjection(from, 86400, tariff, sydney);
        const std::string tiled = tiles(periods, from, *tariff.planChangeInstant);
        if (!tiled.empty()) {
            return tiled;
        }
        return expect(periods, {{"2026-10-01T02:21:10Z", "2026-10-01T04:00:00Z", 1},
                                {"2026-10-01T04:00:00Z", "2026-10-01T06:30:00Z", 2}});
    });

    suite.run("a planChangeInstant at or before from gives no periods", [&]() -> std::string {
        TariffGroup tariff = timeOfUse();
        tariff.planChangeInstant = at("2026-10-01T02:21:10Z");
        const std::vector<ProjectedPeriod> periods = tariffProjection(at("2026-10-01T02:21:10Z"), 86400, tariff, sydney);
        return periods.empty() ? "" : "got " + show(periods);
    });

    suite.run("the list stops at the first wholesale-linked period", [&]() -> std::string {
        TariffGroup tariff = timeOfUse();
        tariff.schedule[2].segment.wholesaleLinked = true;
        const std::vector<ProjectedPeriod> periods = tariffProjection(at("2026-10-01T02:21:10Z"), 86400, tariff, sydney);
        if (!expect(periods, {{"2026-10-01T02:21:10Z", "2026-10-01T04:00:00Z", 1}}).empty()) {
            return "got " + show(periods);
        }
        const std::vector<ProjectedPeriod> inside = tariffProjection(at("2026-10-01T05:00:00Z"), 86400, tariff, sydney);
        return inside.empty() ? "" : "inside the wholesale-linked period got " + show(inside);
    });

    suite.run("a flat plan is one period over the horizon", [&]() -> std::string {
        TariffGroup tariff;
        tariff.schedule = {ScheduleEntry{0, 1440, segment(Band::anytime, "Anytime", 321970000)}};
        const Instant from = at("2026-10-01T02:21:10Z");
        const std::vector<ProjectedPeriod> periods = tariffProjection(from, 86400, tariff, sydney);
        return expect(periods, {{"2026-10-01T02:21:10Z", "2026-10-02T02:21:10Z", 0}});
    });

    suite.run("a faulted tariff group gives no periods", [&]() -> std::string {
        TariffGroup tariff = timeOfUse();
        tariff.fault = Fault{"plan_unavailable", std::nullopt, std::nullopt, std::nullopt, std::nullopt};
        const std::vector<ProjectedPeriod> periods = tariffProjection(at("2026-10-01T02:21:10Z"), 86400, tariff, sydney);
        return periods.empty() ? "" : "got " + show(periods);
    });

    return suite.finish();
}
