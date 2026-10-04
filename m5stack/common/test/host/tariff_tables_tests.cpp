#include <algorithm>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "flipped/core/flipped_cluster.h"
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

uint32_t matter(const char *text)
{
    return static_cast<uint32_t>(at(text) - MATTER_EPOCH_UNIX_S);
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

AccountGroup account()
{
    AccountGroup value;
    value.productName = "Flipped Flex";
    value.timeZone = "Australia/Sydney";
    return value;
}

std::string ids(const std::vector<uint32_t> &values)
{
    std::string text;
    for (uint32_t value : values) {
        text += (text.empty() ? "" : ",") + std::to_string(value);
    }
    return "[" + text + "]";
}

std::string starts(const TariffTables &tables, const std::vector<uint32_t> &entryIds)
{
    std::string text;
    for (uint32_t id : entryIds) {
        for (const DayEntryValue &entry : *tables.dayEntries) {
            if (entry.id == id) {
                text += (text.empty() ? "" : ",") + std::to_string(entry.startTime);
            }
        }
    }
    return "[" + text + "]";
}

std::string periodOf(const TariffTables &tables, uint32_t entryId, size_t &index)
{
    size_t owners = 0;
    for (size_t i = 0; i < tables.periods->size(); ++i) {
        const std::vector<uint32_t> &entries = (*tables.periods)[i].dayEntryIds;
        if (std::find(entries.begin(), entries.end(), entryId) != entries.end()) {
            ++owners;
            index = i;
        }
    }
    return owners == 1 ? "" : "day entry " + std::to_string(entryId) + " is in " + std::to_string(owners) + " periods";
}

}

