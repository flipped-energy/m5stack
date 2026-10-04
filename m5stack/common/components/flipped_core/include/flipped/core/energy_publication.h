#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "flipped/core/energy_ledger.h"
#include "flipped/core/signals.h"
#include "flipped/core/types.h"

namespace flipped::core {

inline constexpr int64_t ENERGY_MARK_SPACING_MS = 1000;
inline constexpr uint64_t ENERGY_MWH_MAX = 4611686018427387904ULL;

struct EnergyMeasurementValue {
    int64_t energyMwh = 0;
    std::optional<uint32_t> startTimestamp;
    uint32_t endTimestamp = 0;

    bool operator==(const EnergyMeasurementValue &other) const
    {
        return energyMwh == other.energyMwh && startTimestamp == other.startTimestamp &&
               endTimestamp == other.endTimestamp;
    }
    bool operator!=(const EnergyMeasurementValue &other) const { return !(*this == other); }
};

struct EnergyPublication {
    std::optional<EnergyMeasurementValue> cumulativeImported;
    std::optional<EnergyMeasurementValue> cumulativeExported;
    std::optional<EnergyMeasurementValue> periodicImported;
    std::optional<EnergyMeasurementValue> periodicExported;
    std::optional<uint32_t> cumulativeResetTimestamp;
    bool cumulativeEvent = false;
    bool periodicEvent = false;
    std::vector<std::string> problems;
};

enum class EnergyAttribute : uint8_t {
    cumulativeImported,
    cumulativeExported,
    periodicImported,
    periodicExported,
    cumulativeReset,
};
inline constexpr size_t ENERGY_ATTRIBUTE_COUNT = 5;

EnergyPublication energyPublication(const EnergyGroup &energy, const std::optional<Ledger> &ledger,
                                    const std::optional<LedgerStep> &step, const EnergyPublication &previous);
std::vector<EnergyAttribute> changedEnergyAttributes(const EnergyPublication &before, const EnergyPublication &after);
std::optional<int64_t> deferredMarkAt(std::optional<int64_t> lastMarkMs, int64_t nowMs);

}
