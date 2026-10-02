#include "flipped/core/flipped_cluster.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace flipped::core {

namespace {

class Converter {
public:
    explicit Converter(std::vector<std::string> &problems) : problems_(problems) {}

    std::optional<uint32_t> epoch(const char *what, std::optional<Instant> instant)
    {
        if (!instant) {
            return std::nullopt;
        }
        const Instant seconds = *instant - MATTER_EPOCH_UNIX_S;
        if (seconds < 0 || seconds >= std::numeric_limits<uint32_t>::max()) {
            problems_.push_back(std::string(what) + " " + std::to_string(*instant) +
                                " is outside the Matter epoch-s range");
            return std::nullopt;
        }
        return static_cast<uint32_t>(seconds);
    }

    template <typename T>
    std::optional<T> scaled(const char *what, std::optional<double> value, double factor)
    {
        if (!value) {
            return std::nullopt;
        }
        const double product = *value * factor;
        if (!std::isfinite(product) || product <= static_cast<double>(std::numeric_limits<T>::min()) ||
            product >= static_cast<double>(std::numeric_limits<T>::max())) {
            problems_.push_back(std::string(what) + " " + std::to_string(*value) + " does not fit the attribute");
            return std::nullopt;
        }
        return static_cast<T>(std::llround(product));
    }

    std::optional<uint32_t> wholeKwh(const char *what, std::optional<double> kwh)
    {
        if (!kwh) {
            return std::nullopt;
        }
        if (!(*kwh >= 0) || *kwh >= std::numeric_limits<uint32_t>::max() || std::floor(*kwh) != *kwh) {
            problems_.push_back(std::string(what) + " " + std::to_string(*kwh) + " is not a whole number of kWh");
            return std::nullopt;
        }
        return static_cast<uint32_t>(*kwh);
    }

    template <typename T>
    std::optional<T> bounded(const char *what, std::optional<int64_t> value)
    {
        if (!value) {
            return std::nullopt;
        }
        if (*value < 0 || *value >= static_cast<int64_t>(std::numeric_limits<T>::max())) {
            problems_.push_back(std::string(what) + " " + std::to_string(*value) + " does not fit the attribute");
            return std::nullopt;
        }
        return static_cast<T>(*value);
    }

    FaultValues fault(const char *group, const std::optional<Fault> &fault)
    {
        FaultValues values;
        if (!fault) {
            return values;
        }
        values.code = nonEmpty(utf8Prefix(fault->code, FLIPPED_CLUSTER_CODE_MAX_BYTES));
        if (fault->httpStatus) {
            values.httpStatus = bounded<uint16_t>(group, static_cast<int64_t>(*fault->httpStatus));
        }
        if (fault->body && !fault->body->empty()) {
            values.text = utf8Prefix(*fault->body, FLIPPED_CLUSTER_TEXT_MAX_BYTES);
        } else if (fault->message && !fault->message->empty()) {
            values.text = utf8Prefix(*fault->message, FLIPPED_CLUSTER_TEXT_MAX_BYTES);
        }
        if (fault->bodyBytes) {
            values.bodyBytes = bounded<uint32_t>(group, static_cast<int64_t>(*fault->bodyBytes));
        }
        return values;
    }

    static std::optional<std::string> nonEmpty(const std::string &text)
    {
        if (text.empty()) {
            return std::nullopt;
        }
        return text;
    }

private:
    std::vector<std::string> &problems_;
};

uint8_t ratePeriod(Band band)
{
    switch (band) {
    case Band::anytime:
        return 0;
    case Band::offPeak:
        return 1;
    case Band::shoulder:
        return 2;
    case Band::peak:
        return 3;
    }
    std::fprintf(stderr, "band %d has no RatePeriod value\n", static_cast<int>(band));
    std::abort();
}

std::optional<uint8_t> priceLevel(const std::string &tier)
{
    static constexpr const char *tiers[] = {"UnusuallyLow", "Normal", "Elevated", "Spike"};
    for (uint8_t i = 0; i < 4; ++i) {
        if (tier == tiers[i]) {
            return i;
        }
    }
    return std::nullopt;
}

}

int64_t matterMoney(int64_t rateKey)
{
    const int64_t magnitude = (rateKey < 0 ? -rateKey : rateKey);
    const int64_t money = (magnitude + 50) / 100;
    return rateKey < 0 ? -money : money;
}

