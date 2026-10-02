#include <algorithm>
#include <cstdio>
#include <cstdlib>

#include "flipped/core/constants.h"
#include "flipped/core/sync.h"
#include "flipped/core/time.h"

namespace flipped::core {

namespace {

size_t slot(TimerId timer)
{
    return static_cast<size_t>(timer);
}

}

Timers::Timers(Platform &platform) : platform_(platform) {}

void Timers::arm(TimerId timer, Instant target)
{
    std::optional<Instant> &pending = targets_[slot(timer)];
    if (pending && *pending == target) {
        return;
    }
    const std::optional<Instant> now = platform_.now();
    if (!now) {
        std::fprintf(stderr, "timer %s armed for %lld with no clock\n", timerName(timer), static_cast<long long>(target));
        std::abort();
    }
    pending = target;
    const int64_t delay = std::clamp<int64_t>(target - *now, 0, TIMER_MAX_AHEAD_S);
    platform_.armAt(timer, target, delay);
}

void Timers::cancel(TimerId timer)
{
    std::optional<Instant> &pending = targets_[slot(timer)];
    if (!pending) {
        return;
    }
    pending.reset();
    platform_.cancel(timer);
}

bool Timers::due(TimerId timer)
{
    std::optional<Instant> &pending = targets_[slot(timer)];
    if (!pending) {
        return false;
    }
    const std::optional<Instant> now = platform_.now();
    if (!now) {
        std::fprintf(stderr, "timer %s fired with no clock\n", timerName(timer));
        std::abort();
    }
    if (*now >= *pending) {
        pending.reset();
        return true;
    }
    const Instant target = *pending;
    const int64_t delay = std::min<int64_t>(target - *now, TIMER_MAX_AHEAD_S);
    platform_.armAt(timer, target, delay);
    return false;
}

std::optional<Instant> Timers::target(TimerId timer) const
{
    return targets_[slot(timer)];
}

Instant dispatchBoundary(Instant instant)
{
    const Instant remainder = instant % DISPATCH_INTERVAL_S;
    return instant - (remainder < 0 ? remainder + DISPATCH_INTERVAL_S : remainder);
}

Instant nextDispatchBoundary(Instant instant)
{
    return dispatchBoundary(instant) + DISPATCH_INTERVAL_S;
}

std::string nemWallClock(Instant instant)
{
    constexpr Instant NEM_OFFSET_S = 10 * 3600;
    return formatInstant(instant + NEM_OFFSET_S).substr(0, 19);
}

}
