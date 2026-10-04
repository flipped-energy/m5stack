#include "flipped/ui/led_frame.h"

#include "flipped/ui/theme.h"

namespace flipped::ui {

namespace {

Rgb scale(Rgb colour, uint32_t level)
{
    return Rgb{static_cast<uint8_t>(colour.r * level / 255), static_cast<uint8_t>(colour.g * level / 255),
               static_cast<uint8_t>(colour.b * level / 255)};
}

uint32_t pulse(uint32_t phaseMs, uint32_t periodMs)
{
    uint32_t t = phaseMs % periodMs;
    uint32_t half = periodMs / 2;
    uint32_t ramp = t < half ? t : periodMs - t;
    return 60 + 195 * ramp / half;
}

bool animatedTone(Tone tone) { return tone == Tone::spike || tone == Tone::fault; }

void fillBar(std::array<Rgb, kLedCount>& frame, std::size_t first, Tone tone, const LedInput& input)
{
    uint32_t level = input.brightness;
    if (animatedTone(tone)) {
        level = level * pulse(input.phaseMs, tone == Tone::spike ? 700 : 1600) / 255;
    }
    if (tone == Tone::neutral) {
        level = level / 4;
    }
    Rgb colour = scale(theme::toneColour(tone), level);
    for (std::size_t i = 0; i < kLedsPerBar; ++i) {
        frame[first + i] = colour;
    }
}

}

bool ledAnimated(const LedInput& input)
{
    if (!input.enabled) {
        return false;
    }
    return input.pairing || animatedTone(input.plan) || animatedTone(input.wholesale);
}

std::array<Rgb, kLedCount> ledFrame(const LedInput& input)
{
    std::array<Rgb, kLedCount> frame{};
    if (!input.enabled) {
        return frame;
    }
    if (input.pairing) {
        uint32_t step = (input.phaseMs / 120) % (kLedsPerBar * 2);
        std::size_t lit = step < kLedsPerBar ? step : kLedsPerBar * 2 - 1 - step;
        Rgb colour = scale(theme::primary, input.brightness);
        frame[lit] = colour;
        frame[kLedsPerBar + lit] = colour;
        return frame;
    }
    fillBar(frame, 0, input.plan, input);
    fillBar(frame, kLedsPerBar, input.wholesale, input);
    return frame;
}

}
