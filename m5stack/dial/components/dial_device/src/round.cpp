#include "round.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "flipped/ui/format.h"
#include "flipped/ui/theme.h"

namespace dial {

namespace ui = flipped::ui;
namespace theme = flipped::ui::theme;

namespace {

constexpr int16_t kCx = 120;
constexpr int16_t kCy = 120;
constexpr float kPi = 3.14159265f;

uint32_t rgb(ui::Rgb c) { return (uint32_t{c.r} << 16) | (uint32_t{c.g} << 8) | c.b; }

uint32_t blend(ui::Rgb a, ui::Rgb b, uint8_t amount)
{
    auto mix = [amount](uint8_t x, uint8_t y) { return static_cast<uint8_t>((x * (255 - amount) + y * amount) / 255); };
    return rgb(ui::Rgb{mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)});
}

float minuteAngle(int minute) { return -90.0f + 360.0f * static_cast<float>(minute) / 1440.0f; }

std::string afterComma(const std::string& text)
{
    std::size_t at = text.find(", ");
    return at == std::string::npos ? std::string() : text.substr(at + 2);
}

std::string beforeComma(const std::string& text)
{
    std::size_t at = text.find(", ");
    return at == std::string::npos ? text : text.substr(0, at);
}

}

void RoundRenderer::centreText(const char* text, int16_t y, const lgfx::GFXfont* font, uint32_t colour)
{
    c_.setFont(font);
    c_.setTextColor(colour);
    c_.setTextDatum(textdatum_t::middle_center);
    c_.drawString(text, kCx, Y(y));
}

void RoundRenderer::frame(const ui::ScreenModel& model, const DialState& state)
{
    c_.fillScreen(rgb(theme::background));
    c_.fillCircle(kCx, Y(kCy), 119, rgb(theme::background));
    (void)model;
    (void)state;
}

void RoundRenderer::overlay(const ui::ScreenModel& model, const DialState& state)
{
    if (state.screen == Screen::spot || state.screen == Screen::usage) {
        centreText(model.clock.c_str(), 42, &fonts::DejaVu9, rgb(theme::textDim));
    }
    if (state.screen != Screen::pair && assets_.logo) {
        assets_.logo->pushSprite(&c_, kCx - assets_.logo->width() / 2, Y(204) - assets_.logo->height() / 2);
    }
    c_.fillSmoothRoundRect(kCx - 34, Y(219), 68, 14, 7, rgb(theme::background));
    for (std::size_t i = 0; i < kScreenCount; ++i) {
        int16_t x = static_cast<int16_t>(kCx - 42 + static_cast<int16_t>(i) * 12);
        bool on = static_cast<std::size_t>(state.screen) == i;
        c_.fillSmoothCircle(x, Y(226), on ? 3 : 2, on ? rgb(theme::primary) : rgb(theme::line));
    }
}

void RoundRenderer::radialBars(const std::vector<double>& values, const std::vector<ui::Tone>& tones, float startDeg, float sweepDeg,
                               int16_t base, int16_t outward, int16_t inward)
{
    if (values.empty()) {
        return;
    }
    double highest = 0;
    double lowest = 0;
    for (double v : values) {
        highest = std::max(highest, v);
        lowest = std::min(lowest, v);
    }
    float step = sweepDeg / static_cast<float>(values.size());
    float gap = std::min(1.5f, step * 0.25f);
    c_.drawArc(kCx, Y(kCy), base, base, startDeg, startDeg + sweepDeg, rgb(theme::line));
    for (std::size_t i = 0; i < values.size(); ++i) {
        float a0 = startDeg + step * static_cast<float>(i) + gap / 2;
        float a1 = a0 + step - gap;
        double v = values[i];
        uint32_t colour = rgb(theme::toneColour(tones[i]));
        if (v >= 0 && highest > 0) {
            int16_t len = static_cast<int16_t>(std::max(1.0, outward * v / highest));
            c_.fillArc(kCx, Y(kCy), base, base + len, a0, a1, colour);
        } else if (v < 0 && lowest < 0) {
            int16_t len = static_cast<int16_t>(std::max(1.0, inward * v / lowest));
            c_.fillArc(kCx, Y(kCy), base - len, base, a0, a1, colour);
        }
    }
}

