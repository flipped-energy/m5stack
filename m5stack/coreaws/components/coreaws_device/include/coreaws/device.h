#pragma once

#include <cstdint>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flipped/ui/navigator.h"
#include "flipped/ui/view.h"

namespace coreaws {

enum class Action : uint8_t { openCommissioningWindow, refreshUsage, eraseEverything, toggleVirtualDevices, toggleSpotPrices };

class ModelSource {
public:
    virtual ~ModelSource() = default;
    virtual flipped::ui::ScreenModel model() = 0;
    virtual void act(Action action) = 0;
};

void startDevice(ModelSource& source);
TaskHandle_t deviceTask();
void notifyModelChanged();
void injectInput(flipped::ui::Input input, int16_t x, int16_t y);
std::string screenshotBase64();

}
