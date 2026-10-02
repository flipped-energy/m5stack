#include <cstdio>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <ArduinoJson.h>

#include "flipped/core/eve_history.h"
#include "flipped/core/time.h"
#include "json_reader.h"
#include "json_text.h"
#include "memory_source.h"
#include "suite.h"

using namespace flipped::core;

namespace {

const std::string H = "0123456789abcdef";

std::string hex(const std::vector<uint8_t> &bytes)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for (const uint8_t byte : bytes) {
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0x0F]);
    }
    return text;
}

int nibble(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

std::optional<std::vector<uint8_t>> bytesOf(const std::string &text)
{
    if (text.size() % 2 != 0) {
        return std::nullopt;
    }
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < text.size(); i += 2) {
        const int high = nibble(text[i]);
        const int low = nibble(text[i + 1]);
        if (high < 0 || low < 0) {
            return std::nullopt;
        }
        bytes.push_back(static_cast<uint8_t>(high * 16 + low));
    }
    return bytes;
}

std::string text(JsonVariantConst value)
{
    return value.as<const char *>() == nullptr ? std::string() : std::string(value.as<const char *>());
}

double number(JsonVariantConst value)
{
    return flipped::core::detail::markedNumber(value);
}

std::string u32le(uint32_t value)
{
    return hex({static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8), static_cast<uint8_t>(value >> 16),
                static_cast<uint8_t>(value >> 24)});
}

std::string compareStatus(const EveHistory &history, JsonVariantConst expected, const char *label)
{
    const std::optional<std::vector<uint8_t>> status = history.historyStatus();
    const std::string got = status ? hex(*status) : "null";
    if (got != text(expected)) {
        return std::string(label) + " historyStatus " + got + ", expected " + text(expected);
    }
    return "";
}

std::string compareReplay(EveHistory &history, JsonArrayConst requests, const char *label)
{
    if (requests.size() == 0) {
        return std::string(label) + " has no request to replay";
    }
    for (JsonVariantConst request : requests) {
        const auto address = static_cast<uint32_t>(number(request["address"]));
        const std::string written = text(request["request"]);
        if (written != "0114" + u32le(address) + "0000") {
            return std::string(label) + " request " + written + " does not carry address " + std::to_string(address);
        }
        const std::optional<std::vector<uint8_t>> bytes = bytesOf(written);
        if (!bytes) {
            return std::string(label) + " request " + written + " is not hex";
        }
        if (const std::optional<Invalid> invalid = history.handleRequest(bytes->data(), bytes->size())) {
            return std::string(label) + " " + invalid->message;
        }
        size_t index = 0;
        for (JsonVariantConst read : request["reads"].as<JsonArrayConst>()) {
            const std::string got = hex(history.historyEntries());
            if (got != text(read)) {
                return std::string(label) + " address " + std::to_string(address) + " read " + std::to_string(index) +
                       ": " + got + ", expected " + text(read);
            }
            ++index;
        }
        if (index == 0) {
            return std::string(label) + " address " + std::to_string(address) + " has no read";
        }
    }
    return "";
}

std::string compareState(const EveHistory &history, JsonVariantConst expected, const char *label)
{
    const EveHistoryState &s = history.state();
    const std::string got = std::to_string(s.firstEntry) + "/" + std::to_string(s.lastEntry) + "/" +
                            std::to_string(s.usedMemory) + "/" + std::to_string(s.refTime) + "/" +
                            std::to_string(static_cast<uint64_t>(s.refTime) + EVE_EPOCH);
    const std::string want = std::to_string(static_cast<uint64_t>(number(expected["firstEntry"]))) + "/" +
                             std::to_string(static_cast<uint64_t>(number(expected["lastEntry"]))) + "/" +
                             std::to_string(static_cast<uint64_t>(number(expected["usedMemory"]))) + "/" +
                             std::to_string(static_cast<uint64_t>(number(expected["refTime"]))) + "/" +
                             std::to_string(static_cast<uint64_t>(number(expected["initialTime"])));
    if (got != want) {
        return std::string(label) + " state firstEntry/lastEntry/usedMemory/refTime/initialTime " + got + ", expected " +
               want;
    }
    return "";
}

