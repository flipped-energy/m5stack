#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "flipped/core/parse.h"
#include "flipped/core/signals.h"
#include "flipped/core/types.h"

namespace flipped::core {

inline constexpr uint8_t LEDGER_VERSION = 1;
inline constexpr size_t LEDGER_BLOB_BYTES = 49;

struct Ledger {
    std::string h;
    std::optional<Instant> startedAt;
    Instant through = 0;
    uint64_t importedMwh = 0;
    uint64_t exportedMwh = 0;
};

struct LedgerStep {
    size_t took = 0;
    bool newLedger = false;
    std::optional<Instant> firstStart;
    std::optional<Instant> lastEnd;
    uint64_t deltaImportedMwh = 0;
    uint64_t deltaExportedMwh = 0;
};

Ledger ledgerFor(std::string_view instanceHash, const std::optional<Ledger> &stored);
std::optional<std::string> ledgerRefusal(const Ledger &ledger, const std::vector<EnergyEntry> &intervals);
LedgerStep advance(Ledger &ledger, const std::vector<EnergyEntry> &intervals);
std::array<uint8_t, LEDGER_BLOB_BYTES> encodeLedger(const Ledger &ledger);
std::variant<Ledger, Invalid> decodeLedger(const uint8_t *data, size_t size);

}
