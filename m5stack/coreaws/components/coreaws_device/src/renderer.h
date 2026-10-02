#pragma once

#include <M5GFX.h>

#include <cstdint>

#include "coreaws/settings.h"
#include "flipped/ui/navigator.h"
#include "flipped/ui/view.h"

namespace coreaws {

struct Assets {
    const uint8_t* wordmark = nullptr;
    size_t wordmarkSize = 0;
    const uint8_t* bolt = nullptr;
    size_t boltSize = 0;
    LGFX_Sprite* headerLogo = nullptr;
    LGFX_Sprite* headerBolt = nullptr;
};

class Renderer {
public:
    Renderer(LGFX_Sprite& canvas, const Assets& assets) : c_(canvas), assets_(assets) {}

    void draw(const flipped::ui::ScreenModel& model, const flipped::ui::NavState& nav, const UiSettings& settings,
              uint8_t holdPercent);
    void drawBoot(uint8_t step, uint8_t steps, const char* line);

    static constexpr int16_t kSettingsRowTop = 40;
    static constexpr int16_t kSettingsRowHeight = 44;

private:
    void header(const char* title, const flipped::ui::ScreenModel& model, bool back);
    void tabBar(flipped::ui::Tab active, bool showSpotPrices);
    void tabIcon(flipped::ui::Tab tab, int16_t cx, int16_t cy, uint32_t colour);
    void figureTile(int16_t x, int16_t y, int16_t w, int16_t h, const flipped::ui::Figure& figure, bool large);
    void chip(int16_t x, int16_t y, int16_t w, int16_t h, const flipped::ui::Chip& chip);
    void pill(int16_t x, int16_t y, const char* text, uint32_t fill, uint32_t ink);
    void chart(const flipped::ui::Chart& chart, std::optional<std::size_t> selected, const char* unit);
    void message(int16_t y, const std::string& text, uint32_t colour);
    std::string fit(const std::string& text, int16_t width);
    int16_t wrap(int16_t x, int16_t y, int16_t width, const std::string& text, int16_t lineHeight, int16_t maxLines);

    void home(const flipped::ui::ScreenModel& model);
    void prices(const flipped::ui::ScreenModel& model, const flipped::ui::NavState& nav);
    void usage(const flipped::ui::ScreenModel& model, const flipped::ui::NavState& nav);
    void costs(const flipped::ui::ScreenModel& model);
    void more(const flipped::ui::ScreenModel& model);
    void pairing(const flipped::ui::ScreenModel& model);
    void status(const flipped::ui::ScreenModel& model, const flipped::ui::NavState& nav);
    void settings(const flipped::ui::ScreenModel& model, const UiSettings& settings, const flipped::ui::NavState& nav);
    void reset(const flipped::ui::ScreenModel& model, uint8_t holdPercent);

    LGFX_Sprite& c_;
    Assets assets_;
};

}
