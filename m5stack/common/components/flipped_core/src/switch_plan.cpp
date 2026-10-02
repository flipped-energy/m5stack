#include "flipped/core/switch_plan.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace flipped::core {

namespace {

bool lowerHex(std::string_view text)
{
    return text.size() == INSTANCE_HASH_CHARS && std::all_of(text.begin(), text.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

size_t switchIndex(std::string_view key)
{
    for (size_t i = 0; i < SWITCH_COUNT; ++i) {
        if (key == SWITCH_IDENTITIES[i].key) {
            return i;
        }
    }
    std::fprintf(stderr, "switch key %.*s is not in SWITCH_IDENTITIES\n", static_cast<int>(key.size()), key.data());
    std::abort();
}

}

SwitchPlan switchPlan(const std::optional<std::string> &instanceKey, const std::optional<StoredSwitches> &stored)
{
    SwitchPlan plan;
    if (!instanceKey) {
        return plan;
    }
    plan.h = instanceHash(*instanceKey);
    const bool resume = stored && stored->h == plan.h;
    plan.action = resume ? SwitchAction::resume : SwitchAction::replace;
    for (size_t i = 0; i < SWITCH_COUNT; ++i) {
        SwitchEndpoint &endpoint = plan.endpoints[i];
        endpoint.key = SWITCH_IDENTITIES[i].key;
        endpoint.nodeLabel = SWITCH_IDENTITIES[i].nodeLabel;
        endpoint.uniqueId = uniqueId(plan.h, SWITCH_IDENTITIES[i].suffix);
        if (resume) {
            endpoint.endpointId = stored->endpointIds[i];
        }
        if (!endpoint.endpointId) {
            plan.store = true;
        }
    }
    return plan;
}

std::array<std::optional<bool>, SWITCH_COUNT> switchValues(const Signals &signals)
{
    std::array<std::optional<bool>, SWITCH_COUNT> values;
    if (!signals.tariff.fault) {
        values[0] = signals.tariff.peak;
        values[1] = signals.tariff.offPeak;
        values[2] = signals.tariff.period.segment.band == Band::shoulder;
    }
    if (!signals.price.fault) {
        values[3] = signals.price.priceHigh;
        values[4] = signals.price.priceLow;
    }
    return values;
}

std::array<uint8_t, STORED_SWITCHES_BYTES> encodeStoredSwitches(std::string_view h,
                                                                const std::array<uint16_t, SWITCH_COUNT> &endpointIds)
{
    if (!lowerHex(h)) {
        std::fprintf(stderr, "switch blob h '%.*s' is not 16 lower-case hexadecimal characters\n", static_cast<int>(h.size()),
                     h.data());
        std::abort();
    }
    std::array<uint8_t, STORED_SWITCHES_BYTES> blob{};
    std::copy(h.begin(), h.end(), blob.begin());
    for (size_t i = 0; i < SWITCH_COUNT; ++i) {
        blob[INSTANCE_HASH_CHARS + 2 * i] = static_cast<uint8_t>(endpointIds[i] & 0xFF);
        blob[INSTANCE_HASH_CHARS + 2 * i + 1] = static_cast<uint8_t>(endpointIds[i] >> 8);
    }
    return blob;
}

std::variant<StoredSwitches, Invalid> decodeStoredSwitches(const uint8_t *data, size_t size)
{
    if (size != STORED_SWITCHES_BYTES && size != FOUR_SWITCH_BLOB_BYTES) {
        return Invalid{"switch blob is " + std::to_string(size) + " bytes, expected " +
                       std::to_string(STORED_SWITCHES_BYTES) + " or " + std::to_string(FOUR_SWITCH_BLOB_BYTES)};
    }
    StoredSwitches stored;
    stored.h.assign(reinterpret_cast<const char *>(data), INSTANCE_HASH_CHARS);
    if (!lowerHex(stored.h)) {
        return Invalid{"switch blob h is not 16 lower-case hexadecimal characters"};
    }
    const size_t count = (size - INSTANCE_HASH_CHARS) / 2;
    for (size_t i = 0; i < count; ++i) {
        const size_t slot = count == SWITCH_COUNT ? i : switchIndex(FOUR_SWITCH_BLOB_KEYS[i]);
        stored.endpointIds[slot] = static_cast<uint16_t>(data[INSTANCE_HASH_CHARS + 2 * i] |
                                                         (data[INSTANCE_HASH_CHARS + 2 * i + 1] << 8));
    }
    return stored;
}

}
