#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "flipped/core/types.h"

namespace flipped::core {

struct AccountGroup {
    std::optional<Fault> fault;
    std::optional<std::string> accountNumber;
    std::optional<std::string> accountState;
    std::optional<std::string> productName;
    std::optional<std::string> region;
    std::optional<std::string> timeZone;
    std::optional<Instant> tokenExpiresAt;
    std::optional<std::string> tokenScope;
    std::optional<bool> tokenExpiringSoon;
};

enum class Band : uint8_t { anytime, offPeak, peak, shoulder };
enum class Structure : uint8_t { flat, timeOfUse };

struct RateBlock {
    double fromKwh = 0;
    std::optional<double> toKwh;
    int64_t rateKey = 0;

    bool operator==(const RateBlock &other) const
    {
        return fromKwh == other.fromKwh && toKwh == other.toKwh && rateKey == other.rateKey;
    }
};

struct Segment {
    Band band = Band::anytime;
    std::string name;
    int64_t rateKey = 0;
    std::optional<double> kwhLimit;
    std::optional<int64_t> rateAfterLimitKey;
    std::vector<RateBlock> blocks;
    bool wholesaleLinked = false;
    std::optional<int64_t> wholesaleCapKey;

    bool operator==(const Segment &other) const
    {
        return band == other.band && name == other.name && rateKey == other.rateKey && kwhLimit == other.kwhLimit &&
               rateAfterLimitKey == other.rateAfterLimitKey && blocks == other.blocks &&
               wholesaleLinked == other.wholesaleLinked && wholesaleCapKey == other.wholesaleCapKey;
    }
    bool operator!=(const Segment &other) const { return !(*this == other); }
};

struct Period {
    Segment segment;
    std::optional<Instant> start;
    std::optional<Instant> end;
};

struct ScheduleEntry {
    int startMinute = 0;
    int endMinute = 0;
    Segment segment;
};

struct TariffGroup {
    std::optional<Fault> fault;
    Structure structure = Structure::flat;
    bool spotLinked = false;
    bool peak = false;
    bool offPeak = false;
    Period period;
    std::optional<Instant> nextChange;
    std::optional<Instant> planChangeInstant;
    std::vector<ScheduleEntry> schedule;
};

struct ForecastPoint {
    Instant start = 0;
    double centsPerKwh = 0;
};

struct PriceForecast {
    Instant from = 0;
    Instant to = 0;
    Instant publishedAt = 0;
    double minCentsPerKwh = 0;
    double maxCentsPerKwh = 0;
    std::string tier;
    std::vector<ForecastPoint> points;
};

struct PriceGroup {
    std::optional<Fault> fault;
    double centsPerKwh = 0;
    Instant intervalStart = 0;
    std::string tier;
    bool priceHigh = false;
    bool priceLow = false;
    bool negative = false;
    std::optional<PriceForecast> nextHour;
    std::optional<PriceForecast> ahead;
};

struct EnergyEntry {
    std::string local;
    Instant start = 0;
    int durationMinutes = 0;
    double gridImportKwh = 0;
    double controlledLoadKwh = 0;
    double solarExportKwh = 0;
    std::optional<double> costAud;
    std::optional<double> feedInCreditAud;
};

struct EnergyGroup {
    std::optional<Fault> fault;
    std::string nmi;
    std::vector<EnergyEntry> intervals;
    std::vector<EnergyEntry> days;
    std::optional<Instant> latestIntervalEnd;
};

struct Signals {
    AccountGroup account;
    TariffGroup tariff;
    PriceGroup price;
    EnergyGroup energy;
    std::optional<Instant> nextEvaluation;
};

}
