#include <cmath>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include <ArduinoJson.h>

#include "flipped/core/compute_signals.h"
#include "flipped/core/parse.h"
#include "flipped/core/signals_json.h"
#include "flipped/core/time.h"
#include "json_reader.h"
#include "json_text.h"
#include "memory_source.h"

using namespace flipped::core;

namespace {

constexpr size_t SMALL_CHUNK = 7;
constexpr double TOLERANCE = 1e-6;

std::optional<Instant> instantOf(JsonVariantConst value, std::string &problem, const char *label)
{
    if (value.isNull()) {
        return std::nullopt;
    }
    const InstantText parsed = parseInstant(value.as<const char *>() == nullptr ? "" : value.as<const char *>());
    if (!parsed.instant) {
        problem = std::string(label) + " is not an instant";
    }
    return parsed.instant;
}

std::optional<std::string> optionalString(JsonVariantConst value)
{
    if (value.isNull()) {
        return std::nullopt;
    }
    return std::string(value.as<const char *>());
}

std::optional<double> optionalNumber(JsonVariantConst value)
{
    if (value.isNull()) {
        return std::nullopt;
    }
    return flipped::core::detail::markedNumber(value);
}

DeserializationError parseExact(JsonDocument &document, std::string_view text, std::string &problem)
{
    MemorySource source(text, text.size());
    flipped::core::detail::SourceReader reader(source);
    const DeserializationError error = deserializeJson(document, reader);
    if (reader.numberError()) {
        problem = *reader.numberError();
    }
    return error;
}

std::optional<SnapshotError> errorOf(JsonVariantConst value, std::string &problem)
{
    if (value.isNull()) {
        return std::nullopt;
    }
    SnapshotError error;
    const std::string kind = value["kind"].as<const char *>() == nullptr ? "" : value["kind"].as<const char *>();
    if (kind == "http") {
        error.kind = ErrorKind::http;
        error.httpStatus = static_cast<int>(flipped::core::detail::markedNumber(value["status"]));
        const JsonString body = value["body"].as<JsonString>();
        error.body.assign(body.c_str(), body.size());
        if (!value["bodyBytes"].isUnbound()) {
            error.bodyBytes = static_cast<size_t>(flipped::core::detail::markedNumber(value["bodyBytes"]));
        }
    } else if (kind == "network") {
        error.kind = ErrorKind::network;
        error.message = value["message"].as<const char *>();
    } else if (kind == "invalid") {
        error.kind = ErrorKind::invalid;
        error.message = value["message"].as<const char *>();
    } else {
        problem = "unknown error kind " + kind;
    }
    return error;
}

template <typename Body, typename ParseBody>
Snapshot<Body> snapshotOf(const std::string &text, RawScanner &scanner, Span input, JsonVariantConst value,
                          const char *name, size_t chunk, ParseBody parseBody, std::string &problem)
{
    Snapshot<Body> snapshot;
    snapshot.fetchedAt = instantOf(value["fetchedAt"], problem, "fetchedAt");
    snapshot.error = errorOf(value["error"], problem);
    if (value["body"].isNull()) {
        return snapshot;
    }
    const std::optional<Span> snapshotSpan = scanner.member(input, name);
    const std::optional<Span> bodySpan = snapshotSpan ? scanner.member(*snapshotSpan, "body") : std::nullopt;
    if (!bodySpan) {
        problem = std::string("no raw body text for ") + name;
        return snapshot;
    }
    const std::string_view raw(text.data() + bodySpan->begin, bodySpan->end - bodySpan->begin);
    MemorySource source(raw, chunk == 0 ? raw.size() : chunk);
    Parsed<Body> parsed = parseBody(source);
    if (std::holds_alternative<Invalid>(parsed)) {
        problem = std::string(name) + " body rejected by its parser: " + std::get<Invalid>(parsed).message;
        return snapshot;
    }
    snapshot.body = std::move(std::get<Body>(parsed));
    return snapshot;
}

bool isNumber(JsonVariantConst value)
{
    return flipped::core::detail::isMarkedNumber(value);
}

double numberOf(JsonVariantConst value)
{
    return flipped::core::detail::markedNumber(value);
}

bool same(JsonVariantConst expected, JsonVariantConst actual, const std::string &path, std::string &why)
{
    if (expected.isNull()) {
        if (!actual.isNull()) {
            why = path + ": expected null";
            return false;
        }
        return true;
    }
    if (expected.is<bool>()) {
        if (!actual.is<bool>() || actual.as<bool>() != expected.as<bool>()) {
            why = path + ": expected " + (expected.as<bool>() ? "true" : "false");
            return false;
        }
        return true;
    }
    if (isNumber(expected)) {
        if (!isNumber(actual) || std::fabs(numberOf(actual) - numberOf(expected)) > TOLERANCE) {
            char text[96];
            std::snprintf(text, sizeof text, ": expected %.17g got %.17g", numberOf(expected),
                          isNumber(actual) ? numberOf(actual) : NAN);
            why = path + text;
            return false;
        }
        return true;
    }
    if (expected.is<JsonString>()) {
        if (!actual.is<JsonString>() || std::string(actual.as<const char *>()) != expected.as<const char *>()) {
            why = path + ": expected \"" + expected.as<const char *>() + "\" got " +
                  (actual.is<JsonString>() ? "\"" + std::string(actual.as<const char *>()) + "\"" : "a non-string");
            return false;
        }
        return true;
    }
    if (expected.is<JsonArrayConst>()) {
        if (!actual.is<JsonArrayConst>() || actual.size() != expected.size()) {
            why = path + ": expected an array of " + std::to_string(expected.size());
            return false;
        }
        for (size_t i = 0; i < expected.size(); ++i) {
            if (!same(expected[i], actual[i], path + "[" + std::to_string(i) + "]", why)) {
                return false;
            }
        }
        return true;
    }
    if (!actual.is<JsonObjectConst>()) {
        why = path + ": expected an object";
        return false;
    }
    const bool fault = path.size() >= 6 && path.compare(path.size() - 6, 6, ".fault") == 0;
    if (fault) {
        for (const char *key : {"code", "httpStatus", "body", "bodyBytes"}) {
            if (std::string_view(key) != "code" && expected[key].isUnbound()) {
                continue;
            }
            if (!same(expected[key], actual[key], path + "." + key, why)) {
                return false;
            }
        }
        return true;
    }
    for (JsonPairConst pair : expected.as<JsonObjectConst>()) {
        if (actual[pair.key().c_str()].isUnbound()) {
            why = path + "." + pair.key().c_str() + ": missing";
            return false;
        }
        if (!same(pair.value(), actual[pair.key().c_str()], path + "." + pair.key().c_str(), why)) {
            return false;
        }
    }
    for (JsonPairConst pair : actual.as<JsonObjectConst>()) {
        if (expected[pair.key().c_str()].isUnbound()) {
            why = path + "." + pair.key().c_str() + ": not expected";
            return false;
        }
    }
    return true;
}

struct Outcome {
    std::string json;
    std::string problem;
};

Outcome run(const std::string &text, JsonVariantConst vector, size_t chunk)
{
    Outcome outcome;
    RawScanner scanner(text);
    const std::optional<Span> root = scanner.root();
    const std::optional<Span> input = root ? scanner.member(*root, "input") : std::nullopt;
    if (!input) {
        outcome.problem = "no input member";
        return outcome;
    }
    JsonVariantConst in = vector["input"];
    Config config;
    config.accountNumber = optionalString(in["config"]["accountNumber"]);
    config.nmi = optionalString(in["config"]["nmi"]);
    config.tokenPreview = optionalString(in["config"]["tokenPreview"]);
    config.priceHighThresholdCentsPerKwh = optionalNumber(in["config"]["priceHighThresholdCentsPerKwh"]);
    config.priceLowThresholdCentsPerKwh = optionalNumber(in["config"]["priceLowThresholdCentsPerKwh"]);
    std::string &problem = outcome.problem;
    const std::optional<Instant> instant = instantOf(in["instant"], problem, "instant");

    const auto account = snapshotOf<AccountBody>(text, scanner, *input, in["account"], "account", chunk,
                                                 [](ByteSource &source) { return parseAccount(source); }, problem);
    const auto meters = snapshotOf<MetersBody>(text, scanner, *input, in["meters"], "meters", chunk,
                                               [](ByteSource &source) { return parseMeters(source); }, problem);
    const auto tokens = snapshotOf<TokensBody>(text, scanner, *input, in["tokens"], "tokens", chunk,
                                               [](ByteSource &source) { return parseTokens(source); }, problem);
    const auto outlook = snapshotOf<OutlookBody>(text, scanner, *input, in["outlook"], "outlook", chunk,
                                                 [](ByteSource &source) { return parseOutlook(source); }, problem);
    const std::string nmi = selectedNmi(config, account, meters).value_or(std::string());
    const auto usageParser = [&nmi](ByteSource &source) { return parseUsage(source, nmi); };
    const auto halfHourly = snapshotOf<UsageBody>(text, scanner, *input, in["usageHalfHourly"], "usageHalfHourly",
                                                  chunk, usageParser, problem);
    const auto daily =
        snapshotOf<UsageBody>(text, scanner, *input, in["usageDaily"], "usageDaily", chunk, usageParser, problem);
    if (!problem.empty()) {
        return outcome;
    }
    outcome.json = signalsJson(computeSignals(instant, config, account, meters, tokens, outlook, halfHourly, daily));
    return outcome;
}

}

