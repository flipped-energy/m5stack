#include "flipped/core/tariff_tables.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

#include "flipped/core/flipped_cluster.h"
#include "flipped/core/tariff_projection.h"
#include "flipped/core/time.h"

namespace flipped::core {

namespace {

constexpr int MINUTES_PER_DAY = 1440;
constexpr Instant SECONDS_PER_DAY = 86400;
constexpr uint32_t INDIVIDUAL_DAY_HASH_MASK = 0x7FFFFFFF;

void putInt(std::string &key, int64_t value)
{
    const auto bits = static_cast<uint64_t>(value);
    for (int i = 0; i < 8; ++i) {
        key.push_back(static_cast<char>(bits >> (8 * i)));
    }
}

void putText(std::string &key, const std::optional<std::string> &text)
{
    if (!text) {
        key.push_back('\0');
        return;
    }
    key.push_back('\1');
    putInt(key, static_cast<int64_t>(text->size()));
    key += *text;
}

void putIds(std::string &key, const std::vector<uint32_t> &ids)
{
    putInt(key, static_cast<int64_t>(ids.size()));
    for (uint32_t id : ids) {
        putInt(key, id);
    }
}

[[noreturn]] void invalid(const std::string &what)
{
    std::fprintf(stderr, "%s\n", what.c_str());
    std::abort();
}

void requireTiling(const std::vector<ScheduleEntry> &schedule)
{
    int expected = 0;
    for (size_t i = 0; i < schedule.size(); ++i) {
        const ScheduleEntry &entry = schedule[i];
        if (entry.startMinute != expected || entry.endMinute <= entry.startMinute) {
            invalid("tariff.schedule entry " + std::to_string(i) + " covers minutes " +
                    std::to_string(entry.startMinute) + ".." + std::to_string(entry.endMinute) + " after minute " +
                    std::to_string(expected));
        }
        if (entry.segment.blocks.empty()) {
            invalid("tariff.schedule entry " + std::to_string(i) + " has no rate blocks");
        }
        if (entry.segment.kwhLimit && !entry.segment.rateAfterLimitKey) {
            invalid("tariff.schedule entry " + std::to_string(i) + " has a kwhLimit and no rate after it");
        }
        expected = entry.endMinute;
    }
    if (expected != MINUTES_PER_DAY) {
        invalid("tariff.schedule ends at minute " + std::to_string(expected));
    }
}

size_t runAt(const std::vector<ScheduleEntry> &schedule, int minute)
{
    for (size_t i = 0; i < schedule.size(); ++i) {
        if (minute >= schedule[i].startMinute && minute < schedule[i].endMinute) {
            return i;
        }
    }
    invalid("no tariff.schedule entry covers minute " + std::to_string(minute));
}

class Epochs {
public:
    explicit Epochs(std::vector<std::string> &problems) : problems_(problems) {}

    std::optional<uint32_t> operator()(const char *what, Instant instant)
    {
        const Instant seconds = instant - MATTER_EPOCH_UNIX_S;
        if (seconds < 0 || seconds >= std::numeric_limits<uint32_t>::max()) {
            problems_.push_back(std::string(what) + " " + std::to_string(instant) + " is outside the Matter epoch-s range");
            return std::nullopt;
        }
        return static_cast<uint32_t>(seconds);
    }

private:
    std::vector<std::string> &problems_;
};

class Ranks {
public:
    explicit Ranks(const std::vector<ScheduleEntry> &schedule)
    {
        for (const ScheduleEntry &entry : schedule) {
            keys_.push_back(entry.segment.rateKey);
        }
        std::sort(keys_.begin(), keys_.end());
        keys_.erase(std::unique(keys_.begin(), keys_.end()), keys_.end());
    }

