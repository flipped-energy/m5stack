#include "flipped/core/sync.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <variant>

#include "flipped/core/compute_signals.h"
#include "flipped/core/constants.h"
#include "groups.h"

namespace flipped::core {

namespace {

SnapshotError errorOf(const Response &response)
{
    SnapshotError error;
    switch (response.kind) {
    case ResponseKind::http:
        error.kind = ErrorKind::http;
        error.httpStatus = response.status;
        error.body = response.errorBody;
        error.bodyBytes = response.errorBodyBytes;
        return error;
    case ResponseKind::network:
        error.kind = ErrorKind::network;
        error.message = response.message;
        return error;
    case ResponseKind::invalid:
        error.kind = ErrorKind::invalid;
        error.message = response.message;
        return error;
    case ResponseKind::body:
    case ResponseKind::noContent:
        break;
    }
    std::fprintf(stderr, "no snapshot error for a response of kind %d\n", static_cast<int>(response.kind));
    std::abort();
}

template <typename Body>
void apply(Snapshot<Body> &snapshot, const Response &response, Instant now)
{
    if (response.kind != ResponseKind::body) {
        snapshot.error = errorOf(response);
        return;
    }
    const Body *body = std::get_if<Body>(&response.body);
    if (body == nullptr) {
        std::fprintf(stderr, "response body type %zu does not match its endpoint\n", response.body.index());
        std::abort();
    }
    snapshot.body = *body;
    snapshot.fetchedAt = now;
    snapshot.error.reset();
}

void applyUsage(Snapshot<UsageBody> &snapshot, Response &response, Instant now)
{
    if (response.kind != ResponseKind::body) {
        snapshot.error = errorOf(response);
        return;
    }
    UsageBody *body = std::get_if<UsageBody>(&response.body);
    if (body == nullptr) {
        std::fprintf(stderr, "response body type %zu does not match its endpoint\n", response.body.index());
        std::abort();
    }
    snapshot.body = std::move(*body);
    snapshot.fetchedAt = now;
    snapshot.error.reset();
}

std::optional<int64_t> wholeNumber(const std::optional<std::string> &text)
{
    if (!text || text->empty() || text->size() > 12) {
        return std::nullopt;
    }
    int64_t value = 0;
    for (const char c : *text) {
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        value = value * 10 + (c - '0');
    }
    return value;
}

}

const char *requestsStateName(RequestsState state)
{
    switch (state) {
    case RequestsState::running:
        return "running";
    case RequestsState::stoppedTokenRejected:
        return "stoppedTokenRejected";
    case RequestsState::stoppedUntilAccountSync:
        return "stoppedUntilAccountSync";
    }
    std::abort();
}

std::optional<std::string> tokenPreview(std::string_view token)
{
    constexpr size_t HEAD = 8;
    constexpr size_t TAIL = 4;
    if (token.size() < HEAD + TAIL) {
        return std::nullopt;
    }
    return std::string(token.substr(0, HEAD)) + "\xE2\x80\xA6" + std::string(token.substr(token.size() - TAIL));
}

Engine::Engine(Platform &platform, Config config, int usageLookbackDays)
    : platform_(platform), config_(std::move(config)), usageLookbackDays_(usageLookbackDays), timers_(platform),
      priceLoop_(*this)
{
    if (usageLookbackDays_ < USAGE_LOOKBACK_DAYS_MIN || usageLookbackDays_ > USAGE_LOOKBACK_DAYS_MAX) {
        std::fprintf(stderr, "usage look-back of %d days is outside %d..%d\n", usageLookbackDays_, USAGE_LOOKBACK_DAYS_MIN,
                     USAGE_LOOKBACK_DAYS_MAX);
        std::abort();
    }
    recompute();
}

Instant Engine::clock()
{
    const std::optional<Instant> now = platform_.now();
    if (!now) {
        std::fprintf(stderr, "engine step with no clock\n");
        std::abort();
    }
    return *now;
}

void Engine::onClockSynced()
{
    if (started_) {
        return;
    }
    started_ = true;
    recompute();
    beginStartUp();
}

void Engine::onConfigChanged(Config config)
{
    const bool instanceChanged = config.accountNumber != config_.accountNumber || config.nmi != config_.nmi;
    const bool tokenChanged = config.tokenPreview != config_.tokenPreview;
    config_ = std::move(config);
    if (instanceChanged || tokenChanged) {
        reset(!instanceChanged);
        if (started_) {
            beginStartUp();
        }
    }
    recompute();
}

void Engine::onTimer(TimerId timer)
{
    if (!timers_.due(timer)) {
        return;
    }
    switch (timer) {
    case TimerId::startup:
        beginStartUp();
        return;
    case TimerId::price:
        priceLoop_.onTimer();
        return;
    case TimerId::retryAfter:
        if (!retry_) {
            std::fprintf(stderr, "retryAfter fired with no request to repeat\n");
            std::abort();
        }
        queue_.push_front(std::move(*retry_));
        retry_.reset();
        pump();
        return;
    case TimerId::accountSync:
        onAccountSyncPoint();
        return;
    case TimerId::usageSync:
        onUsageSyncPoint();
        return;
    case TimerId::evaluation:
        recompute();
        return;
    }
}

void Engine::send(Owner owner, Request request)
{
    queue_.push_back(Pending{owner, std::move(request)});
    pump();
}

void Engine::dropQueued(Owner owner)
{
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [owner](const Pending &pending) { return pending.owner == owner; }),
                 queue_.end());
}

