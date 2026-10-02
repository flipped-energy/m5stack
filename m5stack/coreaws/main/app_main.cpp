#include "coreaws/device.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "flipped/app/app.h"
#include "flipped/dev_wifi.h"
#include "flipped/matter/matter_service.h"
#include "flipped/net/http_client.h"
#include "flipped/ui/app_bridge.h"

namespace {

class AppSource : public coreaws::ModelSource {
public:
    flipped::ui::ScreenModel model() override
    {
        auto screen = flipped::ui::screenFromApp();
        screen.showSpotPrices = flipped::matter::spotPricesEnabled();
        screen.virtualDevices = flipped::matter::virtualDevicesEnabled();
        return screen;
    }

    void act(coreaws::Action action) override
    {
        switch (action) {
        case coreaws::Action::openCommissioningWindow:
            flipped::app::openCommissioningWindow();
            return;
        case coreaws::Action::refreshUsage:
            flipped::app::requestUsageRefresh();
            return;
        case coreaws::Action::toggleVirtualDevices:
            flipped::matter::setVirtualDevicesEnabled(!flipped::matter::virtualDevicesEnabled());
            return;
        case coreaws::Action::toggleSpotPrices:
            flipped::matter::setSpotPricesEnabled(!flipped::matter::spotPricesEnabled());
            return;
        case coreaws::Action::eraseEverything:
            flipped::app::eraseEverything();
            return;
        }
    }
};

AppSource source;

void publish(const flipped::app::Publication &publication)
{
    flipped::matter::publish(flipped::matter::Update{publication.signals, publication.instanceKey,
                                                     publication.dailyLimitRemaining, publication.now,
                                                     publication.ledger, publication.ledgerStep});
}

}

extern "C" void app_main()
{
    flipped::app::checkTimeZones();
    ESP_ERROR_CHECK(nvs_flash_init());
    flipped::matter::loadSetupCode();
    coreaws::startDevice(source);
    flipped::net::setTaskMemory({16384, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT});
    flipped::app::prepare(publish);
    flipped::matter::createNode(flipped::app::matterHooks());
    flipped::matter::start();
    flipped::devwifi::begin();
    flipped::matter::restoreSwitches(flipped::app::storedInstanceKey());
#if CONFIG_FLIPPED_EVE_HISTORY
    flipped::matter::restoreEveHistory(flipped::app::storedInstanceKey());
#endif
    flipped::app::startConsole();
    flipped::app::onUiModelChanged(coreaws::deviceTask());
    flipped::app::start();
    flipped::matter::openCommissioningWindow();
}
