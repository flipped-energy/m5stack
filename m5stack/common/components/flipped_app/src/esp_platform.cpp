#include "esp_platform.h"

#include <cstdlib>

#include "esp_log.h"

#include "flipped/app/app.h"

namespace flipped::app {

namespace {

const char *TAG = "flipped_core";

}

EspPlatform::EspPlatform(EspTimers &timers, net::HttpClient &http, store::ConfigStore &store)
    : timers_(timers), http_(http), store_(store)
{
}

std::optional<core::Instant> EspPlatform::now()
{
    return app::now();
}

void EspPlatform::armAt(core::TimerId timer, core::Instant, int64_t delaySeconds)
{
    timers_.arm(timer, delaySeconds);
}

void EspPlatform::cancel(core::TimerId timer)
{
    timers_.cancel(timer);
}

void EspPlatform::httpGet(core::RequestId id, const core::Request &request)
{
    const std::optional<std::string> &token = store_.settings().token;
    if (!token) {
        ESP_LOGE(TAG, "request %u for %s with no token stored", static_cast<unsigned>(id),
                 core::endpointPath(request.endpoint));
        abort();
    }
    http_.get(id, request, *token);
}

void EspPlatform::log(core::LogLevel level, std::string_view line)
{
    const int length = static_cast<int>(line.size());
    switch (level) {
    case core::LogLevel::error:
        ESP_LOGE(TAG, "%.*s", length, line.data());
        return;
    case core::LogLevel::warning:
        ESP_LOGW(TAG, "%.*s", length, line.data());
        return;
    case core::LogLevel::info:
        ESP_LOGI(TAG, "%.*s", length, line.data());
        return;
    }
    abort();
}

void EspPlatform::storeAccountNumber(std::string_view accountNumber)
{
    const core::ConfigResult result = store_.setAccountNumber(accountNumber);
    if (!result.accepted) {
        ESP_LOGE(TAG, "account number not pinned: %s", result.text.c_str());
    }
}

}
