#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "flipped/core/platform.h"

struct TimerRecord {
    flipped::core::Instant armedAt = 0;
    std::string timer;
    flipped::core::Instant target = 0;
    int64_t delaySeconds = 0;
};

struct SentRequest {
    flipped::core::Instant sendAt = 0;
    flipped::core::RequestId id = 0;
    flipped::core::Request request;
};

class FakePlatform : public flipped::core::Platform {
public:
    std::optional<flipped::core::Instant> now() override { return clock; }

    void armAt(flipped::core::TimerId timer, flipped::core::Instant target, int64_t delaySeconds) override
    {
        timers.push_back(TimerRecord{clock, flipped::core::timerName(timer), target, delaySeconds});
        pending[static_cast<size_t>(timer)] = std::make_pair(clock + delaySeconds, ++sequence_);
    }

    void cancel(flipped::core::TimerId timer) override { pending[static_cast<size_t>(timer)].reset(); }

    void httpGet(flipped::core::RequestId id, const flipped::core::Request &request) override
    {
        sent.push_back(SentRequest{clock, id, request});
    }

    void log(flipped::core::LogLevel, std::string_view line) override { lines.emplace_back(line); }

    void storeAccountNumber(std::string_view accountNumber) override { storedAccountNumber = std::string(accountNumber); }

    std::optional<std::pair<flipped::core::TimerId, flipped::core::Instant>> nextTimer() const
    {
        std::optional<size_t> best;
        for (size_t i = 0; i < pending.size(); ++i) {
            if (pending[i] && (!best || pending[i]->first < pending[*best]->first ||
                               (pending[i]->first == pending[*best]->first && pending[i]->second < pending[*best]->second))) {
                best = i;
            }
        }
        if (!best) {
            return std::nullopt;
        }
        return std::make_pair(static_cast<flipped::core::TimerId>(*best), pending[*best]->first);
    }

    void forgetTimers() { pending = {}; }

    flipped::core::Instant clock = 0;
    std::vector<TimerRecord> timers;
    std::array<std::optional<std::pair<flipped::core::Instant, uint64_t>>, flipped::core::TIMER_COUNT> pending{};
    std::vector<SentRequest> sent;
    std::optional<std::string> storedAccountNumber;
    std::vector<std::string> lines;

private:
    uint64_t sequence_ = 0;
};
