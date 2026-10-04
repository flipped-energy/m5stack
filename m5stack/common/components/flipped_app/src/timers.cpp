#include "timers.h"

#include <cstdlib>

#include "esp_log.h"

namespace flipped::app {

namespace {

const char *TAG = "flipped_timers";
EspTimers::Fired fired = nullptr;

}

EspTimers::EspTimers(Fired onFired)
{
    if (fired != nullptr) {
        ESP_LOGE(TAG, "a second EspTimers was created");
        abort();
    }
    fired = onFired;
    for (size_t i = 0; i < core::TIMER_COUNT; ++i) {
        esp_timer_create_args_t args = {};
        args.callback = onExpiry;
        args.arg = reinterpret_cast<void *>(static_cast<uintptr_t>(i));
        args.dispatch_method = ESP_TIMER_TASK;
        args.name = core::timerName(static_cast<core::TimerId>(i));
        ESP_ERROR_CHECK(esp_timer_create(&args, &handles_[i]));
    }
}

void EspTimers::arm(core::TimerId timer, int64_t delaySeconds)
{
    esp_timer_handle_t handle = handles_[static_cast<size_t>(timer)];
    if (esp_timer_is_active(handle)) {
        ESP_ERROR_CHECK(esp_timer_stop(handle));
    }
    ESP_ERROR_CHECK(esp_timer_start_once(handle, static_cast<uint64_t>(delaySeconds) * 1000000ULL));
}

void EspTimers::cancel(core::TimerId timer)
{
    esp_timer_handle_t handle = handles_[static_cast<size_t>(timer)];
    if (esp_timer_is_active(handle)) {
        ESP_ERROR_CHECK(esp_timer_stop(handle));
    }
}

void EspTimers::onExpiry(void *arg)
{
    fired(static_cast<core::TimerId>(reinterpret_cast<uintptr_t>(arg)));
}

}
