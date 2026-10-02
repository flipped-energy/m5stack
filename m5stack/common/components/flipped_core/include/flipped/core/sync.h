#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "flipped/core/platform.h"
#include "flipped/core/signals.h"
#include "flipped/core/types.h"
#include "flipped/core/tz_table.h"

namespace flipped::core {

class Timers {
public:
    explicit Timers(Platform &platform);

    void arm(TimerId timer, Instant target);
    void cancel(TimerId timer);
    bool due(TimerId timer);
    std::optional<Instant> target(TimerId timer) const;

private:
    Platform &platform_;
    std::array<std::optional<Instant>, TIMER_COUNT> targets_{};
};

Instant dispatchBoundary(Instant instant);
Instant nextDispatchBoundary(Instant instant);
std::string nemWallClock(Instant instant);
std::optional<std::string> tokenPreview(std::string_view token);

enum class RequestsState : uint8_t { running, stoppedTokenRejected, stoppedUntilAccountSync };
const char *requestsStateName(RequestsState state);

struct Snapshots {
    Snapshot<AccountBody> account;
    Snapshot<MetersBody> meters;
    Snapshot<TokensBody> tokens;
    Snapshot<OutlookBody> outlook;
    Snapshot<UsageBody> halfHourly;
    Snapshot<UsageBody> daily;
};

class Engine;

class PriceLoop {
public:
    enum class State : uint8_t { idle, bootstrapping, bootstrapWaiting, armed, holding, holdWaiting, fetchingOutlook };

    explicit PriceLoop(Engine &engine);

    void start(std::string region);
    void stop();
    void onTimer();
    void onResponse(const Response &response);
    bool running() const { return state_ != State::idle; }
    bool refused() const { return refused_; }
    const std::string &region() const { return region_; }
    State state() const { return state_; }

private:
    void bootstrap();
    void armNext();
    void hold();
    void waitForBoundary(State state);
    Request outlookRequest() const;
    Request waitRequest() const;
    bool fatal(const Response &response) const;

    Engine &engine_;
    State state_ = State::idle;
    bool refused_ = false;
    std::string region_;
    Instant t_ = 0;
};

class Engine {
public:
    Engine(Platform &platform, Config config, int usageLookbackDays);
    Engine(const Engine &) = delete;
    Engine &operator=(const Engine &) = delete;

    void onClockSynced();
    void onTimer(TimerId timer);
    void onResponse(RequestId id, Response response);
    void onConfigChanged(Config config);
    void onUsageRefresh();

    RequestsState requestsState() const { return requestsState_; }
    const Signals &signals() const { return signals_; }
    const Config &config() const { return config_; }
    const Snapshots &snapshots() const { return snapshots_; }
    const PriceLoop &priceLoop() const { return priceLoop_; }
    uint32_t usageSyncs() const { return usageSyncs_; }
    std::optional<int64_t> dailyLimitRemaining() const { return dailyLimitRemaining_; }

private:
    friend class PriceLoop;

    enum class Owner : uint8_t { chain, price };
    enum class Step : uint8_t { startupAccount, recoveryAccount, account, meters, tokens, price, usage, usageDaily };

    struct Pending {
        Owner owner = Owner::chain;
        Request request;
    };

    struct InFlight {
        RequestId id = 0;
        Owner owner = Owner::chain;
        Request request;
        Instant sentAt = 0;
    };

    struct AccountFacts {
        std::optional<std::string> region;
        std::optional<TimeZone> zone;
    };

    struct UsageWindow {
        std::string start;
        std::string end;
        std::string nmi;
    };

    Instant clock();
    void send(Owner owner, Request request);
    void dropQueued(Owner owner);
    void pump();
    void record(const Request &request, Response &response);
    void recordWaitNoProgress(std::string message);
    void recompute();
    void reset(bool keepSnapshots);
    void stopRequests(RequestsState state);
    size_t holdsStarted(Instant instant) const;
    AccountFacts accountFacts() const;

    void beginStartUp();
    void onStartUpAccount(const Response &response);
    void onStartUpFailure(const Response &response);

    void armSyncPoints();
    void onAccountSyncPoint();
    void onUsageSyncPoint();
    bool accountPartFailed() const;
    void onAccountStepSuccess();

    void runChain();
    void sendStep(Step step, Request request);
    void onChainResponse(const Request &request, const Response &response);
    bool startUsage();
    Request usageRequest(Endpoint endpoint) const;

    Platform &platform_;
    Config config_;
    int usageLookbackDays_;
    Timers timers_;
    PriceLoop priceLoop_;
    Snapshots snapshots_;
    Signals signals_;
    RequestsState requestsState_ = RequestsState::running;
    bool started_ = false;
    std::deque<Pending> queue_;
    std::optional<InFlight> inFlight_;
    std::optional<Pending> retry_;
    RequestId lastId_ = 0;
    std::vector<Instant> holdStarts_;
    std::deque<Step> chain_;
    std::optional<Step> chainStep_;
    std::optional<UsageWindow> usageWindow_;
    std::array<bool, ENDPOINT_COUNT> refused_{};
    uint32_t usageSyncs_ = 0;
    std::optional<int64_t> dailyLimitRemaining_;
    std::optional<Instant> lastUsageRefresh_;
};

}
