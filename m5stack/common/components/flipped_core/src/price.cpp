#include <optional>
#include <string>
#include <utility>

#include "flipped/core/constants.h"
#include "flipped/core/time.h"
#include "groups.h"

namespace flipped::core::detail {

namespace {

bool knownTier(const std::string &tier)
{
    return tier == "UnusuallyLow" || tier == "Normal" || tier == "Elevated" || tier == "Spike";
}

std::optional<std::string> readInstant(const Field<std::string> &field, const std::string &label, Instant &out)
{
    if (!field.present()) {
        return label + " is missing or not a string";
    }
    const InstantText parsed = parseInstant(field.value);
    if (parsed.missingOffset) {
        return label + " " + field.value + " has no UTC offset";
    }
    if (!parsed.instant) {
        return label + " " + field.value + " is not an ISO-8601 date-time";
    }
    out = *parsed.instant;
    return std::nullopt;
}

std::optional<std::string> readTier(const Field<Assessment> &assessment, const std::string &label, std::string &out)
{
    if (!assessment.present()) {
        return label + " is missing or not an object";
    }
    if (!assessment.value.tier.present() || !knownTier(assessment.value.tier.value)) {
        return label + ".tier " +
               (assessment.value.tier.present() ? assessment.value.tier.value : std::string("(absent)")) +
               " is not a NemPriceTier";
    }
    out = assessment.value.tier.value;
    return std::nullopt;
}

std::optional<std::string> readForecast(const Field<Forecast> &field, const Field<Assessment> &peak,
                                        const std::string &label, std::optional<PriceForecast> &out)
{
    if (field.presence == Presence::null) {
        return std::nullopt;
    }
    if (!field.present()) {
        return "outlook." + label + " is missing or not an object";
    }
    const Forecast &forecast = field.value;
    if (!forecast.points.present()) {
        return "outlook." + label + ".points is missing or not an array";
    }
    if (forecast.points.value.empty()) {
        return std::nullopt;
    }
    PriceForecast result;
    if (std::optional<std::string> problem = readInstant(forecast.from, "outlook." + label + ".from", result.from)) {
        return problem;
    }
    if (std::optional<std::string> problem = readInstant(forecast.to, "outlook." + label + ".to", result.to)) {
        return problem;
    }
    if (std::optional<std::string> problem =
            readInstant(forecast.publishedAt, "outlook." + label + ".publishedAt", result.publishedAt)) {
        return problem;
    }
    if (!forecast.minCentsPerKwh.present() || !forecast.maxCentsPerKwh.present()) {
        return "outlook." + label + ".minCentsPerKwh or maxCentsPerKwh is missing or not a number";
    }
    result.minCentsPerKwh = forecast.minCentsPerKwh.value;
    result.maxCentsPerKwh = forecast.maxCentsPerKwh.value;
    if (std::optional<std::string> problem = readTier(peak, "outlook." + label + "Peak", result.tier)) {
        return problem;
    }
    for (const PricePoint &point : forecast.points.value) {
        ForecastPoint entry;
        if (std::optional<std::string> problem = readInstant(point.time, "outlook." + label + ".points[].time", entry.start)) {
            return problem;
        }
        if (!point.averageCentsPerKwh.present()) {
            return "outlook." + label + ".points[].averageCentsPerKwh is missing or not a number";
        }
        entry.centsPerKwh = point.averageCentsPerKwh.value;
        result.points.push_back(entry);
    }
    out = std::move(result);
    return std::nullopt;
}

std::optional<std::string> readOutlook(const OutlookBody &body, PriceGroup &group)
{
    if (!body.now.present()) {
        return std::string("outlook.now is missing or not an object");
    }
    if (std::optional<std::string> problem = readInstant(body.now.value.time, "outlook.now.time", group.intervalStart)) {
        return problem;
    }
    if (!body.now.value.averageCentsPerKwh.present()) {
        return std::string("outlook.now.averageCentsPerKwh is missing or not a number");
    }
    group.centsPerKwh = body.now.value.averageCentsPerKwh.value;
    if (std::optional<std::string> problem = readTier(body.nowAssessment, "outlook.nowAssessment", group.tier)) {
        return problem;
    }
    if (std::optional<std::string> problem = readForecast(body.nextHour, body.nextHourPeak, "nextHour", group.nextHour)) {
        return problem;
    }
    return readForecast(body.ahead, body.aheadPeak, "ahead", group.ahead);
}

PriceGroup faulted(Fault fault)
{
    PriceGroup group;
    group.fault = std::move(fault);
    return group;
}

}

PriceGroup priceGroup(Instant instant, const Config &config, const Snapshot<AccountBody> &account,
                      const Selection &selection, const Snapshot<OutlookBody> &outlook)
{
    if (!account.body) {
        return faulted(unloadedFault(account));
    }
    if (selection.fault) {
        return faulted(*selection.fault);
    }
    const Product &product = selection.account->product.value;
    if (product.gridType.presence == Presence::null) {
        return faulted(makeFault("region_missing", "product.gridType is null or absent"));
    }
    if (!product.gridType.present()) {
        return faulted(makeFault("invalid_response", "product.gridType is missing or not a string"));
    }
    if (!outlook.body) {
        return faulted(unloadedFault(outlook));
    }
    PriceGroup group;
    if (std::optional<std::string> problem = readOutlook(*outlook.body, group)) {
        return faulted(outlook.error ? errorFault(*outlook.error) : makeFault("invalid_response", *problem));
    }
    const Instant age = instant > group.intervalStart ? instant - group.intervalStart : 0;
    if (age >= PRICE_STALE_AFTER_S) {
        return faulted(outlook.error ? errorFault(*outlook.error) : makeFault("price_stale"));
    }
    group.negative = group.centsPerKwh < 0;
    std::optional<bool> highThreshold;
    std::optional<bool> lowThreshold;
    if (config.priceHighThresholdCentsPerKwh) {
        highThreshold = group.centsPerKwh >= *config.priceHighThresholdCentsPerKwh;
    }
    if (config.priceLowThresholdCentsPerKwh) {
        lowThreshold = group.centsPerKwh <= *config.priceLowThresholdCentsPerKwh;
    }
    const bool highTier = (group.tier == "Elevated" || group.tier == "Spike") && group.centsPerKwh >= 0;
    const bool lowTier = group.tier == "UnusuallyLow" || group.centsPerKwh < 0;
    group.priceHigh = highThreshold ? *highThreshold : highTier && !(lowThreshold && *lowThreshold);
    group.priceLow = lowThreshold ? *lowThreshold : lowTier && !(highThreshold && *highThreshold);
    return group;
}

}
