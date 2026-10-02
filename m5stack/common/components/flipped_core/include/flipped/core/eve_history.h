#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "flipped/core/parse.h"
#include "flipped/core/signals.h"
#include "flipped/core/types.h"

namespace flipped::core {

inline constexpr uint32_t EVE_EPOCH = 978307200;
inline constexpr uint16_t EVE_MEMORY_SIZE = 4032;
inline constexpr size_t EVE_RECORDS_PER_READ = 11;
inline constexpr uint32_t EVE_MAX_DECIWATTS = 0xFFFF;
inline constexpr int EVE_SLOT_MINUTES = 10;
inline constexpr size_t EVE_STATUS_BYTES = 35;
inline constexpr size_t EVE_REQUEST_MIN_BYTES = 6;
inline constexpr size_t EVE_REFERENCE_RECORD_BYTES = 21;
inline constexpr size_t EVE_ENERGY_RECORD_BYTES = 20;
inline constexpr size_t EVE_ENTRIES_MAX_BYTES = EVE_RECORDS_PER_READ * EVE_REFERENCE_RECORD_BYTES;

inline constexpr uint8_t EVE_HISTORY_VERSION = 1;
inline constexpr size_t EVE_HISTORY_HEADER_BYTES = 27;
inline constexpr size_t EVE_HISTORY_RECORD_BYTES = 6;
inline constexpr size_t EVE_HISTORY_MAX_BLOB_BYTES =
    EVE_HISTORY_HEADER_BYTES + EVE_HISTORY_RECORD_BYTES * static_cast<size_t>(EVE_MEMORY_SIZE);

enum class EveSlotKind : uint8_t { empty, energy, reference };

struct EveSlot {
    uint32_t time = 0;
    uint16_t deciwatts = 0;
    EveSlotKind kind = EveSlotKind::empty;
};

struct EveHistoryState {
    uint16_t memorySize = EVE_MEMORY_SIZE;
    uint32_t firstEntry = 0;
    uint32_t lastEntry = 0;
    uint16_t usedMemory = 0;
    uint32_t refTime = 0;
    std::vector<EveSlot> slots;
};

struct EveEntry {
    uint32_t time = 0;
    uint16_t deciwatts = 0;
};

struct EveOverflow {
    Instant start = 0;
    int durationMinutes = 0;
    double kwh = 0;
    double deciwatts = 0;
};

struct EveEntries {
    std::vector<EveEntry> entries;
    std::vector<EveOverflow> overflow;
};

struct EveCursor {
    uint32_t currentEntry = 1;
    bool transfer = false;
    bool setTime = true;
};

using EvePick = double (*)(const EnergyEntry &interval);

double importedKwh(const EnergyEntry &interval);
EveEntries eveEntries(const std::vector<EnergyEntry> &intervals, EvePick pick);

class EveHistory {
public:
    explicit EveHistory(uint16_t memorySize);
    explicit EveHistory(EveHistoryState state);

    const EveHistoryState &state() const { return state_; }
    void add(uint32_t time, uint16_t deciwatts);
    EveEntries addIntervals(const std::vector<EnergyEntry> &intervals, EvePick pick);

    std::optional<std::vector<uint8_t>> historyStatus() const;
    std::optional<Invalid> handleRequest(const uint8_t *bytes, size_t size);
    std::vector<uint8_t> historyEntries();

    EveCursor cursor() const { return cursor_; }
    void restore(const EveCursor &cursor) { cursor_ = cursor; }

private:
    const EveSlot &slot(uint32_t entry) const;
    void put(uint32_t entry, EveSlot value);

    EveHistoryState state_;
    EveCursor cursor_;
    bool restarted_ = true;
};

struct StoredEveHistory {
    std::string h;
    EveHistoryState state;
};

std::vector<uint8_t> encodeEveHistory(const std::string &h, const EveHistoryState &state);
std::variant<StoredEveHistory, Invalid> decodeEveHistory(const uint8_t *data, size_t size, uint16_t memorySize);

}
