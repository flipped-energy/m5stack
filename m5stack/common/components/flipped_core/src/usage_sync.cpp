#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>

#include "flipped/core/compute_signals.h"
#include "flipped/core/constants.h"
#include "flipped/core/sync.h"
#include "flipped/core/time.h"

namespace flipped::core {

namespace {

std::string shiftDate(std::string_view date, int days)
{
    const std::optional<Instant> midnight = wallAsUtc(std::string(date) + "T00:00:00");
    if (!midnight) {
        std::fprintf(stderr, "local date %.*s is not YYYY-MM-DD\n", static_cast<int>(date.size()), date.data());
        std::abort();
    }
    return formatInstant(*midnight + static_cast<Instant>(days) * 86400).substr(0, 10);
}

}

bool Engine::startUsage()
{
    const AccountFacts facts = accountFacts();
    if (!facts.zone) {
        platform_.log(LogLevel::warning, "usage sync not sent: the selected account has no supported time zone");
        return false;
    }
    const std::optional<std::string> nmi = selectedNmi(config_, snapshots_.account, snapshots_.meters);
    if (!nmi) {
        platform_.log(LogLevel::warning, "usage sync not sent: no NMI is selected");
        return false;
    }
    const LocalTime local = toLocal(clock(), *facts.zone);
    usageWindow_ = UsageWindow{shiftDate(local.date(), -usageLookbackDays_) + "T00:00:00",
                               nextDate(local.date()) + "T00:00:00", *nmi};
    return true;
}

Request Engine::usageRequest(Endpoint endpoint) const
{
    if (!usageWindow_) {
        std::fprintf(stderr, "usage request with no window\n");
        std::abort();
    }
    Request request;
    request.endpoint = endpoint;
    request.query.push_back(QueryParameter{"start", usageWindow_->start});
    request.query.push_back(QueryParameter{"end", usageWindow_->end});
    request.query.push_back(QueryParameter{"nmi", usageWindow_->nmi});
    request.timeoutSeconds = HTTP_TIMEOUT_S;
    request.nmi = usageWindow_->nmi;
    return request;
}

}

namespace flipped::core {

void Engine::onUsageRefresh()
{
    if (!started_) {
        platform_.log(LogLevel::info, "usage refresh not sent: start-up has not begun");
        return;
    }
    if (requestsState_ != RequestsState::running) {
        platform_.log(LogLevel::info,
                      std::string("usage refresh not sent: requests are ") + requestsStateName(requestsState_));
        return;
    }
    const Instant now = clock();
    if (lastUsageRefresh_ && now - *lastUsageRefresh_ < USAGE_REFRESH_MIN_INTERVAL_S) {
        platform_.log(LogLevel::info, "usage refresh not sent: the last one was " +
                                          std::to_string(now - *lastUsageRefresh_) + " s ago, the minimum is " +
                                          std::to_string(USAGE_REFRESH_MIN_INTERVAL_S) + " s");
        return;
    }
    const bool underWay = chainStep_ == Step::usage || chainStep_ == Step::usageDaily ||
                          std::find(chain_.begin(), chain_.end(), Step::usage) != chain_.end();
    if (underWay) {
        platform_.log(LogLevel::info, "usage refresh not sent: a usage sync is already under way");
        return;
    }
    lastUsageRefresh_ = now;
    chain_.push_back(Step::usage);
    runChain();
}

}
