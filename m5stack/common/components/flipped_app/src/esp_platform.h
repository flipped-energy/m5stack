#pragma once

#include "flipped/core/platform.h"
#include "flipped/net/http_client.h"
#include "flipped/store/config_store.h"
#include "timers.h"

namespace flipped::app {

class EspPlatform : public core::Platform {
public:
    EspPlatform(EspTimers &timers, net::HttpClient &http, store::ConfigStore &store);

    std::optional<core::Instant> now() override;
    void armAt(core::TimerId timer, core::Instant target, int64_t delaySeconds) override;
    void cancel(core::TimerId timer) override;
    void httpGet(core::RequestId id, const core::Request &request) override;
    void log(core::LogLevel level, std::string_view line) override;
    void storeAccountNumber(std::string_view accountNumber) override;

private:
    EspTimers &timers_;
    net::HttpClient &http_;
    store::ConfigStore &store_;
};

}