void RoundRenderer::now(const ui::ScreenModel& model)
{
    const ui::HomeView& home = model.home;
    c_.fillArc(kCx, Y(kCy), 104, 114, 0, 360, rgb(theme::surface));
    for (const auto& run : home.strip) {
        c_.fillArc(kCx, Y(kCy), 104, 114, minuteAngle(run.startMinute), minuteAngle(run.endMinute), rgb(theme::toneColour(run.tone)));
    }
    if (home.nowMinute) {
        float a = minuteAngle(*home.nowMinute) * kPi / 180.0f;
        int16_t x = static_cast<int16_t>(kCx + 109 * std::cos(a));
        int16_t y = static_cast<int16_t>(kCy + 109 * std::sin(a));
        c_.fillSmoothCircle(x, Y(y), 7, rgb(theme::background));
        c_.fillSmoothCircle(x, Y(y), 5, rgb(theme::text));
    }
    const ui::Figure& rate = home.rate;
    centreText(model.clock.c_str(), 54, &fonts::FreeSansBold18pt7b, rgb(theme::text));
    centreText(rate.value.c_str(), 108, &fonts::FreeSansBold24pt7b, rate.faulted ? rgb(theme::fault) : rgb(theme::text));
    centreText((rate.unit + "  " + rate.note).c_str(), 140, &fonts::DejaVu12, rate.faulted ? rgb(theme::fault) : rgb(theme::toneColour(rate.tone)));
    centreText(beforeComma(home.periodLine).c_str(), 162, &fonts::DejaVu9, rgb(theme::text));
    centreText(afterComma(home.periodLine).c_str(), 176, &fonts::DejaVu9, rgb(theme::textDim));
}

void RoundRenderer::spot(const ui::ScreenModel& model, const DialState& state)
{
    const ui::PricesView& prices = model.prices;
    const ui::Chart& chart = state.spotAhead ? prices.ahead : prices.nextHour;
    std::vector<double> values;
    std::vector<ui::Tone> tones;
    for (const auto& bar : chart.bars) {
        values.push_back(bar.value);
        tones.push_back(bar.tone);
    }
    radialBars(values, tones, 135, 270, 96, 20, 14);
    const ui::Figure& now = prices.now;
    centreText("SPOT PRICE", 62, &fonts::DejaVu9, rgb(theme::textDim));
    centreText(now.value.c_str(), 104, &fonts::FreeSansBold24pt7b, now.faulted ? rgb(theme::fault) : rgb(theme::text));
    centreText("c/kWh ex GST", 134, &fonts::DejaVu9, rgb(theme::textDim));
    if (!prices.faultText.empty()) {
        centreText(prices.faultText.c_str(), 156, &fonts::DejaVu12, rgb(theme::fault));
        return;
    }
    ui::Rgb tone = theme::toneColour(now.tone);
    c_.setFont(&fonts::DejaVu12);
    int16_t w = static_cast<int16_t>(c_.textWidth(prices.tierLine.c_str())) + 16;
    c_.fillSmoothRoundRect(kCx - w / 2, Y(146), w, 18, 9, blend(tone, theme::background, 130));
    centreText(prices.tierLine.c_str(), 155, &fonts::DejaVu12, rgb(tone));
    centreText(state.spotAhead ? "next 19 hours" : "next hour", 180, &fonts::DejaVu9, rgb(theme::primary));
}

void RoundRenderer::usage(const ui::ScreenModel& model, const DialState& state)
{
    const ui::UsageView& view = model.usage;
    if (!view.faultText.empty() || view.days.empty()) {
        centreText("YOUR USAGE", 62, &fonts::DejaVu9, rgb(theme::textDim));
        centreText(view.faultText.empty() ? "No meter readings yet" : view.faultText.c_str(), 120, &fonts::DejaVu12,
                   view.faultText.empty() ? rgb(theme::textDim) : rgb(theme::fault));
        return;
    }
    const ui::UsageDay& day = view.days[std::min(state.usageDay, view.days.size() - 1)];
    std::vector<double> values;
    std::vector<ui::Tone> tones;
    for (int h = 0; h < 24; ++h) {
        double v = day.hourImportKwh[h].value_or(0) - day.hourExportKwh[h].value_or(0);
        values.push_back(v);
        tones.push_back(v < 0 ? ui::Tone::low : ui::Tone::mid);
    }
    radialBars(values, tones, -90, 360, 92, 22, 14);
    centreText(day.title.c_str(), 62, &fonts::DejaVu9, rgb(theme::text));
    char kwh[24];
    std::snprintf(kwh, sizeof kwh, "%.1f", day.importKwh);
    centreText(day.hasData ? kwh : "--", 104, &fonts::FreeSansBold24pt7b, rgb(theme::text));
    centreText("kWh from the grid", 134, &fonts::DejaVu9, rgb(theme::textDim));
    centreText(ui::fmtAudOrDash(day.costAud).c_str(), 156, &fonts::FreeSansBold12pt7b, rgb(theme::primary));
    centreText("delayed meter data", 178, &fonts::DejaVu9, rgb(theme::high));
}