    int16_t operator()(int64_t rateKey) const
    {
        return static_cast<int16_t>(std::lower_bound(keys_.begin(), keys_.end(), rateKey) - keys_.begin());
    }

private:
    std::vector<int64_t> keys_;
};

std::string number(double value)
{
    char text[48];
    for (int decimals = 0; decimals <= 6; ++decimals) {
        std::snprintf(text, sizeof text, "%.*f", decimals, value);
        if (std::strtod(text, nullptr) == value) {
            return text;
        }
    }
    std::snprintf(text, sizeof text, "%.6f", value);
    return text;
}

std::string cents(int64_t rateKey, int decimals)
{
    int64_t divisor = 1;
    for (int i = decimals; i < 7; ++i) {
        divisor *= 10;
    }
    const uint64_t magnitude = rateKey < 0 ? static_cast<uint64_t>(-(rateKey + 1)) + 1 : static_cast<uint64_t>(rateKey);
    const uint64_t scaled = (magnitude + static_cast<uint64_t>(divisor / 2)) / static_cast<uint64_t>(divisor);
    uint64_t unit = 1;
    for (int i = 0; i < decimals; ++i) {
        unit *= 10;
    }
    std::string text = (rateKey < 0 ? "-" : "") + std::to_string(scaled / unit);
    if (decimals > 0) {
        std::string fraction = std::to_string(scaled % unit);
        fraction.insert(0, static_cast<size_t>(decimals) - fraction.size(), '0');
        while (!fraction.empty() && fraction.back() == '0') {
            fraction.pop_back();
        }
        if (!fraction.empty()) {
            text += "." + fraction;
        }
    }
    return text;
}

std::string description(const Segment &segment)
{
    if (!segment.kwhLimit) {
        return utf8Prefix(segment.name, PRICE_DESCRIPTION_MAX_BYTES);
    }
    const std::string head = "First " + number(*segment.kwhLimit) + " kWh/day; then ";
    for (int decimals = 7; decimals >= 0; --decimals) {
        const std::string text = head + cents(*segment.rateAfterLimitKey, decimals) + "c";
        if (text.size() <= PRICE_DESCRIPTION_MAX_BYTES) {
            return text;
        }
    }
    return utf8Prefix(head + cents(*segment.rateAfterLimitKey, 0) + "c", PRICE_DESCRIPTION_MAX_BYTES);
}

std::string componentKey(const TariffComponentValue &component)
{
    std::string key = "TariffComponent";
    key.push_back(component.price ? '\1' : '\0');
    if (component.price) {
        putInt(key, 0);
        putInt(key, component.price->price);
        putInt(key, component.price->priceLevel);
    }
    key.push_back(component.threshold ? '\1' : '\0');
    if (component.threshold) {
        putInt(key, *component.threshold);
    }
    putText(key, component.label);
    key.push_back(component.peak ? '\1' : '\0');
    key.push_back('\0');
    return key;
}

struct DayPlan {
    std::string date;
    Instant midnight = 0;
    bool transition = false;
    std::vector<DayEntryValue> entries;
    std::vector<Instant> starts;
    std::vector<size_t> runs;
};

Instant requireMidnight(const std::string &date, const TimeZone &zone)
{
    const std::optional<Instant> midnight = localMidnight(date, zone);
    if (!midnight) {
        invalid("local midnight of " + date + " does not exist in " + std::string(zone.iana));
    }
    return *midnight;
}

DayPlan planDay(const std::string &date, const std::vector<ScheduleEntry> &schedule, const TimeZone &zone)
{
    DayPlan plan;
    plan.date = date;
    plan.midnight = requireMidnight(date, zone);
    const Instant end = requireMidnight(nextDate(date), zone);
    plan.transition = end - plan.midnight != SECONDS_PER_DAY;
    if (!plan.transition) {
        for (size_t i = 0; i < schedule.size(); ++i) {
            const uint16_t start = static_cast<uint16_t>(schedule[i].startMinute);
            plan.entries.push_back(DayEntryValue{static_cast<uint32_t>(start) + 1, start});
            plan.starts.push_back(plan.midnight + static_cast<Instant>(start) * 60);
            plan.runs.push_back(i);
        }
        return plan;
    }
    for (Instant instant = plan.midnight; instant < end; instant += 60) {
        const size_t run = runAt(schedule, toLocal(instant, zone).minuteOfDay);
        if (!plan.runs.empty() && plan.runs.back() == run) {
            continue;
        }
        const auto elapsed = static_cast<uint16_t>((instant - plan.midnight) / 60);
        std::string key = "IndividualDayEntry" + date;
        putInt(key, elapsed);
        const uint32_t id = INDIVIDUAL_DAY_ENTRY_BIT | (fnv1a32(key) & INDIVIDUAL_DAY_HASH_MASK);
        plan.entries.push_back(DayEntryValue{id, elapsed});
        plan.starts.push_back(instant);
        plan.runs.push_back(run);
    }
    return plan;
}

std::vector<uint32_t> idsOf(const DayPlan &plan)
{
    std::vector<uint32_t> ids;
    for (const DayEntryValue &entry : plan.entries) {
        ids.push_back(entry.id);
    }
    return ids;
}

}

uint32_t fnv1a32(std::string_view bytes)
{
    uint32_t hash = 0x811C9DC5u;
    for (char c : bytes) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 0x01000193u;
    }
    return hash;
}

