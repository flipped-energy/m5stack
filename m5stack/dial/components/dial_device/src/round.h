#pragma once

#include <M5GFX.h>

#include <cstddef>
#include <cstdint>

#include "flipped/ui/view.h"

namespace dial {

enum class Screen : uint8_t { now, spot, usage, signals, pair, virtualDevices, spotSetting, beep };
constexpr std::size_t kScreenCount = 8;

struct DialState {
    Screen screen = Screen::now;
    bool spotAhead = false;
    bool beep = false;
    std::size_t usageDay = 0;
    bool usageChosen = false;
};

struct Assets {
    const uint8_t* bolt = nullptr;
    size_t boltSize = 0;
    const uint8_t* wordmark = nullptr;
    size_t wordmarkSize = 0;
    LGFX_Sprite* logo = nullptr;
};

class RoundRenderer {
public:
    RoundRenderer(LGFX_Sprite& canvas, const Assets& assets) : c_(canvas), assets_(assets) {}

    void draw(const flipped::ui::ScreenModel& model, const DialState& state, int16_t top);
    void drawBoot(uint8_t step, uint8_t steps, int16_t top);

    static constexpr int16_t kStrip = 24;

private:
    void frame(const flipped::ui::ScreenModel& model, const DialState& state);
    void overlay(const flipped::ui::ScreenModel& model, const DialState& state);
    void centreText(const char* text, int16_t y, const lgfx::GFXfont* font, uint32_t colour);
    void now(const flipped::ui::ScreenModel& model);
    void spot(const flipped::ui::ScreenModel& model, const DialState& state);
    void usage(const flipped::ui::ScreenModel& model, const DialState& state);
    void signals(const flipped::ui::ScreenModel& model);
    void pair(const flipped::ui::ScreenModel& model);
    void radialBars(const std::vector<double>& values, const std::vector<flipped::ui::Tone>& tones, float startDeg, float sweepDeg,
                    int16_t base, int16_t outward, int16_t inward);

    int16_t Y(int y) const { return static_cast<int16_t>(y - oy_); }

    LGFX_Sprite& c_;
    Assets assets_;
    int16_t oy_ = 0;
};

}
