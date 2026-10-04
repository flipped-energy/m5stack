#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "flipped/core/identity.h"
#include "flipped/core/parse.h"
#include "flipped/core/signals.h"

namespace flipped::core {

inline constexpr size_t SWITCH_COUNT = SWITCH_IDENTITIES.size();
inline constexpr size_t STORED_SWITCHES_BYTES = INSTANCE_HASH_CHARS + SWITCH_COUNT * 2;
inline constexpr std::array<std::string_view, 4> FOUR_SWITCH_BLOB_KEYS{"peak_rate", "off_peak_rate", "wholesale_price_high",
                                                                       "wholesale_price_low"};
inline constexpr size_t FOUR_SWITCH_BLOB_BYTES = INSTANCE_HASH_CHARS + FOUR_SWITCH_BLOB_KEYS.size() * 2;

struct StoredSwitches {
    std::string h;
    std::array<std::optional<uint16_t>, SWITCH_COUNT> endpointIds{};
};

struct SwitchEndpoint {
    std::string key;
    std::string nodeLabel;
    std::string uniqueId;
    std::optional<uint16_t> endpointId;
};

enum class SwitchAction : uint8_t { none, resume, replace };

struct SwitchPlan {
    SwitchAction action = SwitchAction::none;
    bool store = false;
    std::string h;
    std::array<SwitchEndpoint, SWITCH_COUNT> endpoints;
};

SwitchPlan switchPlan(const std::optional<std::string> &instanceKey, const std::optional<StoredSwitches> &stored);
std::array<std::optional<bool>, SWITCH_COUNT> switchValues(const Signals &signals);
std::array<uint8_t, STORED_SWITCHES_BYTES> encodeStoredSwitches(std::string_view h,
                                                                const std::array<uint16_t, SWITCH_COUNT> &endpointIds);
std::variant<StoredSwitches, Invalid> decodeStoredSwitches(const uint8_t *data, size_t size);

}