int runVectors(const char *directory)
{
    const std::string base(directory);
    const std::optional<std::string> indexText = readFile(base + "/index.json");
    if (!indexText) {
        std::fprintf(stderr, "cannot read %s/index.json\n", directory);
        return 1;
    }
    JsonDocument index;
    const DeserializationError indexError = deserializeJson(index, *indexText);
    if (indexError) {
        std::fprintf(stderr, "%s/index.json: %s\n", directory, indexError.c_str());
        return 1;
    }
    size_t passed = 0;
    size_t failed = 0;
    for (JsonVariantConst entry : index.as<JsonArrayConst>()) {
        const std::string path = entry["path"].as<const char *>();
        const std::optional<std::string> text = readFile(base + "/" + path);
        std::string why;
        if (!text) {
            why = "cannot read the file";
        }
        JsonDocument vector;
        if (why.empty()) {
            const DeserializationError error = parseExact(vector, *text, why);
            if (error) {
                why = std::string("vector JSON: ") + error.c_str();
            }
        }
        Outcome small;
        Outcome whole;
        if (why.empty()) {
            small = run(*text, vector.as<JsonVariantConst>(), SMALL_CHUNK);
            whole = run(*text, vector.as<JsonVariantConst>(), 0);
            if (!small.problem.empty()) {
                why = "7-byte source: " + small.problem;
            } else if (!whole.problem.empty()) {
                why = "whole-body source: " + whole.problem;
            } else if (small.json != whole.json) {
                why = "7-byte and whole-body sources give different signals";
            }
        }
        if (why.empty()) {
            JsonDocument actual;
            const DeserializationError error = parseExact(actual, whole.json, why);
            if (!why.empty()) {
                why = "signals JSON: " + why;
            } else if (error) {
                why = std::string("signals JSON: ") + error.c_str();
            } else {
                same(vector["expected"], actual.as<JsonVariantConst>(), "expected", why);
            }
        }
        if (why.empty()) {
            ++passed;
            std::printf("ok   %s (7-byte source, whole-body source)\n", path.c_str());
        } else {
            ++failed;
            std::printf("FAIL %s: %s\n", path.c_str(), why.c_str());
        }
    }
    std::printf("%zu vectors: %zu passed, %zu failed\n", passed + failed, passed, failed);
    std::fflush(stdout);
    return failed == 0 && passed > 0 ? 0 : 1;
}
