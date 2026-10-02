#include <optional>
#include <string>
#include <utility>

#include <esp_log.h>
#include <esp_matter.h>
#include <esp_system.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>
#include <lib/core/ErrorStr.h>
#include <platform/CHIPDeviceLayer.h>

#include "flipped/matter/ids.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

const char *TAG = "flipped_commissioning";

struct State {
    bool ready = false;
    bool bleAvailable = false;
    std::optional<std::pair<bool, bool>> codeFlags;
    std::pair<std::string, std::string> codes;
};

State &state()
{
    static State instance;
    return instance;
}

void openWindow(intptr_t)
{
    const bool ble = state().bleAvailable;
    const chip::CommissioningWindowAdvertisement mode =
        ble ? chip::CommissioningWindowAdvertisement::kAllSupported : chip::CommissioningWindowAdvertisement::kDnssdOnly;
    const CHIP_ERROR err = chip::Server::GetInstance().GetCommissioningWindowManager().OpenBasicCommissioningWindow(
        chip::System::Clock::Seconds32(COMMISSIONING_WINDOW_S), mode);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "OpenBasicCommissioningWindow: %s", chip::ErrorStr(err));
        return;
    }
    ESP_LOGI(TAG, "commissioning window opened for %u s, %s", static_cast<unsigned>(COMMISSIONING_WINDOW_S),
             ble ? "BLE and DNS-SD" : "DNS-SD only");
}

void restart(intptr_t)
{
    esp_restart();
}

void refresh(State &current)
{
    chip::Server &server = chip::Server::GetInstance();
    CommissioningFacts facts;
    facts.fabricCount = server.GetFabricTable().FabricCount();
    facts.bleAvailable = current.bleAvailable;
    facts.commissioningWindowOpen = server.GetCommissioningWindowManager().IsCommissioningWindowOpen();
    facts.wifiProvisioned = chip::DeviceLayer::ConnectivityMgr().IsWiFiStationProvisioned();
    const std::pair<bool, bool> flags{facts.bleAvailable, facts.wifiProvisioned};
    if (current.codeFlags != flags) {
        current.codeFlags = flags;
        current.codes = onboardingCodes(flags.first, flags.second);
    }
    facts.qrPayload = current.codes.first;
    facts.manualCode = current.codes.second;
    if (hooks().commissioning) {
        hooks().commissioning(facts);
    }
}

void markReady(intptr_t)
{
    State &current = state();
    current.ready = true;
    current.bleAvailable =
        CHIP_DEVICE_CONFIG_ENABLE_CHIPOBLE && chip::Server::GetInstance().GetFabricTable().FabricCount() == 0;
    refresh(current);
}

void factoryReset()
{
    ESP_ERROR_CHECK(esp_matter::factory_reset());
}

}

void onDeviceEvent(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t)
{
    namespace Type = chip::DeviceLayer::DeviceEventType;
    State &current = state();
    switch (event->Type) {
    case Type::kCommissioningComplete:
    case Type::kBLEDeinitialized:
        current.bleAvailable = false;
        break;
    case Type::kFabricRemoved:
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0 && !current.bleAvailable) {
            ESP_LOGW(TAG, "the last fabric was removed while BLE is released: restarting");
            scheduleWork(restart, 0, "esp_restart");
        }
        break;
    case Type::kServerReady:
    case Type::kFabricCommitted:
    case Type::kFabricUpdated:
    case Type::kCommissioningWindowOpened:
    case Type::kCommissioningWindowClosed:
    case Type::kWiFiConnectivityChange:
        break;
    default:
        return;
    }
    if (current.ready) {
        refresh(current);
    }
}

void startCommissioningFacts()
{
    scheduleWork(markReady, 0, "commissioning facts after start");
}

void openCommissioningWindow()
{
    scheduleWork(openWindow, 0, "OpenBasicCommissioningWindow");
}

void unpair()
{
    ESP_LOGW(TAG, "unpair: erasing %s/%s and %s/%s, then esp_matter::factory_reset", NVS_NAMESPACE, NVS_SWITCHES,
             NVS_NAMESPACE, NVS_EVE_ENDPOINT);
    eraseKey(NVS_SWITCHES);
    eraseKey(NVS_EVE_ENDPOINT);
    factoryReset();
}

void eraseEverything()
{
    ESP_LOGW(TAG, "erase everything: erasing %s, then esp_matter::factory_reset", NVS_NAMESPACE);
    eraseKey(nullptr);
    factoryReset();
}

}