std::string reload(EveHistory &history, const char *label)
{
    const std::vector<uint8_t> blob = encodeEveHistory(H, history.state());
    std::variant<StoredEveHistory, Invalid> decoded = decodeEveHistory(blob.data(), blob.size(), history.state().memorySize);
    if (const Invalid *invalid = std::get_if<Invalid>(&decoded)) {
        return std::string(label) + " " + invalid->message;
    }
    StoredEveHistory &stored = std::get<StoredEveHistory>(decoded);
    if (stored.h != H) {
        return std::string(label) + " blob h " + stored.h + ", expected " + H;
    }
    history = EveHistory(std::move(stored.state));
    if (encodeEveHistory(H, history.state()) != blob) {
        return std::string(label) + " blob changed across a decode and encode";
    }
    return "";
}

std::string runFixture(const std::string &body)
{
    JsonDocument document;
    MemorySource source(body, body.size());
    flipped::core::detail::SourceReader reader(source);
    const DeserializationError error = deserializeJson(document, reader);
    if (reader.numberError()) {
        return *reader.numberError();
    }
    if (error) {
        return std::string("fixture JSON: ") + error.c_str();
    }
    if (static_cast<uint32_t>(number(document["epoch"])) != EVE_EPOCH) {
        return "fixture epoch differs from EVE_EPOCH";
    }
    const auto memorySize = static_cast<uint16_t>(number(document["memorySize"]));

    std::vector<EnergyEntry> intervals;
    for (JsonVariantConst item : document["intervals"].as<JsonArrayConst>()) {
        const InstantText start = parseInstant(text(item["start"]));
        if (!start.instant) {
            return "interval start " + text(item["start"]) + " is not an instant";
        }
        EnergyEntry interval;
        interval.start = *start.instant;
        interval.durationMinutes = static_cast<int>(number(item["durationMinutes"]));
        interval.gridImportKwh = number(item["kwh"]);
        intervals.push_back(interval);
    }

    std::optional<size_t> restartAfter;
    if (!document["restartAfterEntry"].isNull()) {
        restartAfter = static_cast<size_t>(number(document["restartAfterEntry"]));
    }
    size_t split = intervals.size();
    if (restartAfter) {
        size_t count = 0;
        split = 0;
        while (split < intervals.size() && count < *restartAfter) {
            count += eveEntries({intervals[split]}, importedKwh).entries.size();
            ++split;
        }
        if (count != *restartAfter) {
            return "restartAfterEntry " + std::to_string(*restartAfter) + " is not at an interval boundary";
        }
    }

    EveHistory history(memorySize);
    EveEntries emitted = history.addIntervals(std::vector<EnergyEntry>(intervals.begin(), intervals.begin() + split), importedKwh);
    if (restartAfter) {
        JsonVariantConst before = document["beforeRestart"];
        for (const std::string &why : {compareStatus(history, before["historyStatus"], "beforeRestart"),
                                       compareReplay(history, before["historyEntries"].as<JsonArrayConst>(), "beforeRestart"),
                                       compareState(history, before["state"], "beforeRestart"),
                                       reload(history, "restart")}) {
            if (!why.empty()) {
                return why;
            }
        }
        JsonVariantConst after = document["afterRestart"];
        for (const std::string &why : {compareReplay(history, after["historyEntries"].as<JsonArrayConst>(), "afterRestart"),
                                       compareState(history, after["state"], "afterRestart")}) {
            if (!why.empty()) {
                return why;
            }
        }
        const EveEntries rest =
            history.addIntervals(std::vector<EnergyEntry>(intervals.begin() + split, intervals.end()), importedKwh);
        emitted.entries.insert(emitted.entries.end(), rest.entries.begin(), rest.entries.end());
        emitted.overflow.insert(emitted.overflow.end(), rest.overflow.begin(), rest.overflow.end());
    }

    JsonArrayConst entries = document["entries"].as<JsonArrayConst>();
    if (emitted.entries.size() != entries.size()) {
        return std::to_string(emitted.entries.size()) + " entries, expected " + std::to_string(entries.size());
    }
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto time = static_cast<uint32_t>(number(entries[i]["time"]));
        const auto deciwatts = static_cast<uint32_t>(number(entries[i]["deciwatts"]));
        if (emitted.entries[i].time != time || emitted.entries[i].deciwatts != deciwatts) {
            return "entry " + std::to_string(i) + ": " + std::to_string(emitted.entries[i].time) + " " +
                   std::to_string(emitted.entries[i].deciwatts) + " dW, expected " + std::to_string(time) + " " +
                   std::to_string(deciwatts) + " dW";
        }
    }
    JsonArrayConst overflow = document["overflow"].as<JsonArrayConst>();
    if (emitted.overflow.size() != overflow.size()) {
        return std::to_string(emitted.overflow.size()) + " over-range intervals, expected " + std::to_string(overflow.size());
    }
    for (size_t i = 0; i < overflow.size(); ++i) {
        const InstantText start = parseInstant(text(overflow[i]["start"]));
        if (!start.instant || emitted.overflow[i].start != *start.instant ||
            emitted.overflow[i].deciwatts != number(overflow[i]["deciwatts"])) {
            return "over-range interval " + std::to_string(i) + " differs from " + text(overflow[i]["start"]);
        }
    }

    for (const std::string &why : {compareStatus(history, document["historyStatus"], "final"),
                                   compareReplay(history, document["historyEntries"].as<JsonArrayConst>(), "final"),
                                   compareState(history, document["state"], "final"), reload(history, "final blob"),
                                   compareStatus(history, document["historyStatus"], "final after reload")}) {
        if (!why.empty()) {
            return why;
        }
    }
    return "";
}