bool runTariffTablesTests()
{
    Suite suite("tariff_tables_tests");
    const TimeZone sydney = *timeZoneFor("Australia/Sydney");
    const Instant noon = at("2026-10-01T02:00:00Z");

    suite.run("IDs are stable for equal input and differ for different content", [&]() -> std::string {
        const TariffTables first = tariffTables(timeOfUse(), account(), noon, sydney);
        const TariffTables again = tariffTables(timeOfUse(), account(), noon + 3600, sydney);
        if (!first.components || first.components->size() != 3) {
            return "expected 3 components";
        }
        if (*first.components != *again.components || *first.dayPatterns != *again.dayPatterns ||
            *first.periods != *again.periods || first.tableHash != again.tableHash) {
            return "equal input gave different tables";
        }
        std::set<uint32_t> unique;
        for (const TariffComponentValue &component : *first.components) {
            if (component.id == 0) {
                return "a TariffComponentID is 0";
            }
            unique.insert(component.id);
        }
        if (unique.size() != 3) {
            return "TariffComponentIDs are not unique";
        }
        TariffGroup dearer = timeOfUse();
        for (ScheduleEntry &entry : dearer.schedule) {
            if (entry.segment.band == Band::peak) {
                entry.segment.rateKey = 460000000;
                entry.segment.blocks.front().rateKey = 460000000;
            }
        }
        const TariffTables changed = tariffTables(dearer, account(), noon, sydney);
        if ((*changed.components)[0].id != (*first.components)[0].id) {
            return "an unchanged component changed its ID";
        }
        if ((*changed.components)[2].id == (*first.components)[2].id) {
            return "a component with a different price kept its ID";
        }
        if (changed.tableHash == first.tableHash) {
            return "the table hash did not change with the content";
        }
        if (changed.dayPatterns->front().id != first.dayPatterns->front().id) {
            return "the DayPattern ID changed although its entries did not";
        }
        return "";
    });

    suite.run("day entries tile the day and each belongs to one period", [&]() -> std::string {
        const TariffTables tables = tariffTables(timeOfUse(), account(), noon, sydney);
        const DayPatternValue &pattern = tables.dayPatterns->front();
        if (pattern.daysOfWeek != 0x7F || starts(tables, pattern.dayEntryIds) != "[0,420,840,1200,1320]" ||
            ids(pattern.dayEntryIds) != "[1,421,841,1201,1321]") {
            return "pattern " + ids(pattern.dayEntryIds) + " starts " + starts(tables, pattern.dayEntryIds);
        }
        if (!tables.individualDays || !tables.individualDays->empty()) {
            return "IndividualDays is not an empty list on an ordinary day";
        }
        if (tables.calendarPeriods->size() != 1 || tables.calendarPeriods->front().startDate != 0 ||
            tables.calendarPeriods->front().dayPatternIds != std::vector<uint32_t>{pattern.id}) {
            return "CalendarPeriods is not one period from date 0 with the one pattern";
        }
        if (tables.periods->size() != 3) {
            return "expected 3 periods, got " + std::to_string(tables.periods->size());
        }
        for (uint32_t id : pattern.dayEntryIds) {
            size_t index = 0;
            const std::string why = periodOf(tables, id, index);
            if (!why.empty()) {
                return why;
            }
        }
        if (ids((*tables.periods)[0].dayEntryIds) != "[1,1321]" || (*tables.periods)[0].label != "Off Peak") {
            return "the off-peak period holds " + ids((*tables.periods)[0].dayEntryIds);
        }
        if (tables.info->label != "Flipped Flex" || tables.info->providerName != "Flipped Energy" ||
            tables.info->blockMode != BlockMode::noBlock || tables.startDate != 0u) {
            return "TariffInfo or StartDate is wrong";
        }
        const TariffComponentValue &peak = (*tables.components)[2];
        if (!peak.peak || !peak.price || peak.price->price != matterMoney(450000000) || peak.price->priceLevel != 2 ||
            peak.threshold || peak.label != "Peak") {
            return "the peak component is wrong";
        }
        if ((*tables.components)[0].peak || (*tables.components)[0].price->priceLevel != 0) {
            return "the off-peak component is wrong";
        }
        return "";
    });

    suite.run("daylight-saving start day: IndividualDays in elapsed minutes", [&]() -> std::string {
        const TariffTables tables = tariffTables(timeOfUse(), account(), at("2026-10-04T04:00:00Z"), sydney);
        if (tables.individualDays->size() != 1) {
            return "expected one individual day, got " + std::to_string(tables.individualDays->size());
        }
        const DayValue &day = tables.individualDays->front();
        if (day.date != matter("2026-10-03T14:00:00Z")) {
            return "individual day date " + std::to_string(day.date);
        }
        if (starts(tables, day.dayEntryIds) != "[0,360,780,1140,1260]") {
            return "elapsed start times " + starts(tables, day.dayEntryIds);
        }
        for (uint32_t id : day.dayEntryIds) {
            size_t index = 0;
            if ((id & INDIVIDUAL_DAY_ENTRY_BIT) == 0) {
                return "individual day entry " + std::to_string(id) + " lacks the high bit";
            }
            const std::string why = periodOf(tables, id, index);
            if (!why.empty()) {
                return why;
            }
        }
        if (tables.currentDay->dayEntryIds != day.dayEntryIds || tables.currentDay->date != day.date) {
            return "CurrentDay does not use the individual day";
        }
        if (tables.currentDayEntry->startTime != 780 || tables.currentDayEntryDate != matter("2026-10-04T03:00:00Z")) {
            return "CurrentDayEntry starts at " + std::to_string(tables.currentDayEntry->startTime);
        }
        if (tables.nextDay->dayEntryIds != tables.dayPatterns->front().dayEntryIds ||
            tables.nextDay->date != matter("2026-10-04T13:00:00Z")) {
            return "NextDay is not the pattern day after the transition";
        }
        const TariffTables before = tariffTables(timeOfUse(), account(), at("2026-10-03T02:00:00Z"), sydney);
        if (before.individualDays->size() != 1 || before.nextDay->dayEntryIds != before.individualDays->front().dayEntryIds) {
            return "the day before the transition does not carry it as NextDay";
        }
        return "";
    });

    suite.run("daylight-saving end day: the repeated hour lengthens the day", [&]() -> std::string {
        const TariffTables tables = tariffTables(timeOfUse(), account(), at("2027-04-04T01:00:00Z"), sydney);
        if (tables.individualDays->size() != 1) {
            return "expected one individual day";
        }
        const std::string elapsed = starts(tables, tables.individualDays->front().dayEntryIds);
        return elapsed == "[0,480,900,1260,1380]" ? "" : "elapsed start times " + elapsed;
    });

    suite.run("CurrentDayEntry and NextDayEntry across local midnight", [&]() -> std::string {
        const TariffTables late = tariffTables(timeOfUse(), account(), at("2026-10-01T13:30:00Z"), sydney);
        if (late.currentDayEntry->id != 1321 || late.currentDayEntryDate != matter("2026-10-01T12:00:00Z")) {
            return "at 23:30 the current entry is " + std::to_string(late.currentDayEntry->id);
        }
        if (late.nextDayEntry->id != 1 || late.nextDayEntryDate != matter("2026-10-01T14:00:00Z")) {
            return "at 23:30 the next entry is " + std::to_string(late.nextDayEntry->id);
        }
        if (late.currentDay->date != matter("2026-09-30T14:00:00Z") || late.nextDay->date != matter("2026-10-01T14:00:00Z")) {
            return "CurrentDay / NextDay dates are wrong";
        }
        if (late.currentComponentIds != late.nextComponentIds || late.nextLocalMidnight != at("2026-10-01T14:00:00Z")) {
            return "off-peak before and after midnight should share components";
        }
        const TariffTables early = tariffTables(timeOfUse(), account(), at("2026-10-01T14:10:00Z"), sydney);
        if (early.currentDayEntry->id != 1 || early.currentDayEntryDate != matter("2026-10-01T14:00:00Z") ||
            early.nextDayEntry->id != 421 || early.nextDayEntryDate != matter("2026-10-01T21:00:00Z")) {
            return "at 00:10 the entries are " + std::to_string(early.currentDayEntry->id) + " and " +
                   std::to_string(early.nextDayEntry->id);
        }
        if (early.currentDay->date != matter("2026-10-01T14:00:00Z")) {
            return "after midnight CurrentDay did not move";
        }
        if (early.currentComponentIds == early.nextComponentIds) {
            return "off-peak and shoulder share components";
        }
        return "";
    });

    suite.run("a wholesale-linked run gives a component with a null price", [&]() -> std::string {
        TariffGroup tariff = timeOfUse();
        tariff.spotLinked = true;
        tariff.schedule[2].segment.wholesaleLinked = true;
        const TariffTables tables = tariffTables(tariff, account(), noon, sydney);
        const std::vector<const TariffPeriodValue *> owners = periodsWithComponent(tables, (*tables.components)[2].id);
        if ((*tables.components)[2].price || !(*tables.components)[0].price || owners.size() != 1 ||
            owners.front()->label != "Peak") {
            return "the wholesale-linked component has a price";
        }
        if (findComponent(tables, 12345) != nullptr) {
            return "an unknown ID was found";
        }
        return "";
    });

    suite.run("an allowance gives two blocks with a threshold and BlockMode Individual", [&]() -> std::string {
        TariffGroup tariff = timeOfUse();
        Segment soak = segment(Band::offPeak, "Solar Soak", 0);
        soak.kwhLimit = 24;
        soak.rateAfterLimitKey = 275000000;
        soak.blocks = {RateBlock{0, 24.0, 0}, RateBlock{24, std::nullopt, 275000000}};
        tariff.schedule = {ScheduleEntry{0, 660, timeOfUse().schedule[0].segment}, ScheduleEntry{660, 840, soak},
                           ScheduleEntry{840, 1440, timeOfUse().schedule[0].segment}};
        const TariffTables tables = tariffTables(tariff, account(), noon, sydney);
        if (tables.info->blockMode != BlockMode::individual || tables.periods->size() != 2 ||
            (*tables.periods)[1].componentIds.size() != 2) {
            return "expected Individual block mode and a two-component soak period";
        }
        const TariffComponentValue *first = findComponent(tables, (*tables.periods)[1].componentIds[0]);
        const TariffComponentValue *second = findComponent(tables, (*tables.periods)[1].componentIds[1]);
        if (first->threshold != 24 || first->label != "Solar Soak (first 24 kWh per day)" || first->price->price != 0 ||
            first->price->priceLevel != 0) {
            return "first block: " + first->label.value_or("(null)");
        }
        if (second->threshold || second->label != "Solar Soak" || second->price->price != matterMoney(275000000) ||
            second->price->priceLevel != 2) {
            return "second block: " + second->label.value_or("(null)");
        }
        tariff.period.segment = soak;
        tariff.period.start = at("2026-10-01T01:00:00Z");
        tariff.nextChange = at("2026-10-01T04:00:00Z");
        const CommodityPriceValues price = commodityPrice(tariff, noon, sydney);
        if (!price.current || price.current->description != "First 24 kWh/day; then 27.5c") {
            return "description " + (price.current ? price.current->description : std::string("(no price)"));
        }
        return "";
    });

    suite.run("Commodity Price: flat plan starts at local midnight, forecast ends one second early", [&]() -> std::string {
        TariffGroup flat;
        const Segment anytime = segment(Band::anytime, "Anytime", 300000000);
        flat.schedule = {ScheduleEntry{0, 1440, anytime}};
        flat.period.segment = anytime;
        const CommodityPriceValues price = commodityPrice(flat, noon, sydney);
        if (!price.current || price.current->periodStart != matter("2026-09-30T14:00:00Z") || price.current->periodEnd ||
            !price.forecast.empty() || price.current->price != 3000000 || price.current->description != "Anytime") {
            return "flat-plan CurrentPrice is wrong";
        }
        TariffGroup tou = timeOfUse();
        tou.period.segment = tou.schedule[1].segment;
        tou.period.start = at("2026-09-30T21:00:00Z");
        tou.period.end = at("2026-10-01T04:00:00Z");
        tou.nextChange = tou.period.end;
        const CommodityPriceValues upcoming = commodityPrice(tou, noon, sydney);
        if (upcoming.forecast.size() != 4) {
            return "expected 4 forecast entries, got " + std::to_string(upcoming.forecast.size());
        }
        if (upcoming.forecast[0].periodStart != matter("2026-10-01T04:00:00Z") ||
            upcoming.forecast[0].periodEnd != matter("2026-10-01T10:00:00Z") - 1 ||
            upcoming.forecast[3].periodEnd != matter("2026-10-02T02:00:00Z") || upcoming.forecast[0].priceLevel != 2) {
            return "forecast periods are wrong";
        }
        tou.schedule[1].segment.wholesaleLinked = true;
        tou.period.segment.wholesaleLinked = true;
        const CommodityPriceValues linked = commodityPrice(tou, noon, sydney);
        return linked.current || !linked.forecast.empty() ? "a wholesale-linked period has a CurrentPrice" : "";
    });

    suite.run("a faulted tariff group gives every nullable member null", [&]() -> std::string {
        TariffGroup tariff = timeOfUse();
        tariff.fault = Fault{"tariff_gap", std::nullopt, std::nullopt, std::nullopt, std::string("minute 0")};
        const TariffTables tables = tariffTables(tariff, account(), noon, sydney);
        const bool allNull = !tables.info && !tables.startDate && !tables.dayEntries && !tables.dayPatterns &&
                             !tables.calendarPeriods && !tables.individualDays && !tables.currentDay && !tables.nextDay &&
                             !tables.currentDayEntry && !tables.currentDayEntryDate && !tables.nextDayEntry &&
                             !tables.nextDayEntryDate && !tables.components && !tables.periods &&
                             !tables.currentComponentIds && !tables.nextComponentIds && !tables.tableHash;
        const CommodityPriceValues price = commodityPrice(tariff, noon, sydney);
        return allNull && !price.current && price.forecast.empty() ? "" : "a faulted group published a value";
    });

    return suite.finish();
}
