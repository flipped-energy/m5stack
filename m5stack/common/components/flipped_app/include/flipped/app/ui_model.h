#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "flipped/core/signals.h"

namespace flipped::app {

struct AccountChoice {
    std::string accountNumber;
    std::string siteAddress;
};

struct WifiInfo {
    std::string ssid;
    int8_t rssi = 0;
    std::string ipv4;
};

struct EndpointLabel {
    uint16_t endpoint = 0;
    std::string label;
};

struct UiModel {
    core::Signals signals;
    bool wifiProvisioned = false;
    bool wifiConnected = false;
    WifiInfo wifi;
    bool clockSynced = false;
    uint8_t fabricCount = 0;
    bool bleAvailable = false;
    bool commissioningWindowOpen = false;
    std::string qrPayload;
    std::string manualCode;
    bool hasToken = false;
    bool tokenRejected = false;
    std::string tokenPreview;
    std::vector<AccountChoice> eligibleAccounts;
    std::vector<std::string> candidateNmis;
    std::optional<double> priceHighThresholdCentsPerKwh;
    std::optional<double> priceLowThresholdCentsPerKwh;
    std::optional<uint16_t> dailyLimitRemaining;
    std::vector<EndpointLabel> identifyLabels;
    std::string firmwareVersion;
};

}
