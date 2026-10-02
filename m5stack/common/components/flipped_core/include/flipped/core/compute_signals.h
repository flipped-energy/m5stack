#pragma once

#include <optional>
#include <string>

#include "flipped/core/signals.h"
#include "flipped/core/types.h"

namespace flipped::core {

Signals computeSignals(std::optional<Instant> instant, const Config &config, const Snapshot<AccountBody> &account,
                       const Snapshot<MetersBody> &meters, const Snapshot<TokensBody> &tokens,
                       const Snapshot<OutlookBody> &outlook, const Snapshot<UsageBody> &halfHourly,
                       const Snapshot<UsageBody> &daily);

std::optional<std::string> selectedNmi(const Config &config, const Snapshot<AccountBody> &account,
                                       const Snapshot<MetersBody> &meters);

}
