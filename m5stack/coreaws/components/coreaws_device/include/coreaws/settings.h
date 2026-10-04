#pragma once

#include <cstdint>

namespace coreaws {

struct UiSettings {
    uint8_t brightness = 160;
    bool leds = true;
    uint8_t ledBrightness = 40;
    bool dimAtNight = true;
    bool beep = false;
};

}
