#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "flipped/core/signals.h"
#include "flipped/core/tariff_tables.h"
#include "flipped/core/types.h"

namespace flipped::core {

inline constexpr uint16_t MAXIMUM_METERED_QUANTITIES = 16;

struct MeteredQuantityValue {
    std::vector<uint32_t> componentIds;
    int64_t quantity = 0;
    double kwh = 0;

    bool operator==(const MeteredQuantityValue &other) const
    {
        return componentIds == other.componentIds && quantity == other.quantity;
    }
    bool operator!=(const MeteredQuantityValue &other) const { return !(*this == other); }
};

struct MeteringAttribution {
    std::optional<std::vector<MeteredQuantityValue>> meteredQuantity;
    std::optional<uint32_t> meteredQuantityTimestamp;
    std::optional<Instant> meteredDayStart;
    std::vector<std::string> problems;
};

MeteringAttribution meteringAttribution(const EnergyGroup &energy, const TariffTables &tables, Instant tariffPublishedAt);

}
