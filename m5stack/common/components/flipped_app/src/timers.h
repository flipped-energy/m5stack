#pragma once

#include <array>
#include <cstdint>

#include "esp_timer.h"

#include "flipped/core/platform.h"

namespace flipped::app {

class EspTimers {
public:
    using Fired = void (*)(core::TimerId timer);

    explicit EspTimers(Fired fired);
    EspTimers(const EspTimers &) = delete;
    EspTimers &operator=(const EspTimers &) = delete;

    void arm(core::TimerId timer, int64_t delaySeconds);
    void cancel(core::TimerId timer);

private:
    static void onExpiry(void *arg);

    std::array<esp_timer_handle_t, core::TIMER_COUNT> handles_{};
};

}