void Engine::pump()
{
    if (inFlight_ || retry_ || queue_.empty()) {
        return;
    }
    Pending next = std::move(queue_.front());
    queue_.pop_front();
    const Instant now = clock();
    inFlight_ = InFlight{++lastId_, next.owner, std::move(next.request), now};
    if (inFlight_->request.endpoint == Endpoint::wait) {
        const Instant boundary = dispatchBoundary(now);
        holdStarts_.erase(std::remove_if(holdStarts_.begin(), holdStarts_.end(),
                                         [boundary](Instant start) { return start < boundary; }),
                          holdStarts_.end());
        holdStarts_.push_back(now);
    }
    platform_.httpGet(inFlight_->id, inFlight_->request);
}

size_t Engine::holdsStarted(Instant instant) const
{
    const Instant boundary = dispatchBoundary(instant);
    return static_cast<size_t>(std::count_if(holdStarts_.begin(), holdStarts_.end(), [boundary](Instant start) {
        return start >= boundary && start < boundary + DISPATCH_INTERVAL_S;
    }));
}

void Engine::onResponse(RequestId id, Response response)
{
    if (!inFlight_ || inFlight_->id != id) {
        platform_.log(LogLevel::info, "response to request " + std::to_string(id) + " dropped: not in flight");
        return;
    }
    const InFlight done = std::move(*inFlight_);
    inFlight_.reset();
    const Instant now = clock();
    if (const std::optional<int64_t> remaining = wholeNumber(response.dailyLimitRemaining)) {
        dailyLimitRemaining_ = remaining;
    }
    const std::string target = requestTarget(done.request);
    if (response.kind == ResponseKind::http) {
        platform_.log(LogLevel::warning, "GET " + target + " -> " + std::to_string(response.status) + ", " +
                                             std::to_string(response.errorBodyBytes) + " bytes");
        if (response.location) {
            platform_.log(LogLevel::warning, "GET " + target + " Location not followed: " + *response.location);
        }
        if (response.status == 429) {
            const std::optional<int64_t> seconds = wholeNumber(response.retryAfter);
            if (seconds) {
                record(done.request, response);
                if (done.request.endpoint == Endpoint::wait) {
                    const auto started = std::find(holdStarts_.begin(), holdStarts_.end(), done.sentAt);
                    if (started != holdStarts_.end()) {
                        holdStarts_.erase(started);
                    }
                }
                retry_ = Pending{done.owner, done.request};
                timers_.arm(TimerId::retryAfter, now + *seconds);
                recompute();
                return;
            }
            Response invalid;
            invalid.kind = ResponseKind::invalid;
            invalid.status = response.status;
            invalid.message = "429 Retry-After " + (response.retryAfter ? "\"" + *response.retryAfter + "\"" : "missing") +
                              " is not an integer number of seconds";
            response = std::move(invalid);
            platform_.log(LogLevel::warning, "GET " + target + " " + response.message);
        } else if (response.status == 401 || response.status == 403) {
            record(done.request, response);
            const bool gateway =
                response.status == 401 && gatewayErrorCode(response.errorBody) == std::optional<std::string>("unauthorized");
            stopRequests(gateway ? RequestsState::stoppedTokenRejected : RequestsState::stoppedUntilAccountSync);
            recompute();
            return;
        }
    } else if (response.kind == ResponseKind::network) {
        platform_.log(LogLevel::warning, "GET " + target + " -> " + response.message);
    } else if (response.kind == ResponseKind::invalid) {
        platform_.log(LogLevel::warning, "GET " + target + " -> " + response.message);
    }
    record(done.request, response);
    if (done.owner == Owner::price) {
        priceLoop_.onResponse(response);
    } else {
        onChainResponse(done.request, response);
    }
    recompute();
    pump();
}

