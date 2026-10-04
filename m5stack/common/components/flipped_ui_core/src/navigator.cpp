#include "flipped/ui/navigator.h"

namespace flipped::ui {

namespace {

std::size_t tabIndex(Tab tab, bool showSpotPrices)
{
    for (std::size_t i = 0; i < tabCount(showSpotPrices); ++i) {
        if (tabAt(i, showSpotPrices) == tab) {
            return i;
        }
    }
    return 0;
}

bool inChart(Hit hit)
{
    return hit.x >= Navigator::kChartLeft && hit.x < Navigator::kChartLeft + Navigator::kChartWidth &&
           hit.y >= Navigator::kChartTop - 8 && hit.y < Navigator::kChartTop + Navigator::kChartHeight + 30;
}

std::size_t barAt(Hit hit, std::size_t count)
{
    if (count == 0) {
        return 0;
    }
    int offset = hit.x - Navigator::kChartLeft;
    std::size_t index = static_cast<std::size_t>(offset) * count / Navigator::kChartWidth;
    return index < count ? index : count - 1;
}

}

std::size_t newestDayWithData(const UsageView& usage)
{
    for (std::size_t i = usage.days.size(); i > 0; --i) {
        if (usage.days[i - 1].hasData) {
            return i - 1;
        }
    }
    return usage.days.empty() ? 0 : usage.days.size() - 1;
}

void Navigator::sync(const ScreenModel& model)
{
    showSpotPrices_ = model.showSpotPrices;
    if (!showSpotPrices_ && state_.tab == Tab::prices) {
        state_.tab = Tab::home;
    }
    if (!model.configured && state_.overlay != Overlay::reset) {
        state_.overlay = Overlay::pairing;
    }
    if (model.configured && state_.overlay == Overlay::pairing && model.pairing.step == PairStep::done) {
        state_.overlay = Overlay::none;
    }
    if (!state_.usageChosen || state_.usageDay >= model.usage.days.size()) {
        state_.usageChosen = !model.usage.days.empty();
        state_.usageDay = newestDayWithData(model.usage);
        state_.usageHourly = false;
        state_.selectedBar.reset();
    }
}

void Navigator::openOverlay(Overlay overlay)
{
    state_.overlay = overlay;
    state_.scroll = 0;
    direction_ = 0;
}

bool Navigator::moveTab(int delta)
{
    std::size_t index = tabIndex(state_.tab, showSpotPrices_);
    int next = static_cast<int>(index) + delta;
    if (next < 0 || next >= static_cast<int>(tabCount(showSpotPrices_))) {
        return false;
    }
    direction_ = delta;
    state_.tab = tabAt(static_cast<std::size_t>(next), showSpotPrices_);
    state_.selectedBar.reset();
    state_.scroll = 0;
    return true;
}

bool Navigator::tapTabBar(int16_t x)
{
    std::size_t index = static_cast<std::size_t>(x) * tabCount(showSpotPrices_) / kWidth;
    if (index >= tabCount(showSpotPrices_)) {
        index = tabCount(showSpotPrices_) - 1;
    }
    int delta = static_cast<int>(index) - static_cast<int>(tabIndex(state_.tab, showSpotPrices_));
    if (delta == 0) {
        return false;
    }
    return moveTab(delta);
}

bool Navigator::tapUsage(Hit hit, const ScreenModel& model)
{
    if (!inChart(hit)) {
        return false;
    }
    direction_ = 0;
    if (state_.usageHourly) {
        state_.selectedBar = barAt(hit, 24);
        return true;
    }
    std::size_t day = barAt(hit, model.usage.days.size());
    if (day == state_.usageDay) {
        state_.usageHourly = true;
        state_.selectedBar.reset();
    } else {
        state_.usageDay = day;
        state_.usageChosen = true;
    }
    return true;
}

bool Navigator::tapPrices(Hit hit, const ScreenModel& model)
{
    if (hit.y < kChartTop - 8) {
        state_.pricesAhead = !state_.pricesAhead;
        state_.selectedBar.reset();
        direction_ = 0;
        return true;
    }
    if (!inChart(hit)) {
        return false;
    }
    const Chart& chart = state_.pricesAhead ? model.prices.ahead : model.prices.nextHour;
    state_.selectedBar = barAt(hit, chart.bars.size());
    direction_ = 0;
    return true;
}

bool Navigator::apply(Input input, Hit hit, const ScreenModel& model)
{
    direction_ = 0;
    if (state_.overlay != Overlay::none) {
        if (state_.overlay == Overlay::pairing && !model.configured) {
            return false;
        }
        if (input == Input::buttonA || (input == Input::tap && hit.y < kHeaderHeight + 6 && hit.x < 90)) {
            state_.overlay = Overlay::none;
            return true;
        }
        if (input == Input::swipeUp) {
            state_.scroll += 1;
            return true;
        }
        if (input == Input::swipeDown && state_.scroll > 0) {
            state_.scroll -= 1;
            return true;
        }
        return false;
    }
    switch (input) {
    case Input::holdB:
        openOverlay(Overlay::pairing);
        return true;
    case Input::swipeLeft:
        if (state_.tab == Tab::usage && state_.usageHourly && state_.usageDay + 1 < model.usage.days.size()) {
            state_.usageDay += 1;
            state_.selectedBar.reset();
            direction_ = 1;
            return true;
        }
        return moveTab(1);
    case Input::swipeRight:
        if (state_.tab == Tab::usage && state_.usageHourly && state_.usageDay > 0) {
            state_.usageDay -= 1;
            state_.selectedBar.reset();
            direction_ = -1;
            return true;
        }
        return moveTab(-1);
    case Input::buttonA:
        if (state_.tab == Tab::usage && state_.usageHourly) {
            state_.usageHourly = false;
            state_.selectedBar.reset();
            return true;
        }
        return moveTab(-1);
    case Input::buttonC:
        return moveTab(1);
    case Input::buttonB:
        if (state_.tab == Tab::prices) {
            state_.pricesAhead = !state_.pricesAhead;
            state_.selectedBar.reset();
            return true;
        }
        if (state_.tab == Tab::usage) {
            state_.usageHourly = !state_.usageHourly;
            state_.selectedBar.reset();
            return true;
        }
        if (state_.tab == Tab::more) {
            openOverlay(Overlay::status);
            return true;
        }
        return false;
    case Input::tap:
        if (hit.y >= kTabBarTop) {
            return tapTabBar(hit.x);
        }
        if (state_.tab == Tab::usage) {
            return tapUsage(hit, model);
        }
        if (state_.tab == Tab::prices) {
            return tapPrices(hit, model);
        }
        if (state_.tab == Tab::home) {
            if (showSpotPrices_ && hit.y < 112 && hit.x >= kWidth / 2) {
                return moveTab(1);
            }
            if (hit.y < 112) {
                return moveTab(showSpotPrices_ ? 3 : 2);
            }
            return false;
        }
        if (state_.tab == Tab::more) {
            int row = (hit.y - 44) / 38;
            if (hit.y < 44 || row > 3) {
                return false;
            }
            constexpr Overlay rows[] = {Overlay::pairing, Overlay::status, Overlay::settings, Overlay::reset};
            openOverlay(rows[row]);
            return true;
        }
        return false;
    case Input::swipeUp:
        state_.scroll += 1;
        return true;
    case Input::swipeDown:
        if (state_.scroll > 0) {
            state_.scroll -= 1;
            return true;
        }
        return false;
    }
    return false;
}

}
