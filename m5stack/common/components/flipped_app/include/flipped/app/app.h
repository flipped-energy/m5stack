#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flipped/app/ui_model.h"
#include "flipped/core/config_check.h"
#include "flipped/core/energy_ledger.h"
#include "flipped/core/signals.h"
#include "flipped/core/types.h"
#include "flipped/matter/matter_service.h"

namespace flipped::app {

UiModel snapshot();
void visitModel(const std::function<void(const UiModel &)> &visit);
void onUiModelChanged(TaskHandle_t notify);
std::optional<core::Instant> now();

void openCommissioningWindow();
void unpair();
void eraseEverything();
void requestUsageRefresh();
core::ConfigResult setToken(std::string_view token);
core::ConfigResult setAccountNumber(std::string_view accountNumber);
core::ConfigResult setNmi(std::string_view nmi);
core::ConfigResult setThresholds(std::optional<double> high, std::optional<double> low);
void onIdentify(std::function<void(uint16_t endpoint, bool active)> handler);
void onIdentifyEffect(std::function<void(uint16_t endpoint, uint8_t effectId, uint8_t effectVariant)> handler);

struct Publication {
    const core::Signals &signals;
    std::optional<std::string> instanceKey;
    std::optional<core::Ledger> ledger;
    std::optional<core::LedgerStep> ledgerStep;
    std::optional<int64_t> dailyLimitRemaining;
    std::optional<core::Instant> now;
};

void checkTimeZones();
void prepare(std::function<void(const Publication &)> publish);
std::optional<std::string> storedInstanceKey();
void start();
void startConsole();
matter::Hooks matterHooks();
void notifyIdentify(uint16_t endpoint, bool active);
void notifyIdentifyEffect(uint16_t endpoint, uint8_t effectId, uint8_t effectVariant);
void setIdentifyLabels(std::vector<EndpointLabel> labels);
void logResources(const char *when);

}