void Engine::record(const Request &request, Response &response)
{
    const Instant now = clock();
    switch (request.endpoint) {
    case Endpoint::account:
        apply(snapshots_.account, response, now);
        return;
    case Endpoint::meters:
        apply(snapshots_.meters, response, now);
        return;
    case Endpoint::tokens:
        apply(snapshots_.tokens, response, now);
        return;
    case Endpoint::outlook:
        apply(snapshots_.outlook, response, now);
        return;
    case Endpoint::wait:
        if (response.kind != ResponseKind::body && response.kind != ResponseKind::noContent) {
            snapshots_.outlook.error = errorOf(response);
        }
        return;
    case Endpoint::usageHalfHourly:
        applyUsage(snapshots_.halfHourly, response, now);
        return;
    case Endpoint::usageDaily:
        applyUsage(snapshots_.daily, response, now);
        return;
    }
}

void Engine::recordWaitNoProgress(std::string message)
{
    platform_.log(LogLevel::warning, message);
    SnapshotError error;
    error.kind = ErrorKind::invalid;
    error.message = std::move(message);
    snapshots_.outlook.error = std::move(error);
}

void Engine::recompute()
{
    signals_ = computeSignals(platform_.now(), config_, snapshots_.account, snapshots_.meters, snapshots_.tokens,
                              snapshots_.outlook, snapshots_.halfHourly, snapshots_.daily);
    if (!config_.accountNumber && !signals_.account.fault && signals_.account.accountNumber) {
        config_.accountNumber = signals_.account.accountNumber;
        platform_.storeAccountNumber(*config_.accountNumber);
        platform_.log(LogLevel::info, "account number pinned");
    }
    if (signals_.nextEvaluation) {
        timers_.arm(TimerId::evaluation, *signals_.nextEvaluation);
    } else {
        timers_.cancel(TimerId::evaluation);
    }
}

void Engine::reset(bool keepSnapshots)
{
    for (size_t i = 0; i < TIMER_COUNT; ++i) {
        timers_.cancel(static_cast<TimerId>(i));
    }
    priceLoop_.stop();
    queue_.clear();
    inFlight_.reset();
    retry_.reset();
    holdStarts_.clear();
    chain_.clear();
    chainStep_.reset();
    usageWindow_.reset();
    refused_ = {};
    requestsState_ = RequestsState::running;
    if (!keepSnapshots) {
        snapshots_ = Snapshots{};
    }
}

void Engine::stopRequests(RequestsState state)
{
    requestsState_ = state;
    queue_.clear();
    retry_.reset();
    chain_.clear();
    chainStep_.reset();
    usageWindow_.reset();
    priceLoop_.stop();
    timers_.cancel(TimerId::retryAfter);
    timers_.cancel(TimerId::startup);
    if (state == RequestsState::stoppedTokenRejected) {
        timers_.cancel(TimerId::accountSync);
        timers_.cancel(TimerId::usageSync);
        platform_.log(LogLevel::error, "requests stopped until the token changes");
        return;
    }
    platform_.log(LogLevel::error, "requests stopped until the next account sync");
    if (!timers_.target(TimerId::accountSync)) {
        constexpr Instant DAY = 86400;
        const Instant now = clock();
        Instant next = now - (now % DAY + DAY) % DAY + ACCOUNT_SYNC_MINUTE_OF_DAY * 60;
        if (next <= now) {
            next += DAY;
        }
        timers_.arm(TimerId::startup, next);
    }
}

Engine::AccountFacts Engine::accountFacts() const
{
    AccountFacts facts;
    if (!snapshots_.account.body) {
        return facts;
    }
    const detail::Selection selection = detail::selectAccount(config_, *snapshots_.account.body);
    if (selection.fault || selection.account == nullptr || !selection.account->product.present()) {
        return facts;
    }
    const Product &product = selection.account->product.value;
    if (product.gridType.present()) {
        facts.region = product.gridType.value;
    }
    if (product.timeZone.present()) {
        facts.zone = timeZoneFor(product.timeZone.value);
    }
    return facts;
}

}
