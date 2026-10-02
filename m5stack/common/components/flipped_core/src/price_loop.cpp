#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <variant>

#include "flipped/core/constants.h"
#include "flipped/core/sync.h"
#include "flipped/core/time.h"

namespace flipped::core {

namespace {

std::optional<Instant> outlookInstant(const Response &response)
{
    if (response.kind != ResponseKind::body) {
        return std::nullopt;
    }
    const OutlookBody *body = std::get_if<OutlookBody>(&response.body);
    if (body == nullptr || !body->now.present() || !body->now.value.time.present()) {
        return std::nullopt;
    }
    return parseInstant(body->now.value.time.value).instant;
}

}

PriceLoop::PriceLoop(Engine &engine) : engine_(engine) {}

void PriceLoop::start(std::string region)
{
    region_ = std::move(region);
    refused_ = false;
    bootstrap();
}

void PriceLoop::stop()
{
    state_ = State::idle;
    engine_.timers_.cancel(TimerId::price);
    engine_.dropQueued(Engine::Owner::price);
}

void PriceLoop::bootstrap()
{
    state_ = State::bootstrapping;
    engine_.send(Engine::Owner::price, outlookRequest());
}

void PriceLoop::armNext()
{
    const Instant target = t_ + DISPATCH_INTERVAL_S;
    if (target <= engine_.clock()) {
        hold();
        return;
    }
    state_ = State::armed;
    engine_.timers_.arm(TimerId::price, target);
}

void PriceLoop::hold()
{
    const Instant now = engine_.clock();
    if (engine_.holdsStarted(now) >= static_cast<size_t>(WAIT_HOLDS_PER_INTERVAL)) {
        waitForBoundary(State::holdWaiting);
        return;
    }
    state_ = State::holding;
    engine_.send(Engine::Owner::price, waitRequest());
}

void PriceLoop::waitForBoundary(State state)
{
    state_ = state;
    engine_.timers_.arm(TimerId::price, nextDispatchBoundary(engine_.clock()));
}

void PriceLoop::onTimer()
{
    switch (state_) {
    case State::bootstrapWaiting:
        bootstrap();
        return;
    case State::armed:
    case State::holdWaiting:
        hold();
        return;
    case State::idle:
    case State::bootstrapping:
    case State::holding:
    case State::fetchingOutlook:
        break;
    }
    std::fprintf(stderr, "price timer fired in state %d\n", static_cast<int>(state_));
    std::abort();
}

bool PriceLoop::fatal(const Response &response) const
{
    if (response.kind != ResponseKind::http) {
        return false;
    }
    if (response.status == 400) {
        return true;
    }
    return response.status == 404 && gatewayErrorCode(response.errorBody) == std::optional<std::string>("unknown_operation");
}

void PriceLoop::onResponse(const Response &response)
{
    if (fatal(response)) {
        engine_.platform_.log(LogLevel::error, "price loop stopped until restart: " +
                                                   std::string(endpointPath(state_ == State::holding ? Endpoint::wait
                                                                                                     : Endpoint::outlook)) +
                                                   " answered " + std::to_string(response.status));
        stop();
        refused_ = true;
        return;
    }
    switch (state_) {
    case State::bootstrapping: {
        const std::optional<Instant> instant = outlookInstant(response);
        if (!instant) {
            waitForBoundary(State::bootstrapWaiting);
            return;
        }
        t_ = *instant;
        armNext();
        return;
    }
    case State::holding: {
        if (response.kind == ResponseKind::noContent) {
            hold();
            return;
        }
        const WaitBody *body = response.kind == ResponseKind::body ? std::get_if<WaitBody>(&response.body) : nullptr;
        if (body == nullptr) {
            waitForBoundary(State::holdWaiting);
            return;
        }
        std::optional<Instant> newest;
        for (const WaitPoint &point : body->points) {
            if (point.start > t_ && (!newest || point.start > *newest)) {
                newest = point.start;
            }
        }
        if (!newest) {
            engine_.recordWaitNoProgress("wait_no_progress: " + std::to_string(body->points.size()) +
                                         " point(s), none newer than since=" + nemWallClock(t_));
            t_ += DISPATCH_INTERVAL_S;
            armNext();
            return;
        }
        t_ = *newest;
        state_ = State::fetchingOutlook;
        engine_.send(Engine::Owner::price, outlookRequest());
        return;
    }
    case State::fetchingOutlook: {
        const std::optional<Instant> instant = outlookInstant(response);
        if (instant) {
            t_ = std::max(t_, *instant);
        }
        armNext();
        return;
    }
    case State::idle:
    case State::bootstrapWaiting:
    case State::armed:
    case State::holdWaiting:
        break;
    }
    std::fprintf(stderr, "price response in state %d\n", static_cast<int>(state_));
    std::abort();
}

Request PriceLoop::outlookRequest() const
{
    Request request;
    request.endpoint = Endpoint::outlook;
    request.query.push_back(QueryParameter{"region", region_});
    request.timeoutSeconds = HTTP_TIMEOUT_S;
    return request;
}

Request PriceLoop::waitRequest() const
{
    Request request;
    request.endpoint = Endpoint::wait;
    request.query.push_back(QueryParameter{"region", region_});
    request.query.push_back(QueryParameter{"since", nemWallClock(t_)});
    request.query.push_back(QueryParameter{"timeoutSeconds", std::to_string(WAIT_TIMEOUT_S)});
    request.timeoutSeconds = WAIT_HTTP_TIMEOUT_S;
    return request;
}

}
