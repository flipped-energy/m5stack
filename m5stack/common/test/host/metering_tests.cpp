#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "flipped/core/energy_publication.h"
#include "flipped/core/flipped_cluster.h"
#include "flipped/core/metering_attribution.h"
#include "flipped/core/tariff_tables.h"
#include "flipped/core/time.h"
#include "flipped/core/tz_table.h"
#include "suite.h"

using namespace flipped::core;

namespace {

Instant at(const char *text)
{
    return *parseInstant(text).instant;
}

uint32_t matter(Instant instant)
{
    return static_cast<uint32_t>(instant - MATTER_EPOCH_UNIX_S);
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
    tariff.period.segment = offPeak;
    return tariff;
}

double importAt(int half)
{
    return 0.05 + 0.013 * (half % 7);
}

void addDay(EnergyGroup &energy, const char *date, Instant start)
{
    EnergyEntry day;
    day.local = std::string(date) + "T00:00:00";
    day.start = start;
    day.durationMinutes = 1440;
    for (int half = 0; half < 48; ++half) {
        EnergyEntry interval;
        char local[32];
        std::snprintf(local, sizeof local, "%sT%02d:%02d:00", date, half / 2, (half % 2) * 30);
        interval.local = local;
        interval.start = start + half * 1800;
        interval.durationMinutes = 30;
        interval.gridImportKwh = importAt(half);
        interval.controlledLoadKwh = 0.2;
        interval.solarExportKwh = 0.1;
        day.gridImportKwh += interval.gridImportKwh;
        energy.intervals.push_back(interval);
    }
    energy.days.push_back(day);
    energy.latestIntervalEnd = start + 86400;
}

struct Fixture {
    TariffTables tables;
    EnergyGroup energy;
    Instant previousDay = 0;
    Instant meteredDay = 0;
};

Fixture fixture()
{
    const TimeZone sydney = *timeZoneFor("Australia/Sydney");
    Fixture f;
    AccountGroup account;
    account.productName = "Flipped Flex";
    f.tables = tariffTables(timeOfUse(), account, at("2026-10-01T02:00:00Z"), sydney);
    f.previousDay = at("2026-09-28T14:00:00Z");
    f.meteredDay = at("2026-09-29T14:00:00Z");
    f.energy.nmi = "4103000000";
    addDay(f.energy, "2026-09-29", f.previousDay);
    addDay(f.energy, "2026-09-30", f.meteredDay);
    return f;
}

std::string near(const char *what, double got, double want)
{
    return std::fabs(got - want) <= 1e-9 ? "" : std::string(what) + " " + std::to_string(got) + " != " + std::to_string(want);
}

Ledger ledger()
{
    Ledger value;
    value.h = "0123456789abcdef";
    value.startedAt = at("2026-09-24T14:00:00Z");
    value.through = at("2026-09-30T14:00:00Z");
    value.importedMwh = 61234567;
    value.exportedMwh = 9876543;
    return value;
}

LedgerStep took(bool newLedger)
{
    LedgerStep step;
    step.took = 48;
    step.newLedger = newLedger;
    step.firstStart = at("2026-09-29T14:00:00Z");
    step.lastEnd = at("2026-09-30T14:00:00Z");
    step.deltaImportedMwh = 11234567;
    step.deltaExportedMwh = 4800000;
    return step;
}

std::string sameMeasurement(const char *what, const std::optional<EnergyMeasurementValue> &got,
                            std::optional<EnergyMeasurementValue> want)
{
    if (got == want) {
        return "";
    }
    return std::string(what) + (got ? " " + std::to_string(got->energyMwh) : std::string(" null")) + " differs";
}

}

