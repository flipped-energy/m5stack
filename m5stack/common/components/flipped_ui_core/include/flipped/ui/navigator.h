#pragma once

#include <cstddef>
#include <cstdint>

#include "flipped/ui/view.h"

namespace flipped::ui {

enum class Input : uint8_t { tap, swipeLeft, swipeRight, swipeUp, swipeDown, buttonA, buttonB, buttonC, holdB };

enum class Overlay : uint8_t { none, pairing, status, settings, reset };

struct NavState {
    Tab tab = Tab::home;
    Overlay overlay = Overlay::none;
    bool pricesAhead = false;
    std::size_t usageDay = 0;
    bool usageHourly = false;
    std::optional<std::size_t> selectedBar;
    int scroll = 0;
    bool usageChosen = false;
};

struct Hit {
    int16_t x = 0;
    int16_t y = 0;
};

class Navigator {
public:
    const NavState& state() const { return state_; }
    int direction() const { return direction_; }

    void sync(const ScreenModel& model);
    bool apply(Input input, Hit hit, const ScreenModel& model);
    void openOverlay(Overlay overlay);

    static constexpr int16_t kWidth = 320;
    static constexpr int16_t kHeight = 240;
    static constexpr int16_t kTabBarTop = 204;
    static constexpr int16_t kHeaderHeight = 24;
    static constexpr int16_t kChartLeft = 34;
    static constexpr int16_t kChartTop = 80;
    static constexpr int16_t kChartWidth = 280;
    static constexpr int16_t kChartHeight = 90;

private:
    bool moveTab(int delta);
    bool tapTabBar(int16_t x);
    bool tapUsage(Hit hit, const ScreenModel& model);
    bool tapPrices(Hit hit, const ScreenModel& model);

    NavState state_;
    bool showSpotPrices_ = true;
    int direction_ = 0;
};

std::size_t newestDayWithData(const UsageView& usage);

}