std::optional<Instant> localMidnight(std::string_view date, const TimeZone &zone)
{
    return localToInstant(std::string(date) + "T00:00:00", zone);
}

TariffTables tariffTables(const TariffGroup &tariff, const AccountGroup &account, Instant now, const TimeZone &zone)
{
    TariffTables tables;
    if (tariff.fault) {
        return tables;
    }
    const std::vector<ScheduleEntry> &schedule = tariff.schedule;
    requireTiling(schedule);
    Epochs epoch(tables.problems);
    const Ranks rank(schedule);

    std::vector<size_t> groupOfRun;
    std::vector<size_t> groupLeader;
    for (size_t i = 0; i < schedule.size(); ++i) {
        size_t group = 0;
        while (group < groupLeader.size() && schedule[groupLeader[group]].segment != schedule[i].segment) {
            ++group;
        }
        if (group == groupLeader.size()) {
            groupLeader.push_back(i);
        }
        groupOfRun.push_back(group);
    }

    std::vector<TariffComponentValue> components;
    std::vector<TariffPeriodValue> periods;
    bool anyLimit = false;
    for (size_t leader : groupLeader) {
        const Segment &segment = schedule[leader].segment;
        anyLimit = anyLimit || segment.kwhLimit.has_value();
        TariffPeriodValue period;
        if (!segment.name.empty()) {
            period.label = utf8Prefix(segment.name, TARIFF_TEXT_MAX_BYTES);
        }
        for (const RateBlock &block : segment.blocks) {
            TariffComponentValue component;
            if (!segment.wholesaleLinked) {
                component.price = TariffPriceValue{matterMoney(block.rateKey), rank(block.rateKey)};
            }
            std::string label = segment.name;
            if (block.toKwh) {
                component.threshold = static_cast<int64_t>(std::llround(*block.toKwh));
                if (static_cast<double>(*component.threshold) != *block.toKwh) {
                    tables.problems.push_back("tariff block toKwh " + number(*block.toKwh) +
                                              " is not a whole number of kWh; Threshold " +
                                              std::to_string(*component.threshold));
                }
                const std::string suffix = "(first " + number(*block.toKwh) + " kWh per day)";
                label = label.empty() ? suffix : label + " " + suffix;
            }
            if (!label.empty()) {
                component.label = utf8Prefix(label, TARIFF_TEXT_MAX_BYTES);
            }
            component.peak = segment.band == Band::peak;
            component.id = fnv1a32(componentKey(component));
            if (component.id == 0) {
                invalid("TariffComponentID hash of " + label + " is 0");
            }
            const auto same = std::find_if(components.begin(), components.end(),
                                           [&](const TariffComponentValue &known) { return known.id == component.id; });
            if (same == components.end()) {
                components.push_back(component);
            } else if (*same != component) {
                invalid("TariffComponentID " + std::to_string(component.id) + " hashes two different components");
            }
            if (std::find(period.componentIds.begin(), period.componentIds.end(), component.id) ==
                period.componentIds.end()) {
                period.componentIds.push_back(component.id);
            }
        }
        periods.push_back(period);
    }

    std::vector<DayEntryValue> entries;
    std::vector<uint32_t> runIds;
    for (size_t i = 0; i < schedule.size(); ++i) {
        const uint16_t start = static_cast<uint16_t>(schedule[i].startMinute);
        entries.push_back(DayEntryValue{static_cast<uint32_t>(start) + 1, start});
        runIds.push_back(static_cast<uint32_t>(start) + 1);
        periods[groupOfRun[i]].dayEntryIds.push_back(static_cast<uint32_t>(start) + 1);
    }

    DayPatternValue pattern;
    pattern.daysOfWeek = ALL_DAYS_OF_WEEK;
    pattern.dayEntryIds = runIds;
    std::string patternKey = "DayPattern";
    putInt(patternKey, pattern.daysOfWeek);
    putIds(patternKey, pattern.dayEntryIds);
    pattern.id = fnv1a32(patternKey);

    TariffInfoValue info;
    if (account.productName && !account.productName->empty()) {
        info.label = utf8Prefix(*account.productName, TARIFF_TEXT_MAX_BYTES);
    }
    info.providerName = TARIFF_PROVIDER_NAME;
    info.blockMode = anyLimit ? BlockMode::individual : BlockMode::noBlock;

    std::string tableKey = "CommodityTariff";
    putText(tableKey, info.label);
    putText(tableKey, info.providerName);
    putInt(tableKey, static_cast<int64_t>(info.blockMode));
    for (const DayEntryValue &entry : entries) {
        putInt(tableKey, entry.id);
        putInt(tableKey, entry.startTime);
    }
    putInt(tableKey, pattern.id);
    for (const TariffComponentValue &component : components) {
        tableKey += componentKey(component);
    }
    for (const TariffPeriodValue &period : periods) {
        putText(tableKey, period.label);
        putIds(tableKey, period.dayEntryIds);
        putIds(tableKey, period.componentIds);
    }

    const std::string today(toLocal(now, zone).date());
    const std::string tomorrow = nextDate(today);
    const DayPlan current = planDay(today, schedule, zone);
    const DayPlan next = planDay(tomorrow, schedule, zone);

    std::vector<DayValue> individualDays;
    for (const DayPlan *plan : {&current, &next}) {
        if (!plan->transition) {
            continue;
        }
        const std::optional<uint32_t> date = epoch("individual day", plan->midnight);
        if (!date) {
            TariffTables unavailable;
            unavailable.problems = tables.problems;
            return unavailable;
        }
        individualDays.push_back(DayValue{*date, idsOf(*plan)});
        for (size_t k = 0; k < plan->entries.size(); ++k) {
            const DayEntryValue &entry = plan->entries[k];
            const bool clash = std::any_of(entries.begin(), entries.end(),
                                           [&](const DayEntryValue &known) { return known.id == entry.id; });
            if (clash) {
                invalid("DayEntryID " + std::to_string(entry.id) + " of " + plan->date + " is not unique");
            }
            entries.push_back(entry);
            periods[groupOfRun[plan->runs[k]]].dayEntryIds.push_back(entry.id);
        }
    }

    size_t currentIndex = 0;
    while (currentIndex + 1 < current.starts.size() && current.starts[currentIndex + 1] <= now) {
        ++currentIndex;
    }
    const bool nextIsTomorrow = currentIndex + 1 == current.entries.size();
    const DayPlan &nextPlan = nextIsTomorrow ? next : current;
    const size_t nextIndex = nextIsTomorrow ? 0 : currentIndex + 1;

    const std::optional<uint32_t> currentDate = epoch("CurrentDay date", current.midnight);
    const std::optional<uint32_t> nextDayDate = epoch("NextDay date", next.midnight);
    const std::optional<uint32_t> currentEntryDate = epoch("CurrentDayEntryDate", current.starts[currentIndex]);
    const std::optional<uint32_t> nextEntryDate = epoch("NextDayEntryDate", nextPlan.starts[nextIndex]);
    if (!currentDate || !nextDayDate || !currentEntryDate || !nextEntryDate) {
        TariffTables unavailable;
        unavailable.problems = tables.problems;
        return unavailable;
    }

    tables.info = info;
    tables.startDate = 0;
    tables.dayEntries = entries;
    tables.dayPatterns = std::vector<DayPatternValue>{pattern};
    tables.calendarPeriods = std::vector<CalendarPeriodValue>{CalendarPeriodValue{0, {pattern.id}}};
    tables.individualDays = individualDays;
    tables.currentDay = DayValue{*currentDate, idsOf(current)};
    tables.nextDay = DayValue{*nextDayDate, idsOf(next)};
    tables.currentDayEntry = current.entries[currentIndex];
    tables.currentDayEntryDate = currentEntryDate;
    tables.nextDayEntry = nextPlan.entries[nextIndex];
    tables.nextDayEntryDate = nextEntryDate;
    tables.components = components;
    tables.currentComponentIds = periods[groupOfRun[current.runs[currentIndex]]].componentIds;
    tables.nextComponentIds = periods[groupOfRun[nextPlan.runs[nextIndex]]].componentIds;
    tables.periods = periods;
    tables.tableHash = fnv1a32(tableKey);
    tables.nextLocalMidnight = next.midnight;
    return tables;
}