bool runMeteringTests()
{
    Suite suite("metering_tests");

    suite.run("attribution: each half hour lands in exactly one tariff period", []() -> std::string {
        const Fixture f = fixture();
        const MeteringAttribution attribution = meteringAttribution(f.energy, f.tables, f.previousDay);
        if (!attribution.meteredQuantity || attribution.meteredQuantity->size() != 3) {
            return "expected three metered quantities";
        }
        double offPeak = 0;
        double shoulder = 0;
        double peak = 0;
        for (int half = 0; half < 48; ++half) {
            const int minute = half * 30;
            if (minute < 420 || minute >= 1320) {
                offPeak += importAt(half);
            } else if (minute < 840 || minute >= 1200) {
                shoulder += importAt(half);
            } else {
                peak += importAt(half);
            }
        }
        const std::vector<MeteredQuantityValue> &q = *attribution.meteredQuantity;
        std::string why = near("off-peak kWh", q[0].kwh, offPeak) + near("shoulder kWh", q[1].kwh, shoulder) +
                          near("peak kWh", q[2].kwh, peak);
        why += near("sum over periods", q[0].kwh + q[1].kwh + q[2].kwh, f.energy.days[1].gridImportKwh);
        if (!why.empty()) {
            return why;
        }
        for (size_t i = 0; i < 3; ++i) {
            if (q[i].componentIds != (*f.tables.periods)[i].componentIds || q[i].quantity != std::llround(q[i].kwh)) {
                return "quantity " + std::to_string(i) + " has the wrong components or rounding";
            }
        }
        if (attribution.meteredQuantityTimestamp != matter(f.meteredDay + 86400) ||
            attribution.meteredDayStart != f.meteredDay) {
            return "MeteredQuantityTimestamp is not the end of the metered day";
        }
        return "";
    });

    suite.run("attribution: the newest complete day is metered", []() -> std::string {
        Fixture f = fixture();
        *f.energy.latestIntervalEnd -= 1800;
        const MeteringAttribution attribution = meteringAttribution(f.energy, f.tables, f.previousDay);
        return attribution.meteredDayStart == f.previousDay ? "" : "the incomplete day was metered";
    });

    suite.run("attribution: no day qualifies before tariff_t", []() -> std::string {
        const Fixture f = fixture();
        const MeteringAttribution late = meteringAttribution(f.energy, f.tables, f.meteredDay + 60);
        if (late.meteredQuantity || late.meteredQuantityTimestamp) {
            return "a day that began before the table was published was metered";
        }
        const MeteringAttribution onTime = meteringAttribution(f.energy, f.tables, f.meteredDay);
        return onTime.meteredDayStart == f.meteredDay ? "" : "the day that began at tariff_t was not metered";
    });

    suite.run("attribution: null when either group is faulted", []() -> std::string {
        Fixture energyFault = fixture();
        energyFault.energy.fault = Fault{"usage_unavailable", 503, std::string("down"), 4, std::nullopt};
        if (meteringAttribution(energyFault.energy, energyFault.tables, energyFault.previousDay).meteredQuantity) {
            return "metered with a faulted energy group";
        }
        Fixture tariffFault = fixture();
        tariffFault.tables = TariffTables{};
        return meteringAttribution(tariffFault.energy, tariffFault.tables, tariffFault.previousDay).meteredQuantity
                   ? "metered with a faulted tariff group"
                   : "";
    });

    suite.run("publication row: ok, took intervals, existing ledger", []() -> std::string {
        const EnergyGroup energy = fixture().energy;
        const EnergyPublication p = energyPublication(energy, ledger(), took(false), EnergyPublication{});
        const uint32_t through = matter(at("2026-09-30T14:00:00Z"));
        const uint32_t first = matter(at("2026-09-29T14:00:00Z"));
        std::string why = sameMeasurement("cumulativeImported", p.cumulativeImported,
                                          EnergyMeasurementValue{61234567, std::nullopt, through});
        why += sameMeasurement("cumulativeExported", p.cumulativeExported,
                               EnergyMeasurementValue{9876543, std::nullopt, through});
        why += sameMeasurement("periodicImported", p.periodicImported, EnergyMeasurementValue{11234567, first, through});
        why += sameMeasurement("periodicExported", p.periodicExported, EnergyMeasurementValue{4800000, first, through});
        if (p.cumulativeResetTimestamp != matter(at("2026-09-24T14:00:00Z"))) {
            why += "CumulativeEnergyReset is not startedAt";
        }
        if (!p.cumulativeEvent || !p.periodicEvent) {
            why += "both events are expected";
        }
        return why;
    });

    suite.run("publication row: ok, took intervals, new ledger", []() -> std::string {
        const EnergyGroup energy = fixture().energy;
        const EnergyPublication p = energyPublication(energy, ledger(), took(true), EnergyPublication{});
        if (!p.cumulativeImported || !p.cumulativeExported || p.periodicImported || p.periodicExported) {
            return "a new ledger publishes totals and no periodic energy";
        }
        return p.cumulativeEvent && !p.periodicEvent ? "" : "a new ledger sends one CumulativeEnergyMeasured only";
    });

    suite.run("publication row: ok, took nothing", []() -> std::string {
        const EnergyGroup energy = fixture().energy;
        const EnergyPublication before = energyPublication(energy, ledger(), took(false), EnergyPublication{});
        LedgerStep nothing;
        const EnergyPublication p = energyPublication(energy, ledger(), nothing, before);
        if (p.periodicImported != before.periodicImported || p.periodicExported != before.periodicExported ||
            p.cumulativeImported != before.cumulativeImported || p.cumulativeEvent || p.periodicEvent) {
            return "took nothing must keep the published values and send no event";
        }
        if (!changedEnergyAttributes(before, p).empty()) {
            return "took nothing marked an attribute";
        }
        const EnergyPublication restored = energyPublication(energy, ledger(), std::nullopt, EnergyPublication{});
        if (!restored.cumulativeImported || restored.periodicImported || !restored.cumulativeEvent) {
            return "after a null total, the totals return with one CumulativeEnergyMeasured and periodic stays null";
        }
        return "";
    });

    suite.run("publication row: faulted", []() -> std::string {
        EnergyGroup energy = fixture().energy;
        const EnergyPublication before = energyPublication(energy, ledger(), took(false), EnergyPublication{});
        energy.fault = Fault{"usage_unavailable", 503, std::string("down"), 4, std::nullopt};
        const EnergyPublication p = energyPublication(energy, ledger(), took(false), before);
        if (p.cumulativeImported || p.cumulativeExported || p.periodicImported || p.periodicExported ||
            p.cumulativeResetTimestamp || p.cumulativeEvent || p.periodicEvent) {
            return "a faulted energy group published a value or an event";
        }
        return changedEnergyAttributes(before, p).size() == ENERGY_ATTRIBUTE_COUNT ? "" : "not every attribute was marked";
    });

    suite.run("publication row: a refused ledger publishes null with the refusal as a problem", []() -> std::string {
        EnergyGroup energy = fixture().energy;
        const EnergyPublication before = energyPublication(energy, ledger(), took(false), EnergyPublication{});
        EnergyEntry negative;
        negative.local = "2026-10-01T00:00:00";
        negative.start = at("2026-09-30T14:00:00Z");
        negative.durationMinutes = 30;
        negative.gridImportKwh = -0.1;
        energy.intervals.push_back(negative);
        const EnergyPublication p = energyPublication(energy, ledger(), std::nullopt, before);
        if (p.cumulativeImported || p.cumulativeExported || p.periodicImported || p.periodicExported ||
            p.cumulativeResetTimestamp || p.cumulativeEvent || p.periodicEvent) {
            return "a refused ledger published a value or an event";
        }
        const std::string want = "ledger not advanced: energy interval 2026-10-01T00:00:00 (" +
                                 std::to_string(at("2026-09-30T14:00:00Z")) + ") has gridImportKwh -0.1";
        if (p.problems != std::vector<std::string>{want}) {
            return "problems: " + (p.problems.empty() ? std::string("(none)") : p.problems.front());
        }
        return changedEnergyAttributes(before, p).size() == ENERGY_ATTRIBUTE_COUNT ? "" : "not every attribute was marked";
    });

    suite.run("the 1-second rule defers a mark to one second after the last", []() -> std::string {
        if (deferredMarkAt(std::nullopt, 5000) || deferredMarkAt(5000, 6000) || deferredMarkAt(5000, 9000)) {
            return "a mark at least 1 s after the last was deferred";
        }
        const std::optional<int64_t> deferred = deferredMarkAt(5000, 5400);
        return deferred == 6000 ? "" : "a mark 0.4 s after the last was not deferred to +1 s";
    });

    return suite.finish();
}
