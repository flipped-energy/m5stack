#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "flipped/core/signals.h"

namespace flipped::core {

inline constexpr Instant MATTER_EPOCH_UNIX_S = 946684800;
inline constexpr size_t FLIPPED_CLUSTER_TEXT_MAX_BYTES = 512;
inline constexpr size_t FLIPPED_CLUSTER_CODE_MAX_BYTES = 32;
inline constexpr size_t FLIPPED_CLUSTER_NAME_MAX_BYTES = 64;

struct FaultValues {
    std::optional<std::string> code;
    std::optional<uint16_t> httpStatus;
    std::optional<std::string> text;
    std::optional<uint32_t> bodyBytes;
};

struct FlippedClusterValues {
    FaultValues tariffFault;
    std::optional<uint8_t> structure;
    std::optional<bool> spotLinked;
    std::optional<uint8_t> ratePeriod;
    std::optional<std::string> ratePeriodName;
    std::optional<int64_t> currentRate;
    std::optional<uint32_t> rateAllowanceKwh;
    std::optional<int64_t> rateAfterAllowance;
    std::optional<uint32_t> nextRateChange;
    std::optional<bool> wholesaleLinkedRate;
    std::optional<int64_t> fixedRateComponent;
    std::optional<int64_t> wholesaleRateCap;
    FaultValues priceFault;
    std::optional<int32_t> wholesalePrice;
    std::optional<uint32_t> wholesaleIntervalStart;
    std::optional<uint8_t> wholesalePriceLevel;
    std::optional<bool> wholesalePriceNegative;
    FaultValues energyFault;
    std::optional<uint32_t> latestIntervalEnd;
    std::optional<uint32_t> lastDayStart;
    std::optional<int64_t> lastDayGridImportMwh;
    std::optional<int64_t> lastDayControlledLoadMwh;
    std::optional<int64_t> lastDaySolarExportMwh;
    std::optional<int32_t> lastDayUsageCostCents;
    std::optional<int32_t> lastDayFeedInCreditCents;
    FaultValues accountFault;
    std::optional<uint32_t> tokenExpiresAt;
    std::optional<bool> tokenExpiringSoon;
    std::optional<uint16_t> dailyLimitRemaining;
    std::vector<std::string> problems;
};

int64_t matterMoney(int64_t rateKey);
std::string utf8Prefix(const std::string &text, size_t maxBytes);
FlippedClusterValues flippedClusterValues(const Signals &signals, std::optional<int64_t> dailyLimitRemaining);

}