EveHistory filled(uint16_t memorySize, uint32_t count)
{
    EveHistory history(memorySize);
    for (uint32_t i = 1; i <= count; ++i) {
        history.add(1790644200 + 600 * i, static_cast<uint16_t>(i));
    }
    return history;
}

}

int main(int argc, char **argv)
{
    if (argc != 2) {
        std::fprintf(stderr, "usage: %s <eve-history-dir>\n", argv[0]);
        return 2;
    }
    const std::string base = argv[1];
    Suite suite("eve_history_tests");
    const std::optional<std::string> indexText = readFile(base + "/index.json");
    JsonDocument index;
    if (!indexText || deserializeJson(index, *indexText) || index.as<JsonArrayConst>().size() == 0) {
        std::printf("FAIL eve_history_tests %s/index.json is missing, unreadable or empty\n", base.c_str());
        return 1;
    }
    for (JsonVariantConst item : index.as<JsonArrayConst>()) {
        const std::string path = text(item["path"]);
        suite.run(path.c_str(), [&]() -> std::string {
            const std::optional<std::string> body = readFile(base + "/" + path);
            if (!body) {
                return "cannot read " + base + "/" + path;
            }
            return runFixture(*body);
        });
    }

    suite.run("a full ring of 4032 slots is stored in EVE_HISTORY_MAX_BLOB_BYTES", []() -> std::string {
        const EveHistory history = filled(EVE_MEMORY_SIZE, EVE_MEMORY_SIZE + 10);
        const size_t size = encodeEveHistory(H, history.state()).size();
        return size == EVE_HISTORY_MAX_BLOB_BYTES ? "" : std::to_string(size) + " bytes";
    });
    suite.run("decode refuses a blob of another version", []() -> std::string {
        std::vector<uint8_t> blob = encodeEveHistory(H, filled(16, 3).state());
        blob[0] = EVE_HISTORY_VERSION + 1;
        const std::variant<StoredEveHistory, Invalid> decoded = decodeEveHistory(blob.data(), blob.size(), 16);
        return std::holds_alternative<Invalid>(decoded) ? "" : "accepted";
    });
    suite.run("decode refuses a blob whose size does not match its counters", []() -> std::string {
        std::vector<uint8_t> blob = encodeEveHistory(H, filled(16, 3).state());
        blob.pop_back();
        const std::variant<StoredEveHistory, Invalid> decoded = decodeEveHistory(blob.data(), blob.size(), 16);
        return std::holds_alternative<Invalid>(decoded) ? "" : "accepted";
    });
    suite.run("a request shorter than 6 bytes is refused with its bytes", []() -> std::string {
        EveHistory history = filled(16, 3);
        const uint8_t bytes[] = {0x01, 0x14, 0x02};
        const std::optional<Invalid> invalid = history.handleRequest(bytes, sizeof bytes);
        const std::string want = "Eve history request 011402: 3 bytes, expected at least 6";
        return invalid && invalid->message == want ? "" : (invalid ? invalid->message : "accepted");
    });

    const bool passed = suite.finish();
    std::printf("eve_history_tests: %s\n", passed ? "passed" : "FAILED");
    return passed ? 0 : 1;
}
