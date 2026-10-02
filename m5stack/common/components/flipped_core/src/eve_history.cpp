#include "flipped/core/eve_history.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string_view>
#include <utility>

#include "flipped/core/identity.h"

namespace flipped::core {

namespace {

constexpr uint8_t REFERENCE_RECORD = 0x15;
constexpr uint8_t ENERGY_RECORD = 0x14;
constexpr uint8_t ENERGY_ENTRY_TYPE = 0x1F;
constexpr uint8_t TRANSFER_DONE = 0x00;
constexpr uint8_t REFERENCE_MARK = 0x81;
constexpr uint8_t SIGNATURE[] = {0x04, 0x01, 0x02, 0x02, 0x02, 0x07, 0x02, 0x0F, 0x03};
constexpr uint8_t STATUS_TAIL[] = {0x00, 0x00, 0x00, 0x00, 0x01, 0x01};
constexpr size_t REQUEST_ADDRESS_OFFSET = 2;
constexpr uint32_t SLOT_SECONDS = EVE_SLOT_MINUTES * 60;
constexpr double DECIWATT_MINUTES_PER_KWH = 600000.0;

[[noreturn]] void invalidState(const char *what)
{
    std::fprintf(stderr, "Eve history: %s\n", what);
    std::abort();
}

void putU16(std::vector<uint8_t> &out, uint16_t value)
{
    out.push_back(static_cast<uint8_t>(value));
    out.push_back(static_cast<uint8_t>(value >> 8));
}

void putU32(std::vector<uint8_t> &out, uint32_t value)
{
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<uint8_t>(value >> (8 * i)));
    }
}

void putZeros(std::vector<uint8_t> &out, size_t count)
{
    out.insert(out.end(), count, 0);
}

uint16_t getU16(const uint8_t *in)
{
    return static_cast<uint16_t>(in[0] | (in[1] << 8));
}

uint32_t getU32(const uint8_t *in)
{
    uint32_t value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(in[i]) << (8 * i);
    }
    return value;
}

double roundHalfUp(double value)
{
    const double whole = std::floor(value);
    return value - whole >= 0.5 ? whole + 1 : whole;
}

bool lowerHex(std::string_view text)
{
    return std::all_of(text.begin(), text.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}

std::string hexOf(const uint8_t *bytes, size_t size)
{
    static const char digits[] = "0123456789abcdef";
    std::string text;
    for (size_t i = 0; i < size; ++i) {
        text.push_back(digits[bytes[i] >> 4]);
        text.push_back(digits[bytes[i] & 0x0F]);
    }
    return text;
}

}

double importedKwh(const EnergyEntry &interval)
{
    return interval.gridImportKwh + interval.controlledLoadKwh;
}

EveEntries eveEntries(const std::vector<EnergyEntry> &intervals, EvePick pick)
{
    std::vector<const EnergyEntry *> ordered;
    for (const EnergyEntry &interval : intervals) {
        ordered.push_back(&interval);
    }
    std::sort(ordered.begin(), ordered.end(), [](const EnergyEntry *a, const EnergyEntry *b) { return a->start < b->start; });
    EveEntries result;
    for (const EnergyEntry *interval : ordered) {
        const double kwh = pick(*interval);
        const int minutes = interval->durationMinutes;
        if (!(kwh >= 0) || minutes <= 0 || minutes % EVE_SLOT_MINUTES != 0) {
            std::fprintf(stderr, "Eve history: interval %lld has %f kWh over %d minutes\n",
                         static_cast<long long>(interval->start), kwh, minutes);
            std::abort();
        }
        const double deciwatts = roundHalfUp(kwh * DECIWATT_MINUTES_PER_KWH / minutes);
        if (deciwatts > EVE_MAX_DECIWATTS) {
            result.overflow.push_back(EveOverflow{interval->start, minutes, kwh, deciwatts});
            continue;
        }
        const Instant last = interval->start + static_cast<Instant>(minutes) * 60;
        if (interval->start <= static_cast<Instant>(EVE_EPOCH) || last > std::numeric_limits<uint32_t>::max()) {
            std::fprintf(stderr, "Eve history: interval %lld is outside the 32-bit Eve time range\n",
                         static_cast<long long>(interval->start));
            std::abort();
        }
        for (int k = 1; k <= minutes / EVE_SLOT_MINUTES; ++k) {
            result.entries.push_back(EveEntry{static_cast<uint32_t>(interval->start + static_cast<Instant>(k) * SLOT_SECONDS),
                                              static_cast<uint16_t>(deciwatts)});
        }
    }
    return result;
}

