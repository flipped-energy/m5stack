#pragma once

#include <string>
#include <string_view>
#include <variant>

#include "flipped/core/byte_source.h"
#include "flipped/core/types.h"

namespace flipped::core {

struct Invalid {
    std::string message;
};

template <typename Body>
using Parsed = std::variant<Body, Invalid>;

Parsed<AccountBody> parseAccount(ByteSource &source);
Parsed<MetersBody> parseMeters(ByteSource &source);
Parsed<TokensBody> parseTokens(ByteSource &source);
Parsed<OutlookBody> parseOutlook(ByteSource &source);
Parsed<WaitBody> parseWait(ByteSource &source);
Parsed<UsageBody> parseUsage(ByteSource &source, std::string_view nmi);

}
