#include <cstdio>
#include <cstdlib>
#include <string>

#include "flipped/core/constants.h"
#include "flipped/core/sync.h"
#include "flipped/core/time.h"

namespace flipped::core {

namespace {

Instant nextLocal(Instant now, const TimeZone &zone, int minuteOfDay)
{
    char time[32];
    std::snprintf(time, sizeof time, "T%02d:%02d:00", minuteOfDay / 60, minuteOfDay % 60);
    std::string date(toLocal(now, zone).date());
    for (int day = 0; day < 3; ++day) {
        const std::optional<Instant> at = localToInstant(date + time, zone);
        if (at && *at > now) {
            return *at;
        }
        date = nextDate(date);
    }
    std::fprintf(stderr, "no local %s after %lld in %s\n", time, static_cast<long long>(now), zone.posix);
    std::abort();
}

Request plainRequest(Endpoint endpoint)
{
    Request request;
    request.endpoint = endpoint;
    request.timeoutSeconds = HTTP_TIMEOUT_S;
    return request;
}

template <typename Body>
bool failedAtLastAttempt(const Snapshot<Body> &snapshot)
{
    return snapshot.error.has_value() || !snapshot.body.has_value();
}

bool refusedUntilRestart(const Response &response)
{
    return response.kind == ResponseKind::http &&
           (response.status == 400 ||
            (response.status == 404 && gatewayErrorCode(response.errorBody) == std::optional<std::string>("unknown_operation")));
}

}

void Engine::armSyncPoints()
{
    const AccountFacts facts = accountFacts();
    if (!facts.zone) {
        platform_.log(LogLevel::warning, "sync points not armed: the selected account has no supported time zone");
        return;
    }
    const Instant now = clock();
    timers_.arm(TimerId::accountSync, nextLocal(now, *facts.zone, ACCOUNT_SYNC_MINUTE_OF_DAY));
    timers_.arm(TimerId::usageSync, nextLocal(now, *facts.zone, USAGE_SYNC_MINUTE_OF_DAY));
}

void Engine::onAccountSyncPoint()
{
    armSyncPoints();
    if (requestsState_ == RequestsState::stoppedUntilAccountSync) {
        chain_.clear();
        chain_.push_back(Step::recoveryAccount);
        chain_.push_back(Step::meters);
        chain_.push_back(Step::tokens);
        chain_.push_back(Step::price);
        chain_.push_back(Step::usage);
        runChain();
        return;
    }
    if (requestsState_ != RequestsState::running) {
        return;
    }
    chain_.push_back(Step::account);
    chain_.push_back(Step::meters);
    chain_.push_back(Step::tokens);
    chain_.push_back(Step::usage);
    runChain();
}

void Engine::onUsageSyncPoint()
{
    armSyncPoints();
    if (requestsState_ != RequestsState::running) {
        return;
    }
    if (accountPartFailed()) {
        chain_.push_back(Step::account);
        chain_.push_back(Step::meters);
        chain_.push_back(Step::tokens);
    }
    chain_.push_back(Step::usage);
    runChain();
}

bool Engine::accountPartFailed() const
{
    return failedAtLastAttempt(snapshots_.account) || failedAtLastAttempt(snapshots_.meters) ||
           failedAtLastAttempt(snapshots_.tokens);
}

void Engine::onAccountStepSuccess()
{
    armSyncPoints();
    const AccountFacts facts = accountFacts();
    if (!facts.region) {
        return;
    }
    if (priceLoop_.running() && priceLoop_.region() != *facts.region) {
        platform_.log(LogLevel::info, "price loop region changed from " + priceLoop_.region() + " to " + *facts.region);
        priceLoop_.stop();
        priceLoop_.start(*facts.region);
        return;
    }
    if (!priceLoop_.running() && !priceLoop_.refused()) {
        priceLoop_.start(*facts.region);
    }
}

void Engine::sendStep(Step step, Request request)
{
    if (refused_[static_cast<size_t>(request.endpoint)]) {
        platform_.log(LogLevel::warning, std::string(endpointPath(request.endpoint)) +
                                             " not sent: it answered 400 or unknown_operation before this restart");
        return;
    }
    chainStep_ = step;
    send(Owner::chain, std::move(request));
}

void Engine::runChain()
{
    while (!chainStep_ && !chain_.empty()) {
        const Step step = chain_.front();
        chain_.pop_front();
        switch (step) {
        case Step::startupAccount:
        case Step::recoveryAccount:
        case Step::account:
            sendStep(step, plainRequest(Endpoint::account));
            break;
        case Step::meters:
            sendStep(step, plainRequest(Endpoint::meters));
            break;
        case Step::tokens:
            sendStep(step, plainRequest(Endpoint::tokens));
            break;
        case Step::price: {
            const AccountFacts facts = accountFacts();
            if (!facts.region) {
                platform_.log(LogLevel::warning, "price loop not started: the selected account has no region");
            } else if (requestsState_ == RequestsState::running && !priceLoop_.running()) {
                priceLoop_.start(*facts.region);
            }
            break;
        }
        case Step::usage:
            if (startUsage()) {
                chain_.push_front(Step::usageDaily);
                sendStep(step, usageRequest(Endpoint::usageHalfHourly));
            }
            break;
        case Step::usageDaily:
            sendStep(step, usageRequest(Endpoint::usageDaily));
            break;
        }
    }
}

void Engine::onChainResponse(const Request &request, const Response &response)
{
    if (!chainStep_) {
        std::fprintf(stderr, "chain response with no chain step\n");
        std::abort();
    }
    const Step step = *chainStep_;
    chainStep_.reset();
    const bool ok = response.kind == ResponseKind::body;
    switch (step) {
    case Step::startupAccount:
        onStartUpAccount(response);
        break;
    case Step::recoveryAccount:
        if (ok) {
            requestsState_ = RequestsState::running;
            platform_.log(LogLevel::info, "requests running again");
            armSyncPoints();
        } else {
            chain_.clear();
        }
        break;
    case Step::account:
        if (ok) {
            onAccountStepSuccess();
        }
        break;
    case Step::usageDaily:
        ++usageSyncs_;
        usageWindow_.reset();
        break;
    case Step::meters:
    case Step::tokens:
    case Step::price:
    case Step::usage:
        break;
    }
    if (step != Step::startupAccount && refusedUntilRestart(response)) {
        refused_[static_cast<size_t>(request.endpoint)] = true;
    }
    runChain();
}

}
