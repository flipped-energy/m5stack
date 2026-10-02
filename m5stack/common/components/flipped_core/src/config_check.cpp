#include "flipped/core/config_check.h"

#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace flipped::core {

namespace {

std::optional<std::string> finiteRefusal(const char *name, const std::optional<double> &value)
{
    if (value && !std::isfinite(*value)) {
        return std::string(name) + " threshold is not a finite number";
    }
    return std::nullopt;
}

}

std::optional<std::string> tokenRefusal(std::string_view token)
{
    if (token.substr(0, std::string_view(TOKEN_PREFIX).size()) != TOKEN_PREFIX) {
        return std::string("token must start with ") + TOKEN_PREFIX;
    }
    for (size_t i = 0; i < token.size(); ++i) {
        const auto byte = static_cast<unsigned char>(token[i]);
        if (byte < 0x21 || byte > 0x7E) {
            char text[96];
            std::snprintf(text, sizeof text, "token contains byte 0x%02X at position %zu, outside 0x21..0x7E", byte, i);
            return std::string(text);
        }
    }
    return std::nullopt;
}

std::optional<std::string> accountNumberRefusal(std::string_view accountNumber)
{
    if (accountNumber.empty()) {
        return std::string("account number is empty");
    }
    return std::nullopt;
}

std::optional<std::string> nmiRefusal(std::string_view nmi)
{
    if (nmi.empty()) {
        return std::string("NMI is empty");
    }
    return std::nullopt;
}

std::optional<std::string> thresholdsRefusal(std::optional<double> high, std::optional<double> low)
{
    if (std::optional<std::string> refusal = finiteRefusal("high", high)) {
        return refusal;
    }
    if (std::optional<std::string> refusal = finiteRefusal("low", low)) {
        return refusal;
    }
    if (high && low && !(*low < *high)) {
        return std::string("low threshold must be less than the high threshold");
    }
    return std::nullopt;
}

std::string thresholdText(double centsPerKwh)
{
    char text[32];
    const std::to_chars_result result = std::to_chars(text, text + sizeof text, centsPerKwh);
    if (result.ec != std::errc()) {
        std::fprintf(stderr, "threshold %g does not fit in %zu characters\n", centsPerKwh, sizeof text);
        std::abort();
    }
    return std::string(text, result.ptr);
}

std::optional<double> parseThreshold(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    const std::string copy(text);
    char *end = nullptr;
    const double value = std::strtod(copy.c_str(), &end);
    if (end != copy.c_str() + copy.size() || !std::isfinite(value)) {
        return std::nullopt;
    }
    return value;
}

}