EveHistory::EveHistory(uint16_t memorySize)
{
    if (memorySize < 2) {
        invalidState("memory size below 2");
    }
    state_.memorySize = memorySize;
    state_.slots.assign(memorySize, EveSlot{});
}

EveHistory::EveHistory(EveHistoryState state) : state_(std::move(state))
{
    if (state_.memorySize < 2 || state_.slots.size() != state_.memorySize) {
        invalidState("a state whose slot count is not its memory size");
    }
}

const EveSlot &EveHistory::slot(uint32_t entry) const
{
    const EveSlot &value = state_.slots[entry % state_.memorySize];
    if (value.kind == EveSlotKind::empty) {
        std::fprintf(stderr, "Eve history: entry %u (slot %u) is empty, last entry %u\n", static_cast<unsigned>(entry),
                     static_cast<unsigned>(entry % state_.memorySize), static_cast<unsigned>(state_.lastEntry));
        std::abort();
    }
    return value;
}

void EveHistory::put(uint32_t entry, EveSlot value)
{
    state_.slots[entry % state_.memorySize] = value;
}

void EveHistory::add(uint32_t time, uint16_t deciwatts)
{
    EveHistoryState &s = state_;
    if (time <= EVE_EPOCH) {
        std::fprintf(stderr, "Eve history: entry time %u is not after 2001\n", static_cast<unsigned>(time));
        std::abort();
    }
    if (s.lastEntry > 0 && time < slot(s.lastEntry).time) {
        std::fprintf(stderr, "Eve history: entry time %u is before the newest entry %u\n", static_cast<unsigned>(time),
                     static_cast<unsigned>(slot(s.lastEntry).time));
        std::abort();
    }
    const EveSlot reference{0, 0, EveSlotKind::reference};
    if (s.usedMemory < s.memorySize) {
        ++s.usedMemory;
        s.firstEntry = 0;
        s.lastEntry = s.usedMemory;
    } else {
        ++s.firstEntry;
        s.lastEntry = s.firstEntry + s.usedMemory;
        if (restarted_) {
            put(s.lastEntry, reference);
            ++s.firstEntry;
            s.lastEntry = s.firstEntry + s.usedMemory;
            restarted_ = false;
        }
    }
    if (s.refTime == 0) {
        s.refTime = time - EVE_EPOCH;
        put(s.lastEntry, reference);
        ++s.lastEntry;
        ++s.usedMemory;
    }
    put(s.lastEntry, EveSlot{time, deciwatts, EveSlotKind::energy});
}

EveEntries EveHistory::addIntervals(const std::vector<EnergyEntry> &intervals, EvePick pick)
{
    EveEntries result = eveEntries(intervals, pick);
    for (const EveEntry &entry : result.entries) {
        add(entry.time, entry.deciwatts);
    }
    return result;
}

