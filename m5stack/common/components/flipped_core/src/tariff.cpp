#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "flipped/core/constants.h"
#include "flipped/core/time.h"
#include "flipped/core/tz_table.h"
#include "groups.h"

namespace flipped::core::detail {

namespace {

constexpr int MINUTES_PER_DAY = 1440;

int64_t rateKey(double value)
{
    const double scaled = std::floor(std::fabs(value) * RATE_KEY_SCALE + 0.5);
    const int64_t magnitude = static_cast<int64_t>(scaled);
    return value < 0 ? -magnitude : magnitude;
}

int64_t floorToMinute(Instant instant)
{
    int64_t minutes = instant / 60;
    if (instant % 60 != 0 && instant < 0) {
        --minutes;
    }
    return minutes * 60;
}

struct FixedUnit {
    std::string id;
    std::string name;
    int start = 0;
    int end = 0;
    double from = 0;
    std::optional<double> to;
    int64_t key = 0;
    size_t order = 0;
};

struct SpotUnit {
    std::string id;
    int start = 0;
    int end = 0;
    bool withCap = false;
    std::optional<int64_t> capKey;
};

bool contains(int start, int end, int minute)
{
    if (start == end) {
        return true;
    }
    if (start < end) {
        return start <= minute && minute < end;
    }
    return minute >= start || minute < end;
}

std::string unitId(const BillingUnit &unit)
{
    return unit.billingUnitId.present() ? unit.billingUnitId.value : std::string("(no billingUnitId)");
}

bool absent(Presence presence)
{
    return presence == Presence::null;
}

std::optional<int> windowEdge(const Field<double> &field)
{
    if (!field.present() || field.value != std::floor(field.value) || field.value < 0 ||
        field.value > MINUTES_PER_DAY) {
        return std::nullopt;
    }
    return static_cast<int>(field.value);
}

std::string kwhText(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.15g", value);
    return text;
}

bool knownType(const std::string &type)
{
    static const char *const KNOWN[] = {"CertificateBillingUnit", "ControlledLoadBillingUnit", "FeedInTariff",
                                        "FixedBillingUnit",       "NetworkTariffBillingUnit",  "PeriodicBillingUnit",
                                        "PrepaidBillingUnit",     "SpotBillingUnit",           "SpotWithCapBillingUnit"};
    for (const char *known : KNOWN) {
        if (type == known) {
            return true;
        }
    }
    return false;
}

std::optional<std::string> planDateProblem(const char *label, const Field<Plan> &plan)
{
    if (plan.presence == Presence::null) {
        return std::nullopt;
    }
    if (plan.presence != Presence::present) {
        return std::string("product.") + label + " is not an object";
    }
    if (!plan.value.start.present() || !isWallDateTime(plan.value.start.value)) {
        return std::string("product.") + label + ".start " +
               (plan.value.start.present() ? plan.value.start.value : std::string("(absent)")) +
               " does not start with YYYY-MM-DDTHH:mm:ss";
    }
    if (!plan.value.end.present() || !isWallDateTime(plan.value.end.value)) {
        return std::string("product.") + label + ".end " +
               (plan.value.end.present() ? plan.value.end.value : std::string("(absent)")) +
               " does not start with YYYY-MM-DDTHH:mm:ss";
    }
    return std::nullopt;
}

bool inEffect(const Field<Plan> &plan, const LocalTime &local)
{
    return plan.present() && std::string_view(plan.value.start.value).substr(0, 19) <= local.wall() &&
           local.date() <= std::string_view(plan.value.end.value).substr(0, 10);
}

TariffResult faulted(Fault fault, std::optional<Instant> planChangeInstant = std::nullopt)
{
    TariffResult result;
    result.group.fault = std::move(fault);
    result.planChangeInstant = planChangeInstant;
    return result;
}

}

TariffResult tariffGroup(Instant instant, const Snapshot<AccountBody> &account, const Selection &selection)
{
    if (!account.body) {
        return faulted(unloadedFault(account));
    }
    if (instant - *account.fetchedAt >= ACCOUNT_MAX_AGE_S) {
        return faulted(account.error ? errorFault(*account.error) : makeFault("account_data_stale"));
    }
    if (selection.fault) {
        return faulted(*selection.fault);
    }
    const Account &selected = *selection.account;
    const Product &product = selected.product.value;
    if (!selected.accountState.present()) {
        return faulted(makeFault("invalid_response", "accountState is missing or not a string"));
    }
    if (selected.accountState.value != "ACTIVE" && selected.accountState.value != "CLOSING") {
        return faulted(makeFault("account_not_supplied", "accountState " + selected.accountState.value));
    }
    if (product.timeZone.presence == Presence::null) {
        return faulted(makeFault("timezone_missing", "product.timeZone is null or absent"));
    }
    if (!product.timeZone.present()) {
        return faulted(makeFault("invalid_response", "product.timeZone is not a string"));
    }
    const std::optional<TimeZone> zone = timeZoneFor(product.timeZone.value);
    if (!zone) {
        return faulted(makeFault("timezone_unsupported", "product.timeZone " + product.timeZone.value));
    }
    for (const auto &[label, plan] : {std::pair<const char *, const Field<Plan> *>{"currentPlan", &product.currentPlan},
                                      std::pair<const char *, const Field<Plan> *>{"upcomingPlan", &product.upcomingPlan}}) {
        if (std::optional<std::string> problem = planDateProblem(label, *plan)) {
            return faulted(makeFault("invalid_response", *problem));
        }
    }

    const LocalTime local = toLocal(instant, *zone);
    const Field<Plan> *plan = nullptr;
    if (inEffect(product.upcomingPlan, local)) {
        plan = &product.upcomingPlan;
    } else if (inEffect(product.currentPlan, local)) {
        plan = &product.currentPlan;
    }

    const Instant t0 = floorToMinute(instant);
    const LocalTime horizon = toLocal(t0 + PLAN_CHANGE_HORIZON_DAYS * 86400, *zone);
    const std::string horizonDate(horizon.date());
    std::vector<std::string> candidates;
    if (plan != nullptr) {
        const std::string endDate = plan->value.end.value.substr(0, 10);
        if (endDate < horizonDate) {
            candidates.push_back(nextDate(endDate) + "T00:00:00");
        }
    }
    if (product.upcomingPlan.present() && plan != &product.upcomingPlan) {
        const std::string start = product.upcomingPlan.value.start.value.substr(0, 19);
        if (std::string_view(start) > local.wall() && start.substr(0, 10) <= horizonDate) {
            candidates.push_back(start);
        }
    }
    std::optional<Instant> planChangeInstant;
    if (!candidates.empty()) {
        const std::string smallest = *std::min_element(candidates.begin(), candidates.end());
        planChangeInstant = localToInstant(smallest, *zone);
        if (!planChangeInstant) {
            return faulted(makeFault("local_time_nonexistent", "plan change at " + smallest));
        }
    }

    if (plan == nullptr) {
        return faulted(makeFault("plan_unavailable", "no plan in effect at " + std::string(local.wall())),
                       planChangeInstant);
    }
    if (!plan->value.billingUnits.present()) {
        return faulted(makeFault("invalid_response", "billingUnits is missing or not an array"), planChangeInstant);
    }

    std::vector<FixedUnit> fixed;
    std::vector<SpotUnit> spots;
    const std::vector<BillingUnit> &units = plan->value.billingUnits.value;
    for (size_t index = 0; index < units.size(); ++index) {
        const BillingUnit &unit = units[index];
        const std::string type = unit.billingUnitType.present() ? unit.billingUnitType.value : std::string();
        if (!unit.billingUnitType.present() || !knownType(type)) {
            if (!absent(unit.timeOfDayStartMinutes.presence) || !absent(unit.timeOfDayEndMinutes.presence) ||
                !absent(unit.chargePerKwh.presence) || !absent(unit.chargePerKwhIncludingGst.presence)) {
                return faulted(makeFault("billing_unit_unsupported",
                                         "billingUnitId " + unitId(unit) + " billingUnitType " +
                                             (unit.billingUnitType.present() ? type : std::string("null"))),
                               planChangeInstant);
            }
            continue;
        }
        const bool isFixed = type == "FixedBillingUnit";
        const bool isSpot = type == "SpotBillingUnit" || type == "SpotWithCapBillingUnit";
        if (!isFixed && !isSpot) {
            continue;
        }
        const std::optional<int> start = windowEdge(unit.timeOfDayStartMinutes);
        const std::optional<int> end = windowEdge(unit.timeOfDayEndMinutes);
        if (!start || !end) {
            return faulted(makeFault("billing_unit_malformed", "billingUnitId " + unitId(unit) + " time window"),
                           planChangeInstant);
        }
        if (isSpot) {
            SpotUnit spot;
            spot.id = unitId(unit);
            spot.start = *start;
            spot.end = *end;
            spot.withCap = type == "SpotWithCapBillingUnit";
            if (unit.capPerKwhIncludingGst.presence == Presence::wrongType) {
                return faulted(makeFault("billing_unit_malformed",
                                         "billingUnitId " + unitId(unit) + " capPerKwhIncludingGst"),
                               planChangeInstant);
            }
            if (spot.withCap && unit.capPerKwhIncludingGst.present()) {
                spot.capKey = rateKey(unit.capPerKwhIncludingGst.value);
            }
            spots.push_back(spot);
            continue;
        }
        if (!unit.chargePerKwhIncludingGst.present()) {
            return faulted(makeFault("billing_unit_malformed",
                                     "billingUnitId " + unitId(unit) + " chargePerKwhIncludingGst"),
                           planChangeInstant);
        }
        if (unit.kwhStart.presence == Presence::wrongType || unit.kwhEnd.presence == Presence::wrongType) {
            return faulted(makeFault("billing_unit_malformed", "billingUnitId " + unitId(unit) + " kWh block"),
                           planChangeInstant);
        }
        FixedUnit entry;
        entry.id = unitId(unit);
        entry.name = unit.name.present() ? unit.name.value : std::string();
        entry.start = *start;
        entry.end = *end;
        entry.from = unit.kwhStart.present() ? unit.kwhStart.value : 0;
        const bool unlimited = !unit.kwhEnd.present() || unit.kwhEnd.value == 0 ||
                               unit.kwhEnd.value >= static_cast<double>(KWH_UNLIMITED);
        if (!unlimited) {
            entry.to = unit.kwhEnd.value;
        }
        if (entry.from < 0 || (unit.kwhEnd.present() && unit.kwhEnd.value < 0) || (entry.to && *entry.to <= entry.from)) {
            return faulted(makeFault("billing_unit_malformed", "billingUnitId " + unitId(unit) + " kWh block"),
                           planChangeInstant);
        }
        entry.key = rateKey(unit.chargePerKwhIncludingGst.value);
        entry.order = index;
        fixed.push_back(entry);
    }

    std::vector<Segment> distinct;
    std::array<uint16_t, MINUTES_PER_DAY> segmentOf{};
    std::vector<const FixedUnit *> active;
    for (int minute = 0; minute < MINUTES_PER_DAY; ++minute) {
        active.clear();
        for (const FixedUnit &unit : fixed) {
            if (contains(unit.start, unit.end, minute)) {
                active.push_back(&unit);
            }
        }
        std::stable_sort(active.begin(), active.end(),
                         [](const FixedUnit *a, const FixedUnit *b) { return a->from < b->from; });
        std::vector<const SpotUnit *> spotActive;
        for (const SpotUnit &spot : spots) {
            if (contains(spot.start, spot.end, minute)) {
                spotActive.push_back(&spot);
            }
        }
        const std::string at = "minute " + std::to_string(minute);
        if (active.empty() && !spotActive.empty()) {
            std::string ids;
            for (const SpotUnit *spot : spotActive) {
                ids += ids.empty() ? "" : ", ";
                ids += spot->id;
            }
            return faulted(makeFault("spot_direction_unknown", at + ", units " + ids), planChangeInstant);
        }
        if (active.empty()) {
            return faulted(makeFault("tariff_gap", at), planChangeInstant);
        }
        if (active.front()->from != 0) {
            return faulted(makeFault("tariff_gap", at + ", kWh 0"), planChangeInstant);
        }
        for (size_t i = 1; i < active.size(); ++i) {
            const FixedUnit &previous = *active[i - 1];
            const FixedUnit &current = *active[i];
            if (!previous.to || current.from < *previous.to) {
                return faulted(makeFault("billing_unit_overlap", at + ", units " + previous.id + ", " + current.id),
                               planChangeInstant);
            }
            if (current.from > *previous.to) {
                return faulted(makeFault("tariff_gap", at + ", kWh " + kwhText(*previous.to)), planChangeInstant);
            }
        }
        if (active.back()->to) {
            return faulted(makeFault("tariff_gap", at + ", kWh " + kwhText(*active.back()->to)), planChangeInstant);
        }

        Segment segment;
        segment.name = active.front()->name;
        segment.rateKey = active.front()->key;
        for (const FixedUnit *unit : active) {
            segment.blocks.push_back(RateBlock{unit->from, unit->to, unit->key});
        }
        segment.kwhLimit = segment.blocks.front().toKwh;
        if (segment.blocks.size() > 1) {
            segment.rateAfterLimitKey = segment.blocks[1].rateKey;
        }
        segment.wholesaleLinked = !spotActive.empty();
        if (spotActive.size() == 1 && spotActive.front()->withCap && spotActive.front()->capKey) {
            segment.wholesaleCapKey = spotActive.front()->capKey;
        }
        size_t id = distinct.size();
        for (size_t i = distinct.size(); i-- > 0;) {
            if (distinct[i] == segment) {
                id = i;
                break;
            }
        }
        if (id == distinct.size()) {
            distinct.push_back(std::move(segment));
        }
        segmentOf[static_cast<size_t>(minute)] = static_cast<uint16_t>(id);
    }

    std::vector<int64_t> tiers;
    for (const Segment &segment : distinct) {
        tiers.push_back(segment.rateKey);
    }
    std::sort(tiers.begin(), tiers.end());
    tiers.erase(std::unique(tiers.begin(), tiers.end()), tiers.end());
    for (Segment &segment : distinct) {
        if (tiers.size() == 1) {
            segment.band = Band::anytime;
        } else if (segment.rateKey == tiers.front()) {
            segment.band = Band::offPeak;
        } else if (segment.rateKey == tiers.back()) {
            segment.band = Band::peak;
        } else {
            segment.band = Band::shoulder;
        }
    }

    const uint16_t now = segmentOf[static_cast<size_t>(toLocal(t0, *zone).minuteOfDay)];
    std::optional<Instant> periodEnd;
    for (int64_t k = 1; k <= SCAN_LIMIT_MIN; ++k) {
        const Instant candidate = t0 + k * 60;
        if (segmentOf[static_cast<size_t>(toLocal(candidate, *zone).minuteOfDay)] != now) {
            periodEnd = candidate;
            break;
        }
    }
    std::optional<Instant> periodStart;
    for (int64_t k = 0; k <= SCAN_LIMIT_MIN; ++k) {
        const Instant candidate = t0 - k * 60;
        if (segmentOf[static_cast<size_t>(toLocal(candidate - 60, *zone).minuteOfDay)] != now) {
            periodStart = candidate;
            break;
        }
    }
    std::optional<Instant> planStartInstant;
    const std::string planStart = plan->value.start.value.substr(0, 19);
    if (std::string_view(planStart).substr(0, 10) >= toLocal(t0 - SCAN_LIMIT_MIN * 60, *zone).date()) {
        planStartInstant = localToInstant(planStart, *zone);
        if (!planStartInstant) {
            return faulted(makeFault("local_time_nonexistent", "plan start at " + planStart), planChangeInstant);
        }
    }
    if (periodEnd && planChangeInstant) {
        periodEnd = std::min(*periodEnd, *planChangeInstant);
    }
    if (periodStart && planStartInstant) {
        periodStart = std::max(*periodStart, *planStartInstant);
    }

    TariffResult result;
    result.planChangeInstant = planChangeInstant;
    TariffGroup &group = result.group;
    group.structure = tiers.size() == 1 ? Structure::flat : Structure::timeOfUse;
    group.spotLinked = !spots.empty();
    group.period.segment = distinct[now];
    group.period.start = periodStart;
    group.period.end = periodEnd;
    group.peak = group.period.segment.band == Band::peak;
    group.offPeak = group.period.segment.band == Band::offPeak;
    if (periodEnd && planChangeInstant) {
        group.nextChange = std::min(*periodEnd, *planChangeInstant);
    } else if (periodEnd) {
        group.nextChange = periodEnd;
    } else {
        group.nextChange = planChangeInstant;
    }
    int runStart = 0;
    for (int minute = 1; minute <= MINUTES_PER_DAY; ++minute) {
        if (minute == MINUTES_PER_DAY ||
            segmentOf[static_cast<size_t>(minute)] != segmentOf[static_cast<size_t>(runStart)]) {
            group.schedule.push_back(ScheduleEntry{runStart, minute, distinct[segmentOf[static_cast<size_t>(runStart)]]});
            runStart = minute;
        }
    }
    return result;
}

}
