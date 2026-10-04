#include <optional>
#include <string>

#include "flipped/app/ui_model.h"
#include "flipped/core/config_check.h"
#include "suite.h"

using flipped::core::parseThreshold;
using flipped::core::thresholdsRefusal;
using flipped::core::thresholdText;
using flipped::core::tokenRefusal;

namespace {

std::string expectRefusal(const std::optional<std::string> &refusal, const std::string &expected)
{
    if (!refusal) {
        return "accepted, expected refusal '" + expected + "'";
    }
    return *refusal == expected ? std::string() : "refusal '" + *refusal + "', expected '" + expected + "'";
}

std::string expectAccepted(const std::optional<std::string> &refusal)
{
    return refusal ? "refused: " + *refusal : std::string();
}

}

bool runConfigTests()
{
    Suite suite("config_tests");
    suite.run("token without the fdk_ prefix is refused",
              [] { return expectRefusal(tokenRefusal("abc_0123456789"), "token must start with fdk_"); });
    suite.run("token with a space is refused", [] {
        return expectRefusal(tokenRefusal("fdk_0123 456789"), "token contains byte 0x20 at position 8, outside 0x21..0x7E");
    });
    suite.run("token with the prefix is accepted", [] { return expectAccepted(tokenRefusal("fdk_0123456789abcdef")); });
    suite.run("low threshold equal to high is refused", [] {
        return expectRefusal(thresholdsRefusal(30.0, 30.0), "low threshold must be less than the high threshold");
    });
    suite.run("negative low threshold below high is accepted",
              [] { return expectAccepted(thresholdsRefusal(30.0, -5.5)); });
    suite.run("one threshold alone is accepted", [] { return expectAccepted(thresholdsRefusal(std::nullopt, 12.0)); });
    suite.run("threshold text round-trips the customer's number", [] {
        const std::string text = thresholdText(30.1);
        const std::optional<double> back = parseThreshold(text);
        if (text != "30.1") {
            return "text '" + text + "', expected '30.1'";
        }
        return back && *back == 30.1 ? std::string() : std::string("30.1 did not parse back to 30.1");
    });
    suite.run("threshold text with trailing characters is not a number",
              [] { return parseThreshold("30c") ? std::string("30c parsed as a number") : std::string(); });
    suite.run("ui model is default constructible on the host", [] {
        const flipped::app::UiModel model;
        return !model.hasToken && model.eligibleAccounts.empty() ? std::string() : std::string("unexpected defaults");
    });
    return suite.finish();
}