std::optional<std::vector<uint8_t>> EveHistory::historyStatus() const
{
    const EveHistoryState &s = state_;
    if (s.lastEntry == 0) {
        return std::nullopt;
    }
    const EveSlot &newest = slot(s.lastEntry);
    const bool full = s.usedMemory >= s.memorySize;
    std::vector<uint8_t> out;
    out.reserve(EVE_STATUS_BYTES);
    putU32(out, newest.time - s.refTime - EVE_EPOCH);
    putZeros(out, 4);
    putU32(out, s.refTime);
    out.insert(out.end(), std::begin(SIGNATURE), std::end(SIGNATURE));
    putU16(out, full ? s.usedMemory : static_cast<uint16_t>(s.usedMemory + 1));
    putU16(out, s.memorySize);
    putU32(out, full ? s.firstEntry + 1 : s.firstEntry);
    out.insert(out.end(), std::begin(STATUS_TAIL), std::end(STATUS_TAIL));
    return out;
}

std::optional<Invalid> EveHistory::handleRequest(const uint8_t *bytes, size_t size)
{
    if (size < EVE_REQUEST_MIN_BYTES) {
        return Invalid{"Eve history request " + hexOf(bytes, size) + ": " + std::to_string(size) +
                       " bytes, expected at least " + std::to_string(EVE_REQUEST_MIN_BYTES)};
    }
    const uint32_t address = getU32(bytes + REQUEST_ADDRESS_OFFSET);
    cursor_.currentEntry = address == 0 ? 1 : address;
    cursor_.transfer = true;
    return std::nullopt;
}

std::vector<uint8_t> EveHistory::historyEntries()
{
    const EveHistoryState &s = state_;
    if (!cursor_.transfer || cursor_.currentEntry > s.lastEntry) {
        cursor_.transfer = false;
        return {TRANSFER_DONE};
    }
    std::vector<uint8_t> out;
    out.reserve(EVE_ENTRIES_MAX_BYTES);
    for (size_t i = 0; i < EVE_RECORDS_PER_READ; ++i) {
        const uint32_t entry = cursor_.currentEntry;
        const EveSlot &value = slot(entry);
        if (value.kind == EveSlotKind::reference || cursor_.setTime || entry == s.firstEntry + 1) {
            out.push_back(REFERENCE_RECORD);
            putU32(out, entry);
            putU32(out, 1);
            out.push_back(REFERENCE_MARK);
            putU32(out, s.refTime);
            putZeros(out, 7);
            cursor_.setTime = false;
        } else {
            out.push_back(ENERGY_RECORD);
            putU32(out, entry);
            putU32(out, value.time - s.refTime - EVE_EPOCH);
            out.push_back(ENERGY_ENTRY_TYPE);
            putZeros(out, 4);
            putU16(out, value.deciwatts);
            putZeros(out, 4);
        }
        ++cursor_.currentEntry;
        if (cursor_.currentEntry > s.lastEntry) {
            break;
        }
    }
    return out;
}

std::vector<uint8_t> encodeEveHistory(const std::string &h, const EveHistoryState &state)
{
    if (h.size() != INSTANCE_HASH_CHARS || !lowerHex(h)) {
        std::fprintf(stderr, "Eve history with h '%s' cannot be stored\n", h.c_str());
        std::abort();
    }
    if (state.lastEntry == 0 || state.slots.size() != state.memorySize) {
        invalidState("a history with no entry or a slot count other than its memory size cannot be stored");
    }
    const uint32_t count = std::min<uint32_t>(state.lastEntry, state.memorySize);
    std::vector<uint8_t> blob;
    blob.reserve(EVE_HISTORY_HEADER_BYTES + EVE_HISTORY_RECORD_BYTES * count);
    blob.push_back(EVE_HISTORY_VERSION);
    blob.insert(blob.end(), h.begin(), h.end());
    putU32(blob, state.refTime);
    putU32(blob, state.firstEntry);
    putU16(blob, state.usedMemory);
    for (uint32_t entry = state.lastEntry - count + 1; entry <= state.lastEntry; ++entry) {
        const EveSlot &value = state.slots[entry % state.memorySize];
        switch (value.kind) {
        case EveSlotKind::empty:
            invalidState("an empty slot inside the stored range");
        case EveSlotKind::reference:
            putU32(blob, 0);
            putU16(blob, 0);
            break;
        case EveSlotKind::energy:
            putU32(blob, value.time);
            putU16(blob, value.deciwatts);
            break;
        }
    }
    return blob;
}

