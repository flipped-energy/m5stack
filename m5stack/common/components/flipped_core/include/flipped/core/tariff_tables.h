#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flipped/core/signals.h"
#include "flipped/core/tz_table.h"
#include "flipped/core/types.h"

namespace flipped::core {

inline constexpr uint32_t INDIVIDUAL_DAY_ENTRY_BIT = 0x80000000;
inline constexpr uint8_t ALL_DAYS_OF_WEEK = 0x7F;
inline constexpr size_t TARIFF_TEXT_MAX_BYTES = 128;
inline constexpr size_t PRICE_DESCRIPTION_MAX_BYTES = 32;
inline constexpr size_t PRICE_FORECAST_MAX_ENTRIES = 56;
inline constexpr int64_t PRICE_FORECAST_HORIZON_S = 86400;
inline constexpr uint16_t CURRENCY_AUD = 36;
inline constexpr uint8_t CURRENCY_DECIMAL_POINTS = 7;
inline constexpr const char *TARIFF_PROVIDER_NAME = "Flipped Energy";

enum class BlockMode : uint8_t { noBlock = 0, combined = 1, individual = 2 };

struct DayEntryValue {
    uint32_t id = 0;
    uint16_t startTime = 0;

    bool operator==(const DayEntryValue &other) const { return id == other.id && startTime == other.startTime; }
    bool operator!=(const DayEntryValue &other) const { return !(*this == other); }
};

struct DayPatternValue {
    uint32_t id = 0;
    uint8_t daysOfWeek = 0;
    std::vector<uint32_t> dayEntryIds;

    bool operator==(const DayPatternValue &other) const
    {
        return id == other.id && daysOfWeek == other.daysOfWeek && dayEntryIds == other.dayEntryIds;
    }
    bool operator!=(const DayPatternValue &other) const { return !(*this == other); }
};

struct CalendarPeriodValue {
    uint32_t startDate = 0;
    std::vector<uint32_t> dayPatternIds;

    bool operator==(const CalendarPeriodValue &other) const
    {
        return startDate == other.startDate && dayPatternIds == other.dayPatternIds;
    }
    bool operator!=(const CalendarPeriodValue &other) const { return !(*this == other); }
};

struct DayValue {
    uint32_t date = 0;
    std::vector<uint32_t> dayEntryIds;

    bool operator==(const DayValue &other) const { return date == other.date && dayEntryIds == other.dayEntryIds; }
    bool operator!=(const DayValue &other) const { return !(*this == other); }
};

struct TariffPriceValue {
    int64_t price = 0;
    int16_t priceLevel = 0;

    bool operator==(const TariffPriceValue &other) const
    {
        return price == other.price && priceLevel == other.priceLevel;
    }
    bool operator!=(const TariffPriceValue &other) const { return !(*this == other); }
};

struct TariffComponentValue {
    uint32_t id = 0;
    std::optional<TariffPriceValue> price;
    std::optional<int64_t> threshold;
    std::optional<std::string> label;
    bool peak = false;

    bool operator==(const TariffComponentValue &other) const
    {
        return id == other.id && price == other.price && threshold == other.threshold && label == other.label &&
               peak == other.peak;
    }
    bool operator!=(const TariffComponentValue &other) const { return !(*this == other); }
};

struct TariffPeriodValue {
    std::optional<std::string> label;
    std::vector<uint32_t> dayEntryIds;
    std::vector<uint32_t> componentIds;

    bool operator==(const TariffPeriodValue &other) const
    {
        return label == other.label && dayEntryIds == other.dayEntryIds && componentIds == other.componentIds;
    }
    bool operator!=(const TariffPeriodValue &other) const { return !(*this == other); }
};

struct TariffInfoValue {
    std::optional<std::string> label;
    std::string providerName;
    BlockMode blockMode = BlockMode::noBlock;

    bool operator==(const TariffInfoValue &other) const
    {
        return label == other.label && providerName == other.providerName && blockMode == other.blockMode;
    }
    bool operator!=(const TariffInfoValue &other) const { return !(*this == other); }
};

struct TariffTables {
    std::optional<TariffInfoValue> info;
    std::optional<uint32_t> startDate;
    std::optional<std::vector<DayEntryValue>> dayEntries;
    std::optional<std::vector<DayPatternValue>> dayPatterns;
    std::optional<std::vector<CalendarPeriodValue>> calendarPeriods;
    std::optional<std::vector<DayValue>> individualDays;
    std::optional<DayValue> currentDay;
    std::optional<DayValue> nextDay;
    std::optional<DayEntryValue> currentDayEntry;
    std::optional<uint32_t> currentDayEntryDate;
    std::optional<DayEntryValue> nextDayEntry;
    std::optional<uint32_t> nextDayEntryDate;
    std::optional<std::vector<TariffComponentValue>> components;
    std::optional<std::vector<TariffPeriodValue>> periods;
    std::optional<std::vector<uint32_t>> currentComponentIds;
    std::optional<std::vector<uint32_t>> nextComponentIds;
    std::optional<uint32_t> tableHash;
    std::optional<Instant> nextLocalMidnight;
    std::vector<std::string> problems;
};

struct PriceValue {
    uint32_t periodStart = 0;
    std::optional<uint32_t> periodEnd;
    int64_t price = 0;
    int16_t priceLevel = 0;
    std::string description;

    bool operator==(const PriceValue &other) const
    {
        return periodStart == other.periodStart && periodEnd == other.periodEnd && price == other.price &&
               priceLevel == other.priceLevel && description == other.description;
    }
    bool operator!=(const PriceValue &other) const { return !(*this == other); }
};

struct CommodityPriceValues {
    std::optional<PriceValue> current;
    std::vector<PriceValue> forecast;
    std::vector<std::string> problems;
};

uint32_t fnv1a32(std::string_view bytes);
std::optional<Instant> localMidnight(std::string_view date, const TimeZone &zone);
TariffTables tariffTables(const TariffGroup &tariff, const AccountGroup &account, Instant now, const TimeZone &zone);
CommodityPriceValues commodityPrice(const TariffGroup &tariff, Instant now, const TimeZone &zone);
const TariffComponentValue *findComponent(const TariffTables &tables, uint32_t id);
std::vector<const TariffPeriodValue *> periodsWithComponent(const TariffTables &tables, uint32_t id);

}
