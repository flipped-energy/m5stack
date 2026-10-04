#include "flipped/core/energy_ledger.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <utility>

#include "flipped/core/identity.h"

namespace flipped::core {

namespace {

uint64_t milliwattHours(double kwh, Instant start, const char *name)
{
    if (!(kwh >= 0)) {
        std::fprintf(stderr, "interval %lld has %s %f kWh\n", static_cast<long long>(start), name, kwh);
        std::abort();
    }
    return static_cast<uint64_t>(std::llround(kwh * 1000000.0));
}

void put(uint8_t *out, uint64_t value)
{
    for (size_t i = 0; i < 8; ++i) {
        out[i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

uint64_t get(const uint8_t *in)
{
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(in[i]) << (8 * i);
    }
    return value;
}

std::vector<const EnergyEntry *> freshIntervals(const Ledger &ledger, const std::vector<EnergyEntry> &intervals)
{
    std::vector<const EnergyEntry *> fresh;
    for (const EnergyEntry &interval : intervals) {
        if (!ledger.startedAt || interval.start >= ledger.through) {
            fresh.push_back(&interval);
        }
    }
    std::sort(fresh.begin(), fresh.end(), [](const EnergyEntry *a, const EnergyEntry *b) { return a->start < b->start; });
    return fresh;
}

std::string kwhText(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.15g", value);
    if (std::strtod(text, nullptr) != value) {
        std::snprintf(text, sizeof text, "%.17g", value);
    }
    return text;
}

bool lowerHex(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

}

Ledger ledgerFor(std::string_view instanceHash, const std::optional<Ledger> &stored)
{
    if (stored && stored->h == instanceHash) {
        return *stored;
    }
    Ledger ledger;
    ledger.h = std::string(instanceHash);
    return ledger;
}

std::optional<std::string> ledgerRefusal(const Ledger &ledger, const std::vector<EnergyEntry> &intervals)
{
    for (const EnergyEntry *interval : freshIntervals(ledger, intervals)) {
        const std::pair<const char *, double> values[] = {{"gridImportKwh", interval->gridImportKwh},
                                                          {"controlledLoadKwh", interval->controlledLoadKwh},
                                                          {"solarExportKwh", interval->solarExportKwh}};
        for (const auto &[name, kwh] : values) {
            if (!(std::isfinite(kwh) && kwh >= 0)) {
                return "energy interval " + interval->local + " (" + std::to_string(interval->start) + ") has " + name +
                       " " + kwhText(kwh);
            }
        }
    }
    return std::nullopt;
}

LedgerStep advance(Ledger &ledger, const std::vector<EnergyEntry> &intervals)
{
    const bool wasNew = !ledger.startedAt;
    const std::vector<const EnergyEntry *> fresh = freshIntervals(ledger, intervals);
    LedgerStep step;
    for (const EnergyEntry *interval : fresh) {
        if (interval->durationMinutes <= 0) {
            std::fprintf(stderr, "interval %lld lasts %d minutes\n", static_cast<long long>(interval->start),
                         interval->durationMinutes);
            std::abort();
        }
        const uint64_t imported = milliwattHours(interval->gridImportKwh, interval->start, "gridImportKwh") +
                                  milliwattHours(interval->controlledLoadKwh, interval->start, "controlledLoadKwh");
        const uint64_t exported = milliwattHours(interval->solarExportKwh, interval->start, "solarExportKwh");
        if (!ledger.startedAt) {
            ledger.startedAt = interval->start;
        }
        ledger.importedMwh += imported;
        ledger.exportedMwh += exported;
        ledger.through = interval->start + static_cast<Instant>(interval->durationMinutes) * 60;
        step.deltaImportedMwh += imported;
        step.deltaExportedMwh += exported;
        if (!step.firstStart) {
            step.firstStart = interval->start;
        }
        step.lastEnd = ledger.through;
        ++step.took;
    }
    step.newLedger = wasNew && step.took > 0;
    return step;
}

std::array<uint8_t, LEDGER_BLOB_BYTES> encodeLedger(const Ledger &ledger)
{
    if (!ledger.startedAt || ledger.h.size() != INSTANCE_HASH_CHARS) {
        std::fprintf(stderr, "ledger with h of %zu characters and %s startedAt cannot be stored\n", ledger.h.size(),
                     ledger.startedAt ? "a" : "no");
        std::abort();
    }
    std::array<uint8_t, LEDGER_BLOB_BYTES> blob{};
    blob[0] = LEDGER_VERSION;
    std::copy(ledger.h.begin(), ledger.h.end(), blob.begin() + 1);
    put(blob.data() + 17, static_cast<uint64_t>(*ledger.startedAt));
    put(blob.data() + 25, static_cast<uint64_t>(ledger.through));
    put(blob.data() + 33, ledger.importedMwh);
    put(blob.data() + 41, ledger.exportedMwh);
    return blob;
}

std::variant<Ledger, Invalid> decodeLedger(const uint8_t *data, size_t size)
{
    if (size != LEDGER_BLOB_BYTES) {
        return Invalid{"ledger blob is " + std::to_string(size) + " bytes, expected " + std::to_string(LEDGER_BLOB_BYTES)};
    }
    if (data[0] != LEDGER_VERSION) {
        return Invalid{"ledger blob version " + std::to_string(data[0]) + ", expected " + std::to_string(LEDGER_VERSION)};
    }
    Ledger ledger;
    ledger.h.assign(reinterpret_cast<const char *>(data + 1), INSTANCE_HASH_CHARS);
    if (!lowerHex(ledger.h)) {
        return Invalid{"ledger blob h is not 16 lower-case hexadecimal characters"};
    }
    ledger.startedAt = static_cast<Instant>(get(data + 17));
    ledger.through = static_cast<Instant>(get(data + 25));
    ledger.importedMwh = get(data + 33);
    ledger.exportedMwh = get(data + 41);
    return ledger;
}

}