std::string utf8Prefix(const std::string &text, size_t maxBytes)
{
    if (text.size() <= maxBytes) {
        return text;
    }
    size_t length = maxBytes;
    while (length > 0 && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
        --length;
    }
    return text.substr(0, length);
}

FlippedClusterValues flippedClusterValues(const Signals &signals, std::optional<int64_t> dailyLimitRemaining)
{
    FlippedClusterValues values;
    Converter convert(values.problems);

    const TariffGroup &tariff = signals.tariff;
    values.tariffFault = convert.fault("tariff.fault", tariff.fault);
    if (!tariff.fault) {
        const Segment &segment = tariff.period.segment;
        values.structure = tariff.structure == Structure::flat ? 0 : 1;
        values.spotLinked = tariff.spotLinked;
        values.ratePeriod = ratePeriod(segment.band);
        values.ratePeriodName = Converter::nonEmpty(utf8Prefix(segment.name, FLIPPED_CLUSTER_NAME_MAX_BYTES));
        if (!segment.wholesaleLinked) {
            values.currentRate = matterMoney(segment.rateKey);
        }
        values.rateAllowanceKwh = convert.wholeKwh("tariff.period.kwhLimit", segment.kwhLimit);
        if (segment.rateAfterLimitKey) {
            values.rateAfterAllowance = matterMoney(*segment.rateAfterLimitKey);
        }
        values.nextRateChange = convert.epoch("tariff.nextChange", tariff.nextChange);
        values.wholesaleLinkedRate = segment.wholesaleLinked;
        if (tariff.spotLinked) {
            values.fixedRateComponent = matterMoney(segment.rateKey);
        }
        if (segment.wholesaleCapKey) {
            values.wholesaleRateCap = matterMoney(*segment.wholesaleCapKey);
        }
    }

    const PriceGroup &price = signals.price;
    values.priceFault = convert.fault("price.fault", price.fault);
    if (!price.fault) {
        values.wholesalePrice = convert.scaled<int32_t>("price.centsPerKwh", price.centsPerKwh, 10000.0);
        values.wholesaleIntervalStart = convert.epoch("price.intervalStart", price.intervalStart);
        values.wholesalePriceLevel = priceLevel(price.tier);
        if (!values.wholesalePriceLevel) {
            values.problems.push_back("price.tier " + price.tier + " has no WholesalePriceLevel value");
        }
        values.wholesalePriceNegative = price.negative;
    }

    const EnergyGroup &energy = signals.energy;
    values.energyFault = convert.fault("energy.fault", energy.fault);
    if (!energy.fault) {
        values.latestIntervalEnd = convert.epoch("energy.latestIntervalEnd", energy.latestIntervalEnd);
        const auto newest = std::max_element(energy.days.begin(), energy.days.end(),
                                             [](const EnergyEntry &a, const EnergyEntry &b) { return a.start < b.start; });
        if (newest != energy.days.end()) {
            values.lastDayStart = convert.epoch("energy.days start", newest->start);
            values.lastDayGridImportMwh = convert.scaled<int64_t>("gridImportKwh", newest->gridImportKwh, 1e6);
            values.lastDayControlledLoadMwh =
                convert.scaled<int64_t>("controlledLoadKwh", newest->controlledLoadKwh, 1e6);
            values.lastDaySolarExportMwh = convert.scaled<int64_t>("solarExportKwh", newest->solarExportKwh, 1e6);
            values.lastDayUsageCostCents = convert.scaled<int32_t>("costAud", newest->costAud, 100.0);
            values.lastDayFeedInCreditCents = convert.scaled<int32_t>("feedInCreditAud", newest->feedInCreditAud, 100.0);
        }
    }

    const AccountGroup &account = signals.account;
    values.accountFault = convert.fault("account.fault", account.fault);
    if (!account.fault) {
        values.tokenExpiresAt = convert.epoch("account.tokenExpiresAt", account.tokenExpiresAt);
        values.tokenExpiringSoon = account.tokenExpiringSoon;
    }

    values.dailyLimitRemaining = convert.bounded<uint16_t>("X-DailyLimit-Remaining", dailyLimitRemaining);
    return values;
}

}