std::variant<StoredEveHistory, Invalid> decodeEveHistory(const uint8_t *data, size_t size, uint16_t memorySize)
{
    if (memorySize < 2) {
        invalidState("memory size below 2");
    }
    if (size < EVE_HISTORY_HEADER_BYTES) {
        return Invalid{"Eve history blob is " + std::to_string(size) + " bytes, shorter than its " +
                       std::to_string(EVE_HISTORY_HEADER_BYTES) + "-byte header"};
    }
    if (data[0] != EVE_HISTORY_VERSION) {
        return Invalid{"Eve history blob version " + std::to_string(data[0]) + ", expected " +
                       std::to_string(EVE_HISTORY_VERSION)};
    }
    StoredEveHistory stored;
    stored.h.assign(reinterpret_cast<const char *>(data + 1), INSTANCE_HASH_CHARS);
    if (!lowerHex(stored.h)) {
        return Invalid{"Eve history blob h is not 16 lower-case hexadecimal characters"};
    }
    EveHistoryState &state = stored.state;
    state.memorySize = memorySize;
    state.refTime = getU32(data + 17);
    state.firstEntry = getU32(data + 21);
    state.usedMemory = getU16(data + 25);
    if (state.usedMemory == 0 || state.usedMemory > memorySize) {
        return Invalid{"Eve history blob usedMemory " + std::to_string(state.usedMemory) + " is outside 1 to " +
                       std::to_string(memorySize)};
    }
    if (state.usedMemory < memorySize && state.firstEntry != 0) {
        return Invalid{"Eve history blob firstEntry " + std::to_string(state.firstEntry) +
                       " is not 0 while the ring is not full"};
    }
    const uint64_t lastEntry = static_cast<uint64_t>(state.firstEntry) + state.usedMemory;
    if (lastEntry > std::numeric_limits<uint32_t>::max()) {
        return Invalid{"Eve history blob lastEntry " + std::to_string(lastEntry) + " does not fit 32 bits"};
    }
    state.lastEntry = static_cast<uint32_t>(lastEntry);
    if (state.refTime == 0) {
        return Invalid{"Eve history blob refTime is 0"};
    }
    const uint32_t count = std::min<uint32_t>(state.lastEntry, memorySize);
    const size_t expected = EVE_HISTORY_HEADER_BYTES + EVE_HISTORY_RECORD_BYTES * count;
    if (size != expected) {
        return Invalid{"Eve history blob is " + std::to_string(size) + " bytes, expected " + std::to_string(expected) +
                       " for " + std::to_string(count) + " slots"};
    }
    state.slots.assign(memorySize, EveSlot{});
    const uint8_t *record = data + EVE_HISTORY_HEADER_BYTES;
    for (uint32_t entry = state.lastEntry - count + 1; entry <= state.lastEntry; ++entry) {
        const uint32_t time = getU32(record);
        const uint16_t deciwatts = getU16(record + 4);
        record += EVE_HISTORY_RECORD_BYTES;
        if (time == 0 && deciwatts != 0) {
            return Invalid{"Eve history blob entry " + std::to_string(entry) + " has time 0 and " +
                           std::to_string(deciwatts) + " deciwatts"};
        }
        if (time != 0 && time <= EVE_EPOCH) {
            return Invalid{"Eve history blob entry " + std::to_string(entry) + " has time " + std::to_string(time) +
                           ", not after 2001"};
        }
        state.slots[entry % memorySize] =
            time == 0 ? EveSlot{0, 0, EveSlotKind::reference} : EveSlot{time, deciwatts, EveSlotKind::energy};
    }
    if (state.slots[state.lastEntry % memorySize].kind != EveSlotKind::energy) {
        return Invalid{"Eve history blob newest entry " + std::to_string(state.lastEntry) + " is not an energy entry"};
    }
    return stored;
}

}