void RoundRenderer::signals(const ui::ScreenModel& model)
{
    struct Place {
        ui::ChipSlot slot;
        const char* name;
        float centreDeg;
        int16_t x;
        int16_t y;
    };
    static constexpr Place places[ui::kChipCount] = {
        {ui::kPeakChip, "PEAK", -150, 68, 90},
        {ui::kShoulderChip, "SHOULDER", -90, 120, 50},
        {ui::kOffPeakChip, "OFF-PEAK", -30, 172, 90},
        {ui::kPriceHighChip, "PRICE HIGH", 30, 172, 150},
        {ui::kPriceLowChip, "PRICE LOW", 150, 68, 150},
    };
    const auto& chips = model.home.chips;
    for (const Place& place : places) {
        if (!model.showSpotPrices && place.slot >= ui::kPriceHighChip) {
            continue;
        }
        const ui::Chip& chip = chips[place.slot];
        bool known = chip.on.has_value();
        bool on = known && *chip.on;
        uint32_t colour = !known ? rgb(theme::fault) : on ? rgb(theme::toneColour(chip.onTone)) : rgb(theme::surfaceHigh);
        c_.fillArc(kCx, Y(kCy), 98, 116, place.centreDeg - 27, place.centreDeg + 27, colour);
        c_.setFont(&fonts::DejaVu9);
        c_.setTextDatum(textdatum_t::middle_center);
        c_.setTextColor(on ? rgb(theme::text) : rgb(theme::textDim));
        c_.drawString(place.name, place.x, Y(place.y - 6));
        c_.drawString(!known ? "?" : on ? "ON" : "off", place.x, Y(place.y + 6));
    }
    centreText(model.clock.c_str(), 120, &fonts::FreeSansBold12pt7b, rgb(theme::text));
}

void RoundRenderer::pair(const ui::ScreenModel& model)
{
    const ui::PairingView& view = model.pairing;
    bool token = view.step == ui::PairStep::addToken;
    const std::string& payload = token ? view.tokenUrl : view.qrPayload;
    centreText(token ? "Link your Flipped account" : "Add to Apple Home", 36, &fonts::DejaVu9, rgb(theme::primary));
    const bool codeWorks = token || view.windowOpen;
    if (!payload.empty() && codeWorks) {
        c_.fillSmoothRoundRect(54, Y(46), 132, 132, 8, rgb(ui::Rgb{255, 255, 255}));
        c_.qrcode(payload.c_str(), 60, Y(52), 120, 1);
    }
    if (!token && codeWorks) {
        centreText(view.manualCode.c_str(), 192, &fonts::FreeSansBold9pt7b, rgb(theme::text));
    } else if (!view.tokenCode.empty()) {
        centreText(view.tokenCode.c_str(), 192, &fonts::DejaVu9, rgb(theme::fault));
    }
    const char* status = view.tokenRejected ? "Token refused, scan again"
                         : token && view.tokenUrl.empty() ? "Opening the token page"
                         : token            ? "Scan with your phone"
                         : view.homes > 0   ? "Paired. Hold to show again"
                         : !view.windowOpen ? "Hold the button to pair"
                                            : "Home app, +, Add Accessory";
    centreText(status, 208, &fonts::DejaVu9, view.tokenRejected ? rgb(theme::fault) : rgb(theme::textDim));
}

void RoundRenderer::draw(const ui::ScreenModel& model, const DialState& state, int16_t top)
{
    oy_ = top;
    frame(model, state);
    switch (state.screen) {
    case Screen::now:
        now(model);
        break;
    case Screen::spot:
        spot(model, state);
        break;
    case Screen::usage:
        usage(model, state);
        break;
    case Screen::signals:
        signals(model);
        break;
    case Screen::pair:
        pair(model);
        break;
    case Screen::virtualDevices:
    case Screen::spotSetting:
    case Screen::beep: {
        const char *title = state.screen == Screen::virtualDevices ? "Virtual devices" :
                            state.screen == Screen::spotSetting ? "Spot prices" : "Beep alerts";
        const bool enabled = state.screen == Screen::virtualDevices ? model.virtualDevices :
                             state.screen == Screen::spotSetting ? model.showSpotPrices : state.beep;
        centreText(title, 65, &fonts::FreeSansBold9pt7b, rgb(theme::text));
        centreText(enabled ? "On" : "Off", 108, &fonts::FreeSansBold18pt7b, rgb(theme::primary));
        centreText("Press to toggle", 155, &fonts::DejaVu9, rgb(theme::textDim));
        centreText("Turn to change page", 178, &fonts::DejaVu9, rgb(theme::textDim));
        break;
    }
    }
    overlay(model, state);
}

void RoundRenderer::drawBoot(uint8_t step, uint8_t steps, int16_t top)
{
    oy_ = top;
    c_.fillScreen(rgb(theme::background));
    float t = steps == 0 ? 1.0f : static_cast<float>(step) / steps;
    c_.fillArc(kCx, Y(kCy), 108, 116, -90, -90 + 360 * t, rgb(theme::primary));
    if (assets_.bolt) {
        float scale = 0.5f + 0.4f * std::min(1.0f, t * 1.5f);
        c_.drawPng(assets_.bolt, assets_.boltSize, kCx, Y(100), 0, 0, 0, 0, scale, scale, textdatum_t::middle_center);
    }
    if (assets_.wordmark && t > 0.4f) {
        c_.drawPng(assets_.wordmark, assets_.wordmarkSize, kCx, Y(168), 0, 0, 0, 0, 0.7f, 0.7f, textdatum_t::middle_center);
    }
}

}
