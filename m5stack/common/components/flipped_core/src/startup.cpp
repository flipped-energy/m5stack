#include <string>

#include "flipped/core/constants.h"
#include "flipped/core/sync.h"

namespace flipped::core {

void Engine::beginStartUp()
{
    chain_.clear();
    chain_.push_back(Step::startupAccount);
    chain_.push_back(Step::meters);
    chain_.push_back(Step::tokens);
    chain_.push_back(Step::price);
    chain_.push_back(Step::usage);
    runChain();
}

void Engine::onStartUpAccount(const Response &response)
{
    if (response.kind != ResponseKind::body) {
        onStartUpFailure(response);
        return;
    }
    requestsState_ = RequestsState::running;
    armSyncPoints();
}

void Engine::onStartUpFailure(const Response &response)
{
    chain_.clear();
    if (response.kind == ResponseKind::http &&
        (response.status == 400 ||
         (response.status == 404 && gatewayErrorCode(response.errorBody) == std::optional<std::string>("unknown_operation")))) {
        platform_.log(LogLevel::error, "start-up stopped until the configuration changes or the device restarts: " +
                                           std::string(endpointPath(Endpoint::account)) + " answered " +
                                           std::to_string(response.status));
        return;
    }
    timers_.arm(TimerId::startup, nextDispatchBoundary(clock()));
}

}
