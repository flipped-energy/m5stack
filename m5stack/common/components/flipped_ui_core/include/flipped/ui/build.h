#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "flipped/core/signals.h"
#include "flipped/core/types.h"
#include "flipped/ui/view.h"

namespace flipped::ui {

struct DeviceFacts {
    bool wifiConnected = false;
    std::string wifiName;
    int8_t wifiRssi = 0;
    std::string wifiAddress;
    bool clockSynced = false;
    uint8_t homes = 0;
    bool bluetooth = false;
    bool commissioningWindowOpen = false;
    std::string qrPayload;
    std::string manualCode;
    bool hasToken = false;
    bool tokenRejected = false;
    std::string tokenPreview;
    std::string tokenUrl;
    std::string tokenCode;
    std::optional<double> priceHighThreshold;
    std::optional<double> priceLowThreshold;
    std::optional<uint16_t> dailyLimitRemaining;
    std::string firmwareVersion;
};

ScreenModel buildScreen(const core::Signals& signals, const DeviceFacts& facts, std::optional<core::Instant> now);

}
