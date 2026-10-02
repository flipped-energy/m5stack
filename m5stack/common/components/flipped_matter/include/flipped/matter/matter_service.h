#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "flipped/core/energy_ledger.h"
#include "flipped/core/signals.h"
#include "flipped/core/types.h"

namespace flipped::matter {

struct CommissioningFacts {
    uint8_t fabricCount = 0;
    bool bleAvailable = false;
    bool commissioningWindowOpen = false;
    bool wifiProvisioned = false;
    std::string qrPayload;
    std::string manualCode;
};

struct EndpointName {
    uint16_t endpoint = 0;
    std::string label;
};

struct Hooks {
    std::function<void(uint16_t endpoint, bool active)> identify;
    std::function<void(uint16_t endpoint, uint8_t effectId, uint8_t effectVariant)> identifyEffect;
    std::function<void(const CommissioningFacts &facts)> commissioning;
    std::function<void(std::vector<EndpointName> labels)> labels;
    std::function<void()> refresh;
    std::function<core::Instant(uint32_t tableHash, core::Instant now)> tariffPublishedAt;
};

struct Update {
    const core::Signals &signals;
    std::optional<std::string> instanceKey;
    std::optional<int64_t> dailyLimitRemaining;
    std::optional<core::Instant> now;
    std::optional<core::Ledger> ledger;
    std::optional<core::LedgerStep> ledgerStep;
};

void loadPreferences();
bool virtualDevicesEnabled();
bool spotPricesEnabled();
void setVirtualDevicesEnabled(bool enabled);
void setSpotPricesEnabled(bool enabled);
void setSpotLinked(bool linked);

void loadSetupCode();
void createNode(Hooks hooks);
void start();
void restoreSwitches(const std::optional<std::string> &instanceKey);
void restoreEveHistory(const std::optional<std::string> &instanceKey);
void publish(const Update &update);
void openCommissioningWindow();
void unpair();
void eraseEverything();

}
