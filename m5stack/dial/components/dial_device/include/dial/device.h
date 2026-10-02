#pragma once

#include <cstdint>
#include <string>

#include "flipped/ui/view.h"

namespace dial {

enum class Action : uint8_t { openCommissioningWindow, refreshUsage, eraseEverything, toggleVirtualDevices, toggleSpotPrices };

enum class Input : uint8_t { turnLeft, turnRight, press, hold, tap };

class ModelSource {
public:
    virtual ~ModelSource() = default;
    virtual flipped::ui::ScreenModel model() = 0;
    virtual void act(Action action) = 0;
};

void startDevice(ModelSource& source);
void* deviceTask();
void notifyModelChanged();
void injectInput(Input input, int16_t x, int16_t y);
void printScreenshot();

}
