#include <algorithm>
#include <array>
#include <ctime>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <esp_log.h>
#include <sdkconfig.h>

#include <platform/CHIPDeviceLayer.h>

#include "flipped/core/constants.h"
#include "flipped/core/tz_table.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

const char *TAG = "flipped_adapter";

struct Payload {
    std::array<std::optional<bool>, core::SWITCH_COUNT> switches;
    core::FlippedClusterValues cluster;
    EnergyPayload energy;
    std::optional<core::Instant> nextLocalMidnight;
#if CONFIG_FLIPPED_EVE_HISTORY
    EvePayload eve;
#endif
};

std::vector<std::string> reportedProblems;

void report(std::vector<std::string> &reported, const std::vector<std::string> &problems, const char *what)
{
    if (problems == reported) {
        return;
    }
    for (const std::string &problem : problems) {
        ESP_LOGE(TAG, "%s: %s", what, problem.c_str());
    }
    reported = problems;
}

#if CONFIG_FLIPPED_ENERGY_ENDPOINTS
std::vector<std::string> reportedEnergyProblems;
core::EnergyPublication previousEnergy;
std::optional<core::Instant> midnightTarget;

void append(std::vector<std::string> &into, const std::vector<std::string> &from)
{
    into.insert(into.end(), from.begin(), from.end());
}

EnergyPayload energyPayload(const Update &update)
{
    EnergyPayload payload;
    const core::Signals &signals = update.signals;
    std::vector<std::string> problems;
    if (!signals.energy.nmi.empty()) {
        payload.nmi = signals.energy.nmi;
    }
    if (!signals.tariff.fault) {
        require(update.now.has_value(), "the tariff group is ok but the update carries no clock");
        if (!signals.account.timeZone) {
            problems.push_back("Commodity Tariff and Commodity Price are null: account.timeZone is null");
        } else if (const std::optional<core::TimeZone> zone = core::timeZoneFor(*signals.account.timeZone)) {
            payload.tables = core::tariffTables(signals.tariff, signals.account, *update.now, *zone);
            payload.price = core::commodityPrice(signals.tariff, *update.now, *zone);
        } else {
            problems.push_back("Commodity Tariff and Commodity Price are null: account.timeZone " +
                               *signals.account.timeZone + " is not in the TZ table");
        }
    }
    payload.energy = core::energyPublication(signals.energy, update.ledger, update.ledgerStep, previousEnergy);
    previousEnergy = payload.energy;
    if (payload.tables.tableHash) {
        require(static_cast<bool>(hooks().tariffPublishedAt), "the tariffPublishedAt hook is not installed");
        const core::Instant publishedAt = hooks().tariffPublishedAt(*payload.tables.tableHash, *update.now);
        payload.metering = core::meteringAttribution(signals.energy, payload.tables, publishedAt);
    }
    append(problems, payload.tables.problems);
    append(problems, payload.price.problems);
    append(problems, payload.metering.problems);
    append(problems, payload.energy.problems);
    report(reportedEnergyProblems, problems, "EP 2 / EP 3");
    return payload;
}

void armMidnight();

void onMidnight(chip::System::Layer *, void *)
{
    if (!midnightTarget) {
        return;
    }
    const auto now = static_cast<core::Instant>(std::time(nullptr));
    if (now < *midnightTarget) {
        armMidnight();
        return;
    }
    midnightTarget.reset();
    require(static_cast<bool>(hooks().refresh), "the refresh hook is not installed");
    hooks().refresh();
}

void armMidnight()
{
    const auto now = static_cast<core::Instant>(std::time(nullptr));
    const int64_t seconds = std::clamp<int64_t>(*midnightTarget - now, 1, core::TIMER_MAX_AHEAD_S);
    checkChip(chip::DeviceLayer::SystemLayer().StartTimer(
                  chip::System::Clock::Milliseconds32(static_cast<uint32_t>(seconds * 1000)), onMidnight, nullptr),
              "SystemLayer StartTimer for local midnight");
}

void followMidnight(std::optional<core::Instant> target)
{
    if (target == midnightTarget) {
        return;
    }
    midnightTarget = target;
    if (!target) {
        chip::DeviceLayer::SystemLayer().CancelTimer(onMidnight, nullptr);
        return;
    }
    armMidnight();
}
#endif

void apply(intptr_t arg)
{
    std::unique_ptr<Payload> payload(reinterpret_cast<Payload *>(arg));
    applySwitches(payload->switches);
    applyFlippedEnergy(payload->cluster);
#if CONFIG_FLIPPED_ENERGY_ENDPOINTS
    if (virtualDevicesEnabled()) {
    applyCommodityTariff(std::move(payload->energy.tables));
    applyCommodityPrice(payload->energy.price);
    applyMeter(payload->energy.nmi, payload->energy.metering, payload->energy.energy);
    followMidnight(payload->nextLocalMidnight);
    }
#endif
#if CONFIG_FLIPPED_EVE_HISTORY
    applyEveHistory(std::move(payload->eve));
#endif
}

}

void publish(const Update &update)
{
    if (!update.signals.tariff.fault) {
        setSpotLinked(update.signals.tariff.spotLinked);
    }
    followInstance(update.instanceKey, !update.signals.account.fault);
    auto payload = std::make_unique<Payload>();
    payload->switches = core::switchValues(update.signals);
    payload->cluster = core::flippedClusterValues(update.signals, update.dailyLimitRemaining);
    report(reportedProblems, payload->cluster.problems, "Flipped Energy cluster");
#if CONFIG_FLIPPED_ENERGY_ENDPOINTS
    payload->energy = energyPayload(update);
    payload->nextLocalMidnight = payload->energy.tables.nextLocalMidnight;
#endif
#if CONFIG_FLIPPED_EVE_HISTORY
    payload->eve = evePayload(update);
#endif
    scheduleWork(apply, reinterpret_cast<intptr_t>(payload.release()), "publish");
}

}
