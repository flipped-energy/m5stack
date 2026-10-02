#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "flipped/core/time.h"
#include "flipped/core/tz_table.h"
#include "groups.h"

namespace flipped::core::detail {

namespace {

EnergyGroup faulted(Fault fault)
{
    EnergyGroup group;
    group.fault = std::move(fault);
    return group;
}

std::optional<double> costAud(const UsageBucket &bucket)
{
    if (bucket.exportGeneral.state == CostState::noRows && bucket.exportControlledLoad.state == CostState::noRows) {
        return std::nullopt;
    }
    double total = 0;
    for (const KeyCost *cost : {&bucket.exportGeneral, &bucket.exportControlledLoad}) {
        if (cost->state == CostState::null) {
            return std::nullopt;
        }
        if (cost->state == CostState::value) {
            total += cost->value;
        }
    }
    return total;
}

std::optional<double> feedInCreditAud(const UsageBucket &bucket)
{
    if (bucket.importGeneral.state != CostState::value) {
        return std::nullopt;
    }
    return -bucket.importGeneral.value;
}

EnergyEntry entryFor(const UsageBucket &bucket, Instant start, int durationMinutes)
{
    EnergyEntry entry;
    entry.local = bucket.time;
    entry.start = start;
    entry.durationMinutes = durationMinutes;
    entry.gridImportKwh = bucket.gridImportKwh;
    entry.controlledLoadKwh = bucket.controlledLoadKwh;
    entry.solarExportKwh = bucket.solarExportKwh;
    entry.costAud = costAud(bucket);
    entry.feedInCreditAud = feedInCreditAud(bucket);
    return entry;
}

void checkNmi(const Snapshot<UsageBody> &usage, const std::string &nmi)
{
    if (usage.body->nmi != nmi) {
        std::fprintf(stderr, "usage snapshot was parsed for nmi %s, the selected nmi is %s\n", usage.body->nmi.c_str(),
                     nmi.c_str());
        std::abort();
    }
}

struct Merge {
    const UsageBucket *earliest = nullptr;
    Instant start = 0;
    Instant end = 0;
    double gridImportKwh = 0;
    double controlledLoadKwh = 0;
    double solarExportKwh = 0;
    std::optional<double> costAud = 0.0;
    std::optional<double> feedInCreditAud = 0.0;
};

}

EnergyGroup energyGroup(const Config &config, const Snapshot<AccountBody> &account, const Selection &selection,
                        const Snapshot<MetersBody> &meters, const Snapshot<UsageBody> &halfHourly,
                        const Snapshot<UsageBody> &daily)
{
    if (!account.body) {
        return faulted(unloadedFault(account));
    }
    if (selection.fault) {
        return faulted(*selection.fault);
    }
    const Account &selected = *selection.account;
    const Field<std::string> &timeZone = selected.product.value.timeZone;
    if (timeZone.presence == Presence::null) {
        return faulted(makeFault("timezone_missing", "product.timeZone is null or absent"));
    }
    if (!timeZone.present()) {
        return faulted(makeFault("invalid_response", "product.timeZone is not a string"));
    }
    const std::optional<TimeZone> zone = timeZoneFor(timeZone.value);
    if (!zone) {
        return faulted(makeFault("timezone_unsupported", "product.timeZone " + timeZone.value));
    }
    for (const Account &other : account.body->accounts.value) {
        if (!other.accountNumber.present() || other.accountNumber.value.empty() || !other.product.present()) {
            continue;
        }
        const Field<std::string> &otherZone = other.product.value.timeZone;
        if (otherZone.presence != timeZone.presence || otherZone.value != timeZone.value) {
            return faulted(makeFault("usage_timezone_ambiguous",
                                     "accounts " + selected.accountNumber.value + " and " + other.accountNumber.value +
                                         " have different product.timeZone values"));
        }
    }
    if (!meters.body) {
        return faulted(unloadedFault(meters));
    }
    const NmiChoice choice = chooseNmi(config, selected, *meters.body);
    if (choice.fault) {
        return faulted(*choice.fault);
    }
    const std::string &nmi = *choice.nmi;
    if (!halfHourly.body) {
        return faulted(unloadedFault(halfHourly));
    }
    if (!daily.body) {
        return faulted(unloadedFault(daily));
    }
    checkNmi(halfHourly, nmi);
    checkNmi(daily, nmi);
    if (halfHourly.body->invalid) {
        return faulted(makeFault("invalid_response", *halfHourly.body->invalid));
    }

    EnergyGroup group;
    group.nmi = nmi;
    std::map<std::string, Merge> merges;
    group.intervals.reserve(halfHourly.body->buckets.size());
    for (const UsageBucket &bucket : halfHourly.body->buckets) {
        const std::vector<Instant> occurrences = localOccurrences(bucket.time, *zone);
        if (occurrences.empty()) {
            return faulted(makeFault("local_time_nonexistent", "usage bucket " + bucket.time));
        }
        if (occurrences.size() == 1) {
            group.intervals.push_back(entryFor(bucket, occurrences.front(), 30));
            continue;
        }
        Merge &merge = merges[bucket.time.substr(0, 10)];
        if (merge.earliest == nullptr || bucket.time < merge.earliest->time) {
            merge.earliest = &bucket;
            merge.start = occurrences.front();
        }
        merge.end = std::max(merge.end, occurrences.back() + 30 * 60);
        merge.gridImportKwh += bucket.gridImportKwh;
        merge.controlledLoadKwh += bucket.controlledLoadKwh;
        merge.solarExportKwh += bucket.solarExportKwh;
        const std::optional<double> cost = costAud(bucket);
        const std::optional<double> credit = feedInCreditAud(bucket);
        merge.costAud = merge.costAud && cost ? std::optional<double>(*merge.costAud + *cost) : std::nullopt;
        merge.feedInCreditAud =
            merge.feedInCreditAud && credit ? std::optional<double>(*merge.feedInCreditAud + *credit) : std::nullopt;
    }
    for (const auto &[date, merge] : merges) {
        EnergyEntry entry;
        entry.local = merge.earliest->time;
        entry.start = merge.start;
        entry.durationMinutes = static_cast<int>((merge.end - merge.start) / 60);
        entry.gridImportKwh = merge.gridImportKwh;
        entry.controlledLoadKwh = merge.controlledLoadKwh;
        entry.solarExportKwh = merge.solarExportKwh;
        entry.costAud = merge.costAud;
        entry.feedInCreditAud = merge.feedInCreditAud;
        group.intervals.push_back(entry);
    }

    if (daily.body->invalid) {
        return faulted(makeFault("invalid_response", *daily.body->invalid));
    }
    group.days.reserve(daily.body->buckets.size());
    for (const UsageBucket &bucket : daily.body->buckets) {
        const std::string date = bucket.time.substr(0, 10);
        const std::optional<Instant> start = localToInstant(date + "T00:00:00", *zone);
        if (!start) {
            return faulted(makeFault("local_time_nonexistent", "usage day " + date));
        }
        const std::string following = nextDate(date) + "T00:00:00";
        const std::optional<Instant> end = localToInstant(following, *zone);
        if (!end) {
            return faulted(makeFault("local_time_nonexistent", "usage day " + following));
        }
        group.days.push_back(entryFor(bucket, *start, static_cast<int>((*end - *start) / 60)));
    }

    const auto byStart = [](const EnergyEntry &a, const EnergyEntry &b) { return a.start < b.start; };
    std::stable_sort(group.intervals.begin(), group.intervals.end(), byStart);
    std::stable_sort(group.days.begin(), group.days.end(), byStart);
    if (!group.intervals.empty()) {
        group.latestIntervalEnd = group.intervals.back().start + group.intervals.back().durationMinutes * 60;
    }
    return group;
}

}
