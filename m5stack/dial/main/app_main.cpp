#include <cstdio>
#include <cstring>

#include "dial/device.h"
#include "esp_heap_caps.h"
#include "esp_matter_console.h"
#include "flipped/app/app.h"
#include "flipped/dev_wifi.h"
#include "flipped/matter/matter_service.h"
#include "flipped/net/http_client.h"
#include "flipped/ui/app_bridge.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

namespace {

class AppSource : public dial::ModelSource {
public:
    flipped::ui::ScreenModel model() override
    {
        auto screen = flipped::ui::screenFromApp();
        screen.showSpotPrices = flipped::matter::spotPricesEnabled();
        screen.virtualDevices = flipped::matter::virtualDevicesEnabled();
        return screen;
    }

    void act(dial::Action action) override
    {
        switch (action) {
        case dial::Action::openCommissioningWindow:
            flipped::app::openCommissioningWindow();
            return;
        case dial::Action::refreshUsage:
            flipped::app::requestUsageRefresh();
            return;
        case dial::Action::toggleVirtualDevices:
            flipped::matter::setVirtualDevicesEnabled(!flipped::matter::virtualDevicesEnabled());
            return;
        case dial::Action::toggleSpotPrices:
            flipped::matter::setSpotPricesEnabled(!flipped::matter::spotPricesEnabled());
            return;
        case dial::Action::eraseEverything:
            flipped::app::eraseEverything();
            return;
        }
    }
};

AppSource source;

esp_err_t screenCommand(int argc, char** argv)
{
    if (argc != 0) {
        printf("usage: matter esp screen\n");
        return ESP_ERR_INVALID_ARG;
    }
    dial::printScreenshot();
    return ESP_OK;
}

esp_err_t inputCommand(int argc, char** argv)
{
    static const struct {
        const char* name;
        dial::Input input;
    } inputs[] = {{"left", dial::Input::turnLeft}, {"right", dial::Input::turnRight}, {"press", dial::Input::press}, {"hold", dial::Input::hold}};
    if (argc == 1) {
        for (const auto& entry : inputs) {
            if (std::strcmp(argv[0], entry.name) == 0) {
                dial::injectInput(entry.input, 0, 0);
                return ESP_OK;
            }
        }
    }
    printf("usage: matter esp input left|right|press|hold\n");
    return ESP_ERR_INVALID_ARG;
}

void addBoardCommands()
{
    static const esp_matter::console::command_t commands[] = {
        {"screen", "Dial: print the screen as RLE565 base64", screenCommand},
        {"input", "Dial: left, right, press or hold", inputCommand},
    };
    ESP_ERROR_CHECK(esp_matter::console::add_commands(commands, 2));
}

void publish(const flipped::app::Publication& publication)
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
    dial::startDevice(source);
    flipped::net::setTaskMemory({12288, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT});
    flipped::app::prepare(publish);
    flipped::matter::createNode(flipped::app::matterHooks());
    flipped::matter::start();
    flipped::devwifi::begin();
    flipped::matter::restoreSwitches(flipped::app::storedInstanceKey());
#if CONFIG_FLIPPED_EVE_HISTORY
    flipped::matter::restoreEveHistory(flipped::app::storedInstanceKey());
#endif
    addBoardCommands();
    flipped::app::startConsole();
    flipped::app::onUiModelChanged(static_cast<TaskHandle_t>(dial::deviceTask()));
    flipped::app::start();
    flipped::matter::openCommissioningWindow();
}
