#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace flipped::core {

struct ConfigResult {
    bool accepted = false;
    std::string text;
};

inline constexpr const char *TOKEN_PREFIX = "fdk_";

std::optional<std::string> tokenRefusal(std::string_view token);
std::optional<std::string> accountNumberRefusal(std::string_view accountNumber);
std::optional<std::string> nmiRefusal(std::string_view nmi);
std::optional<std::string> thresholdsRefusal(std::optional<double> high, std::optional<double> low);
std::string thresholdText(double centsPerKwh);
std::optional<double> parseThreshold(std::string_view text);

}
