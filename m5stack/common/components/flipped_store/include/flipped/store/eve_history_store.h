#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "flipped/core/eve_history.h"
#include "flipped/core/parse.h"

namespace flipped::store {

inline constexpr const char *KEY_EVE_HISTORY = "eve_hist";

std::optional<std::variant<core::StoredEveHistory, core::Invalid>> readEveHistory(uint16_t memorySize);
void writeEveHistory(const std::string &h, const core::EveHistoryState &state);

}