CommodityPriceValues commodityPrice(const TariffGroup &tariff, Instant now, const TimeZone &zone)
{
    CommodityPriceValues values;
    if (tariff.fault) {
        return values;
    }
    requireTiling(tariff.schedule);
    const Segment &segment = tariff.period.segment;
    if (segment.wholesaleLinked) {
        return values;
    }
    Epochs epoch(values.problems);
    const Ranks rank(tariff.schedule);
    const Instant start =
        tariff.period.start ? *tariff.period.start : requireMidnight(std::string(toLocal(now, zone).date()), zone);
    const std::optional<uint32_t> periodStart = epoch("CurrentPrice PeriodStart", start);
    std::optional<uint32_t> periodEnd;
    if (tariff.nextChange) {
        periodEnd = epoch("CurrentPrice PeriodEnd", *tariff.nextChange);
        if (!periodEnd) {
            return values;
        }
    }
    if (!periodStart) {
        return values;
    }
    values.current = PriceValue{*periodStart, periodEnd, matterMoney(segment.rateKey), rank(segment.rateKey),
                                description(segment)};
    if (!periodEnd) {
        return values;
    }
    const std::vector<ProjectedPeriod> projected = tariffProjection(now, PRICE_FORECAST_HORIZON_S, tariff, zone);
    const size_t count = std::min(projected.size() > 0 ? projected.size() - 1 : 0, PRICE_FORECAST_MAX_ENTRIES);
    for (size_t i = 1; i <= count; ++i) {
        const ProjectedPeriod &period = projected[i];
        const Segment &upcoming = tariff.schedule[period.scheduleIndex].segment;
        const Instant end = i < count ? projected[i + 1].start - 1 : period.end;
        const std::optional<uint32_t> forecastStart = epoch("PriceForecast PeriodStart", period.start);
        const std::optional<uint32_t> forecastEnd = epoch("PriceForecast PeriodEnd", end);
        if (!forecastStart || !forecastEnd) {
            values.forecast.clear();
            return values;
        }
        values.forecast.push_back(PriceValue{*forecastStart, forecastEnd, matterMoney(upcoming.rateKey),
                                             rank(upcoming.rateKey), description(upcoming)});
    }
    return values;
}

const TariffComponentValue *findComponent(const TariffTables &tables, uint32_t id)
{
    if (!tables.components) {
        return nullptr;
    }
    for (const TariffComponentValue &component : *tables.components) {
        if (component.id == id) {
            return &component;
        }
    }
    return nullptr;
}

std::vector<const TariffPeriodValue *> periodsWithComponent(const TariffTables &tables, uint32_t id)
{
    std::vector<const TariffPeriodValue *> found;
    if (!tables.periods) {
        return found;
    }
    for (const TariffPeriodValue &period : *tables.periods) {
        if (std::find(period.componentIds.begin(), period.componentIds.end(), id) != period.componentIds.end()) {
            found.push_back(&period);
        }
    }
    return found;
}

}
