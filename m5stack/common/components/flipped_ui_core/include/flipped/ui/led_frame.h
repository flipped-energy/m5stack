#pragma once

#include <array>
#include <cstdint>

#include "flipped/ui/rgb.h"
#include "flipped/ui/view.h"

namespace flipped::ui {

constexpr std::size_t kLedsPerBar = 5;
constexpr std::size_t kLedCount = kLedsPerBar * 2;

struct LedInput {
    Tone plan = Tone::neutral;
    Tone wholesale = Tone::neutral;
    bool pairing = false;
    bool enabled = true;
    uint8_t brightness = 40;
    uint32_t phaseMs = 0;
};

std::array<Rgb, kLedCount> ledFrame(const LedInput& input);
bool ledAnimated(const LedInput& input);

}
