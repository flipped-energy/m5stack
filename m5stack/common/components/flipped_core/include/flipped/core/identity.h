#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace flipped::core {

inline constexpr size_t INSTANCE_HASH_CHARS = 16;
inline constexpr size_t UNIQUE_ID_MAX_CHARS = 32;

struct SwitchIdentity {
    const char *key;
    const char *nodeLabel;
    const char *suffix;
};

inline constexpr std::array<SwitchIdentity, 5> SWITCH_IDENTITIES{{
    {"peak_rate", "Peak Rate", "PK"},
    {"off_peak_rate", "Off-Peak Rate", "OP"},
    {"shoulder_rate", "Shoulder Rate", "SH"},
    {"wholesale_price_high", "Wholesale Price High", "WH"},
    {"wholesale_price_low", "Wholesale Price Low", "WL"},
}};

std::array<uint8_t, 32> sha256(std::string_view data);
std::string instanceKey(std::string_view accountNumber, const std::optional<std::string> &nmi);
std::string instanceHash(std::string_view instanceKey);
std::string stableId(std::string_view instanceKey, std::string_view key);
std::string uniqueId(std::string_view instanceHash, std::string_view suffix);

}
