#pragma once

#include <array>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "flipped/ui/led_frame.h"

namespace coreaws {

struct TouchState {
    bool screenDown = false;
    int16_t x = 0;
    int16_t y = 0;
    bool buttonDown[3] = {false, false, false};
    bool buttonClicked[3] = {false, false, false};
    uint32_t buttonHeldMs[3] = {0, 0, 0};
};

class Board {
public:
    void begin(TaskHandle_t notify);
    TouchState read();
    bool takeTouchWake();
    void setBrightness(uint8_t level);
    void setLeds(const std::array<flipped::ui::Rgb, flipped::ui::kLedCount>& frame);
    void haptic();
};

}
