#include "flipped/ui/app_bridge.h"

#include <memory>
#include <mutex>

#include "esp_log.h"
#include "flipped/app/app.h"
#include "flipped/provision/server.h"
#include "flipped/ui/build.h"

namespace flipped::ui {

namespace {

constexpr const char* kTag = "ui_bridge";

std::mutex lock;
std::unique_ptr<provision::Server> server;
std::string tokenUrl;
std::string tokenError;

void updateProvisioning(const app::UiModel& model)
{
    bool wanted = model.fabricCount > 0 && model.wifiConnected && !model.wifi.ipv4.empty() && (!model.hasToken || model.tokenRejected);
    std::lock_guard<std::mutex> guard(lock);
    if (!wanted) {
        if (server && server->running()) {
            server->stop();
            ESP_LOGI(kTag, "token page closed");
        }
        tokenUrl.clear();
        return;
    }
    if (!server) {
        server = std::make_unique<provision::Server>([](const provision::Outcome& outcome) {
            ESP_LOGI(kTag, "token page: %d %s", outcome.status, outcome.text.c_str());
        });
    }
    if (!server->running()) {
        provision::Start started = server->start(model.wifi.ipv4);
        if (started.started) {
            tokenUrl = started.url;
            tokenError.clear();
            ESP_LOGI(kTag, "token page open on %s", model.wifi.ipv4.c_str());
        } else {
            tokenError = started.error;
            ESP_LOGE(kTag, "token page did not start: %s", started.error.c_str());
        }
    }
}

}

ScreenModel screenFromApp()
{
    ScreenModel screen;
    app::visitModel([&screen](const app::UiModel &model) { screen = fromModel(model); });
    return screen;
}

ScreenModel fromModel(const app::UiModel &model)
{
    updateProvisioning(model);
    DeviceFacts facts;
    facts.wifiConnected = model.wifiConnected;
    facts.wifiName = model.wifi.ssid;
    facts.wifiRssi = model.wifi.rssi;
    facts.wifiAddress = model.wifi.ipv4;
    facts.clockSynced = model.clockSynced;
    facts.homes = model.fabricCount;
    facts.bluetooth = model.bleAvailable;
    facts.commissioningWindowOpen = model.commissioningWindowOpen;
    facts.qrPayload = model.qrPayload;
    facts.manualCode = model.manualCode;
    facts.hasToken = model.hasToken;
    facts.tokenRejected = model.tokenRejected;
    facts.tokenPreview = model.tokenPreview;
    {
        std::lock_guard<std::mutex> guard(lock);
        facts.tokenUrl = tokenUrl;
        facts.tokenCode = tokenError;
    }
    facts.priceHighThreshold = model.priceHighThresholdCentsPerKwh;
    facts.priceLowThreshold = model.priceLowThresholdCentsPerKwh;
    facts.dailyLimitRemaining = model.dailyLimitRemaining;
    facts.firmwareVersion = model.firmwareVersion;
    return buildScreen(model.signals, facts, app::now());
}

}
