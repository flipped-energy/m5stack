#include "renderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include "flipped/ui/format.h"
#include "flipped/ui/theme.h"

namespace coreaws {

namespace ui = flipped::ui;
namespace theme = flipped::ui::theme;

namespace {

uint32_t rgb(ui::Rgb colour) { return (uint32_t{colour.r} << 16) | (uint32_t{colour.g} << 8) | colour.b; }

uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) { return rgb(ui::Rgb{r, g, b}); }

uint32_t blend(ui::Rgb a, ui::Rgb b, uint8_t amount)
{
    auto mix = [amount](uint8_t x, uint8_t y) { return static_cast<uint8_t>((x * (255 - amount) + y * amount) / 255); };
    return rgb(mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b));
}

constexpr const char* kTabNames[] = {"Home", "Prices", "Usage", "Costs", "More"};

const lgfx::GFXfont* fontSmall() { return &fonts::FreeSans9pt7b; }
const lgfx::GFXfont* fontSmallBold() { return &fonts::FreeSansBold9pt7b; }
const lgfx::GFXfont* fontMedium() { return &fonts::FreeSansBold12pt7b; }
const lgfx::GFXfont* fontLarge() { return &fonts::FreeSansBold18pt7b; }
const lgfx::GFXfont* fontHuge() { return &fonts::FreeSansBold24pt7b; }

constexpr int16_t kChipLeft = 6;
constexpr int16_t kChipTop = 180;
constexpr int16_t kChipRowWidth = 308;
constexpr int16_t kChipHeight = 20;
constexpr int16_t kChipGap = 2;
constexpr int16_t kChipPad = 3;

std::string chipText(const ui::Chip& chip) { return chip.on ? chip.title : chip.title + "?"; }

}

std::string Renderer::fit(const std::string& text, int16_t width)
{
    if (c_.textWidth(text.c_str()) <= width) {
        return text;
    }
    std::string cut = text;
    while (!cut.empty() && c_.textWidth((cut + "...").c_str()) > width) {
        cut.pop_back();
    }
    return cut + "...";
}

int16_t Renderer::wrap(int16_t x, int16_t y, int16_t width, const std::string& text, int16_t lineHeight, int16_t maxLines)
{
    std::string line;
    int16_t lines = 0;
    std::size_t i = 0;
    while (i <= text.size() && lines < maxLines) {
        std::size_t next = text.find_first_of(" _/.", i);
        std::string word = text.substr(i, next == std::string::npos ? std::string::npos : next - i + 1);
        if (!line.empty() && c_.textWidth((line + word).c_str()) > width) {
            c_.drawString(line.c_str(), x, y + lines * lineHeight);
            ++lines;
            line.clear();
        }
        line += word;
        if (next == std::string::npos) {
            break;
        }
        i = next + 1;
    }
    if (!line.empty() && lines < maxLines) {
        c_.drawString(line.c_str(), x, y + lines * lineHeight);
        ++lines;
    }
    return lines;
}

void Renderer::message(int16_t y, const std::string& text, uint32_t colour)
{
    c_.setFont(&fonts::DejaVu12);
    c_.setTextColor(colour);
    c_.setTextDatum(textdatum_t::top_left);
    wrap(10, y, 300, text, 15, 5);
}

void Renderer::header(const char* title, const ui::ScreenModel& model, bool back)
{
    c_.fillRect(0, 0, 320, 24, rgb(theme::background));
    c_.setTextDatum(textdatum_t::middle_left);
    if (back) {
        c_.setFont(fontSmallBold());
        c_.setTextColor(rgb(theme::primary));
        c_.drawString("<", 8, 12);
        c_.setFont(fontSmall());
        c_.drawString("Back", 20, 12);
        c_.setFont(fontSmallBold());
        c_.setTextColor(rgb(theme::text));
        c_.setTextDatum(textdatum_t::middle_center);
        c_.drawString(title, 172, 12);
        c_.drawFastHLine(0, 24, 320, rgb(theme::surfaceHigh));
        return;
    } else if (assets_.headerLogo && std::string(title) == "Flipped Energy") {
        assets_.headerLogo->pushSprite(&c_, 2, 0);
    } else {
        if (assets_.headerBolt) {
            assets_.headerBolt->pushSprite(&c_, 2, 0);
        }
        c_.setFont(fontSmallBold());
        c_.setTextColor(rgb(theme::text));
        c_.drawString(title, 28, 12);
    }
    c_.setFont(fontSmall());
    c_.setTextDatum(textdatum_t::middle_right);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(model.clock.c_str(), 312, 12);
    int16_t clockWidth = static_cast<int16_t>(c_.textWidth(model.clock.c_str()));
    int16_t x = 312 - clockWidth - 12;
    c_.fillSmoothCircle(x, 12, 3, model.wifiConnected ? rgb(theme::low) : rgb(theme::fault));
    if (model.homes > 0) {
        c_.fillTriangle(x - 16, 12, x - 10, 6, x - 4, 12, rgb(theme::primary));
        c_.fillRect(x - 14, 12, 8, 6, rgb(theme::primary));
    }
    c_.drawFastHLine(0, 24, 320, rgb(theme::surfaceHigh));
}

void Renderer::tabIcon(ui::Tab tab, int16_t cx, int16_t cy, uint32_t colour)
{
    switch (tab) {
    case ui::Tab::home:
        c_.fillTriangle(cx - 8, cy - 1, cx, cy - 8, cx + 8, cy - 1, colour);
        c_.fillRect(cx - 6, cy - 1, 12, 8, colour);
        c_.fillRect(cx - 2, cy + 2, 4, 5, rgb(theme::surface));
        break;
    case ui::Tab::prices:
        c_.fillTriangle(cx + 2, cy - 8, cx - 6, cy + 1, cx + 1, cy + 1, colour);
        c_.fillTriangle(cx - 1, cy - 1, cx + 6, cy - 1, cx - 2, cy + 8, colour);
        break;
    case ui::Tab::usage:
        c_.fillRect(cx - 8, cy, 4, 7, colour);
        c_.fillRect(cx - 2, cy - 5, 4, 12, colour);
        c_.fillRect(cx + 4, cy - 8, 4, 15, colour);
        break;
    case ui::Tab::costs:
        c_.drawCircle(cx, cy, 8, colour);
        c_.drawCircle(cx, cy, 7, colour);
        c_.setFont(&fonts::Font0);
        c_.setTextColor(colour);
        c_.setTextDatum(textdatum_t::middle_center);
        c_.drawString("$", cx + 1, cy + 1);
        break;
    case ui::Tab::more:
        c_.fillSmoothCircle(cx - 6, cy, 2, colour);
        c_.fillSmoothCircle(cx, cy, 2, colour);
        c_.fillSmoothCircle(cx + 6, cy, 2, colour);
        break;
    }
}

void Renderer::tabBar(ui::Tab active, bool showSpotPrices)
{
    c_.fillRect(0, 204, 320, 36, rgb(theme::surface));
    c_.drawFastHLine(0, 204, 320, rgb(theme::surfaceHigh));
    for (std::size_t i = 0; i < ui::tabCount(showSpotPrices); ++i) {
        int16_t cx = static_cast<int16_t>((2 * i + 1) * 320 / (2 * ui::tabCount(showSpotPrices)));
        const auto tab = ui::tabAt(i, showSpotPrices);
        bool on = tab == active;
        uint32_t colour = on ? rgb(theme::primary) : rgb(theme::textDim);
        if (on) {
            c_.fillRoundRect(cx - 24, 205, 48, 3, 1, colour);
        }
        tabIcon(tab, cx, 217, colour);
        c_.setFont(&fonts::DejaVu9);
        c_.setTextColor(colour);
        c_.setTextDatum(textdatum_t::top_center);
        c_.drawString(kTabNames[static_cast<std::size_t>(tab)], cx, 228);
    }
}

void Renderer::pill(int16_t x, int16_t y, const char* text, uint32_t fill, uint32_t ink)
{
    c_.setFont(&fonts::DejaVu12);
    int16_t w = static_cast<int16_t>(c_.textWidth(text)) + 14;
    c_.fillSmoothRoundRect(x, y, w, 18, 9, fill);
    c_.setTextColor(ink);
    c_.setTextDatum(textdatum_t::middle_center);
    c_.drawString(text, x + w / 2, y + 9);
}

void Renderer::figureTile(int16_t x, int16_t y, int16_t w, int16_t h, const ui::Figure& figure, bool large)
{
    int16_t valueBaseline = static_cast<int16_t>(y + (large ? 58 : 46));
    ui::Rgb accent = figure.faulted ? theme::fault : theme::toneColour(figure.tone);
    c_.fillSmoothRoundRect(x, y, w, h, 10, rgb(theme::surface));
    if (figure.tone != ui::Tone::neutral || figure.faulted) {
        c_.fillSmoothRoundRect(x, y, 5, h, 3, rgb(accent));
    }
    c_.setTextDatum(textdatum_t::top_left);
    c_.setFont(&fonts::DejaVu12);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(figure.label.c_str(), x + 12, y + 7);
    c_.setFont(large ? fontHuge() : fontLarge());
    c_.setTextColor(figure.faulted ? rgb(theme::fault) : rgb(theme::text));
    c_.setTextDatum(textdatum_t::baseline_left);
    int16_t baseline = valueBaseline;
    c_.drawString(figure.value.c_str(), x + 12, baseline);
    int16_t valueWidth = static_cast<int16_t>(c_.textWidth(figure.value.c_str()));
    c_.setFont(&fonts::DejaVu9);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(figure.unit.c_str(), x + 16 + valueWidth, baseline);
    c_.setFont(&fonts::DejaVu12);
    c_.setTextDatum(textdatum_t::bottom_left);
    c_.setTextColor(figure.faulted ? rgb(theme::fault) : rgb(accent));
    c_.drawString(fit(figure.note, w - 18).c_str(), x + 12, y + h - 5);
}

void Renderer::chip(int16_t x, int16_t y, int16_t w, int16_t h, const ui::Chip& chip)
{
    bool known = chip.on.has_value();
    bool on = known && *chip.on;
    ui::Rgb tone = known ? theme::toneColour(chip.onTone) : theme::fault;
    if (on) {
        c_.fillSmoothRoundRect(x, y, w, h, h / 2, rgb(tone));
    } else {
        c_.fillSmoothRoundRect(x, y, w, h, h / 2, rgb(theme::surface));
        c_.drawRoundRect(x, y, w, h, h / 2, known ? rgb(theme::line) : rgb(theme::fault));
    }
    c_.setFont(&fonts::DejaVu9);
    c_.setTextDatum(textdatum_t::middle_center);
    c_.setTextColor(on ? rgb(theme::background) : (known ? rgb(theme::textDim) : rgb(theme::fault)));
    c_.drawString(chipText(chip).c_str(), x + w / 2, y + h / 2 + 1);
}

void Renderer::chart(const ui::Chart& chart, std::optional<std::size_t> selected, const char* unit)
{
    const int16_t left = ui::Navigator::kChartLeft;
    const int16_t top = ui::Navigator::kChartTop;
    const int16_t width = ui::Navigator::kChartWidth;
    const int16_t height = ui::Navigator::kChartHeight;
    c_.setFont(&fonts::Font0);
    c_.setTextColor(rgb(theme::textDim));
    if (chart.bars.empty()) {
        c_.setTextDatum(textdatum_t::middle_center);
        c_.setFont(&fonts::DejaVu12);
        c_.drawString("No data yet", left + width / 2, top + height / 2);
        return;
    }
    double highest = 0;
    double lowest = 0;
    for (const auto& bar : chart.bars) {
        highest = std::max(highest, bar.value);
        lowest = std::min(lowest, bar.value);
    }
    if (highest <= 0 && lowest >= 0) {
        highest = 1;
    }
    double span = highest - lowest;
    auto yOf = [&](double value) { return static_cast<int16_t>(top + (highest - value) * height / span); };
    int16_t zero = yOf(0);
    for (int i = 1; i <= 3; ++i) {
        int16_t gy = static_cast<int16_t>(top + height * i / 4);
        for (int16_t gx = left; gx < left + width; gx += 6) {
            c_.drawPixel(gx, gy, rgb(theme::surfaceHigh));
        }
    }
    c_.drawFastHLine(left, zero, width, rgb(theme::line));
    char label[24];
    c_.setTextDatum(textdatum_t::middle_right);
    std::snprintf(label, sizeof label, highest >= 100 ? "%.0f" : "%.1f", highest);
    c_.drawString(label, left - 4, top);
    if (lowest < 0) {
        std::snprintf(label, sizeof label, lowest <= -100 ? "%.0f" : "%.1f", lowest);
        c_.drawString(label, left - 4, top + height);
    }
    c_.drawString("0", left - 4, zero);
    c_.setTextDatum(textdatum_t::bottom_left);
    c_.drawString(unit, 2, top - 6);
    std::size_t count = chart.bars.size();
    int16_t pitch = static_cast<int16_t>(width / static_cast<int16_t>(count));
    int16_t barWidth = static_cast<int16_t>(std::max<int>(2, pitch - (pitch > 8 ? 3 : 1)));
    for (std::size_t i = 0; i < count; ++i) {
        const ui::Bar& bar = chart.bars[i];
        int16_t x = static_cast<int16_t>(left + i * width / count + (pitch - barWidth) / 2);
        int16_t y = yOf(bar.value);
        bool isSelected = selected && *selected == i;
        ui::Rgb colour = theme::toneColour(bar.tone);
        uint32_t fill = selected && !isSelected ? blend(colour, theme::background, 150) : rgb(colour);
        if (bar.value >= 0) {
            int16_t h = static_cast<int16_t>(std::max<int>(1, zero - y));
            c_.fillRoundRect(x, zero - h, barWidth, h, barWidth > 6 ? 2 : 0, fill);
        } else {
            c_.fillRoundRect(x, zero, barWidth, static_cast<int16_t>(y - zero), barWidth > 6 ? 2 : 0, fill);
        }
        if (isSelected) {
            c_.drawRoundRect(x - 1, top - 2, barWidth + 2, height + 4, 2, rgb(theme::text));
        }
    }
    auto line = [&](const std::optional<double>& value, ui::Rgb colour) {
        if (!value || *value > highest || *value < lowest) {
            return;
        }
        int16_t ly = yOf(*value);
        for (int16_t gx = left; gx < left + width; gx += 8) {
            c_.drawFastHLine(gx, ly, 4, rgb(colour));
        }
    };
    line(chart.highLine, theme::high);
    line(chart.lowLine, theme::low);
    c_.setFont(&fonts::DejaVu9);
    c_.setTextColor(rgb(theme::textDim));
    c_.setTextDatum(textdatum_t::top_left);
    c_.drawString(chart.axisStart.c_str(), left, top + height + 3);
    c_.setTextDatum(textdatum_t::top_center);
    c_.drawString(chart.axisMiddle.c_str(), left + width / 2, top + height + 3);
    c_.setTextDatum(textdatum_t::top_right);
    c_.drawString(chart.axisEnd.c_str(), left + width, top + height + 3);
}

void Renderer::home(const ui::ScreenModel& model)
{
    const ui::HomeView& view = model.home;
    figureTile(6, 30, model.showSpotPrices ? 151 : 308, 80, view.rate, true);
    if (model.showSpotPrices) {
        figureTile(163, 30, 151, 80, view.spot, true);
    }
    c_.setTextDatum(textdatum_t::top_left);
    c_.setFont(fontSmallBold());
    c_.setTextColor(rgb(theme::text));
    c_.drawString(fit(view.periodLine, 304).c_str(), 8, 116);
    c_.setFont(&fonts::DejaVu12);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(fit(view.periodDetail, 304).c_str(), 8, 136);
    const int16_t stripTop = 154;
    const int16_t stripLeft = 8;
    const int16_t stripWidth = 304;
    c_.fillRoundRect(stripLeft, stripTop, stripWidth, 10, 3, rgb(theme::surface));
    for (const auto& run : view.strip) {
        int16_t x0 = static_cast<int16_t>(stripLeft + run.startMinute * stripWidth / 1440);
        int16_t x1 = static_cast<int16_t>(stripLeft + run.endMinute * stripWidth / 1440);
        int16_t h = run.tone == ui::Tone::high ? 10 : (run.tone == ui::Tone::low || run.tone == ui::Tone::free ? 5 : 7);
        c_.fillRect(x0, stripTop + 10 - h, std::max<int16_t>(1, x1 - x0 - 1), h, rgb(theme::toneColour(run.tone)));
    }
    if (view.nowMinute) {
        int16_t x = static_cast<int16_t>(stripLeft + *view.nowMinute * stripWidth / 1440);
        c_.fillTriangle(x - 4, stripTop - 6, x + 4, stripTop - 6, x, stripTop - 1, rgb(theme::text));
        c_.drawFastVLine(x, stripTop - 1, 12, rgb(theme::text));
    }
    c_.setFont(&fonts::Font0);
    c_.setTextColor(rgb(theme::textDim));
    constexpr const char* labels[] = {"12am", "6am", "12pm", "6pm", "12am"};
    for (int i = 0; i < 5; ++i) {
        c_.setTextDatum(i == 0 ? textdatum_t::top_left : (i == 4 ? textdatum_t::top_right : textdatum_t::top_center));
        c_.drawString(labels[i], stripLeft + i * stripWidth / 4, stripTop + 13);
    }
    c_.setFont(&fonts::DejaVu9);
    const std::size_t count = model.showSpotPrices ? ui::kChipCount : ui::kPriceHighChip;
    std::array<int, ui::kChipCount> widths{};
    int spare = kChipRowWidth - kChipGap * static_cast<int>(count - 1);
    for (std::size_t i = 0; i < count; ++i) {
        widths[i] = static_cast<int>(c_.textWidth(chipText(view.chips[i]).c_str())) + 2 * kChipPad;
        spare -= widths[i];
    }
    const int share = spare / static_cast<int>(count);
    const int rest = spare - share * static_cast<int>(count);
    int x = kChipLeft;
    for (std::size_t i = 0; i < count; ++i) {
        const int w = widths[i] + share + (static_cast<int>(i) < rest ? 1 : 0);
        chip(static_cast<int16_t>(x), kChipTop, static_cast<int16_t>(w), kChipHeight, view.chips[i]);
        x += w + kChipGap;
    }
}

void Renderer::prices(const ui::ScreenModel& model, const ui::NavState& nav)
{
    const ui::PricesView& view = model.prices;
    c_.setTextDatum(textdatum_t::top_left);
    c_.setFont(&fonts::DejaVu9);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(view.now.label.c_str(), 8, 30);
    c_.setFont(fontLarge());
    c_.setTextColor(view.now.faulted ? rgb(theme::fault) : rgb(theme::text));
    c_.setTextDatum(textdatum_t::baseline_left);
    c_.drawString(view.now.value.c_str(), 8, 68);
    int16_t w = static_cast<int16_t>(c_.textWidth(view.now.value.c_str()));
    c_.setFont(&fonts::DejaVu9);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(view.now.unit.c_str(), 12 + w, 49);
    ui::Rgb tone = theme::toneColour(view.now.tone);
    if (!view.tierLine.empty()) {
        pill(static_cast<int16_t>(12 + w), 57, view.tierLine.c_str(), blend(tone, theme::background, 120), rgb(tone));
    }
    int16_t toggleX = 196;
    uint32_t on = rgb(theme::primary);
    uint32_t off = rgb(theme::surface);
    c_.fillSmoothRoundRect(toggleX, 30, 116, 22, 11, off);
    c_.fillSmoothRoundRect(nav.pricesAhead ? toggleX + 58 : toggleX, 30, 58, 22, 11, on);
    c_.setFont(&fonts::DejaVu9);
    c_.setTextDatum(textdatum_t::middle_center);
    c_.setTextColor(nav.pricesAhead ? rgb(theme::textDim) : rgb(theme::background));
    c_.drawString("Next hour", toggleX + 29, 41);
    c_.setTextColor(nav.pricesAhead ? rgb(theme::background) : rgb(theme::textDim));
    c_.drawString("Next 19 h", toggleX + 87, 41);
    if (!view.faultText.empty()) {
        message(110, view.faultText, rgb(theme::fault));
        return;
    }
    const ui::Chart& data = nav.pricesAhead ? view.ahead : view.nextHour;
    chart(data, nav.selectedBar, "");
    c_.setTextDatum(textdatum_t::top_left);
    c_.setFont(&fonts::DejaVu12);
    if (nav.selectedBar && *nav.selectedBar < data.bars.size()) {
        const ui::Bar& bar = data.bars[*nav.selectedBar];
        c_.setTextColor(rgb(theme::toneColour(bar.tone)));
        c_.drawString((bar.label + "  " + bar.detail).c_str(), 8, 188);
    } else {
        c_.setTextColor(rgb(theme::textDim));
        c_.setFont(&fonts::DejaVu9);
        c_.drawString(view.sourceLine.c_str(), 8, 190);
    }
}

void Renderer::usage(const ui::ScreenModel& model, const ui::NavState& nav)
{
    const ui::UsageView& view = model.usage;
    c_.setTextDatum(textdatum_t::top_left);
    pill(8, 30, "DELAYED", rgb(theme::surfaceHigh), rgb(theme::high));
    c_.setFont(&fonts::DejaVu9);
    c_.setTextColor(rgb(theme::textDim));
    c_.setTextDatum(textdatum_t::middle_left);
    c_.drawString(view.latestLine.c_str(), 86, 39);
    if (!view.faultText.empty()) {
        message(110, view.faultText, rgb(theme::fault));
        return;
    }
    if (view.days.empty()) {
        message(110, "No meter readings yet", rgb(theme::textDim));
        return;
    }
    std::size_t dayIndex = std::min(nav.usageDay, view.days.size() - 1);
    const ui::UsageDay& day = view.days[dayIndex];
    ui::Chart data;
    if (nav.usageHourly && !day.hasData) {
        c_.setFont(fontSmallBold());
        c_.setTextColor(rgb(theme::text));
        c_.setTextDatum(textdatum_t::top_left);
        c_.drawString(day.title.c_str(), 8, 54);
        message(110, "No meter readings for this day yet", rgb(theme::textDim));
        return;
    }
    if (nav.usageHourly) {
        for (int h = 0; h < 24; ++h) {
            ui::Bar bar;
            bar.value = day.hourImportKwh[h].value_or(0) - day.hourExportKwh[h].value_or(0);
            bar.tone = bar.value < 0 ? ui::Tone::low : ui::Tone::mid;
            data.bars.push_back(bar);
        }
        data.axisStart = "12am";
        data.axisMiddle = "12pm";
        data.axisEnd = "11pm";
        c_.setFont(fontSmallBold());
        c_.setTextColor(rgb(theme::text));
        c_.setTextDatum(textdatum_t::top_left);
        c_.drawString(day.title.c_str(), 8, 54);
    } else {
        for (std::size_t i = 0; i < view.days.size(); ++i) {
            ui::Bar bar;
            bar.value = view.days[i].hasData ? view.days[i].importKwh : 0;
            bar.tone = i == dayIndex ? ui::Tone::mid : ui::Tone::neutral;
            data.bars.push_back(bar);
        }
        data.axisStart = view.days.front().weekday + " " + view.days.front().dayOfMonth;
        data.axisEnd = view.days.back().weekday + " " + view.days.back().dayOfMonth;
        c_.setFont(&fonts::DejaVu9);
        c_.setTextColor(rgb(theme::textDim));
        c_.setTextDatum(textdatum_t::top_left);
        c_.drawString("Grid use per day. Tap a day, then again for hours.", 8, 56);
    }
    chart(data, nav.usageHourly ? nav.selectedBar : std::optional<std::size_t>(dayIndex), "kWh");
    c_.setTextDatum(textdatum_t::top_left);
    c_.setFont(&fonts::DejaVu12);
    char line[96];
    if (nav.usageHourly && nav.selectedBar && *nav.selectedBar < 24) {
        int h = static_cast<int>(*nav.selectedBar);
        std::snprintf(line, sizeof line, "%s-%s  %.2f kWh used  %s", ui::fmtHour(h).c_str(), ui::fmtHour((h + 1) % 24).c_str(),
                      day.hourImportKwh[h].value_or(0), ui::fmtAudOrDash(day.hourCostAud[h]).c_str());
        c_.setTextColor(rgb(theme::primary));
    } else if (day.hasData) {
        std::snprintf(line, sizeof line, "%s  %s  %s", day.title.c_str(), ui::fmtKwh(day.importKwh).c_str(),
                      ui::fmtAudOrDash(day.costAud).c_str());
        c_.setTextColor(rgb(theme::text));
    } else {
        std::snprintf(line, sizeof line, "%s  no readings", day.title.c_str());
        c_.setTextColor(rgb(theme::textDim));
    }
    c_.drawString(line, 8, 188);
}

void Renderer::costs(const ui::ScreenModel& model)
{
    const ui::CostsView& view = model.costs;
    figureTile(6, 28, 308, 66, view.rateNow, false);
    if (!view.faultText.empty()) {
        message(120, view.faultText, rgb(theme::fault));
        return;
    }
    int16_t y = 99;
    c_.setFont(&fonts::DejaVu12);
    for (const auto& row : view.schedule) {
        if (y > 135) {
            break;
        }
        if (row.current) {
            c_.fillSmoothRoundRect(6, y - 2, 308, 18, 5, rgb(theme::surfaceHigh));
        }
        c_.fillSmoothCircle(14, y + 7, 4, rgb(theme::toneColour(row.tone)));
        c_.setTextColor(row.current ? rgb(theme::text) : rgb(theme::textDim));
        c_.setTextDatum(textdatum_t::top_left);
        c_.drawString(row.name.c_str(), 24, y);
        c_.drawString(row.window.c_str(), 128, y);
        c_.setTextDatum(textdatum_t::top_right);
        c_.drawString(row.rate.c_str(), 310, y);
        y += 18;
    }
    if (!view.allowanceLine.empty()) {
        c_.setTextDatum(textdatum_t::top_left);
        c_.setTextColor(rgb(theme::high));
        c_.drawString(view.allowanceLine.c_str(), 8, y);
    }
    const ui::Figure* tiles[] = {&view.lastDay, &view.lastWeek, &view.feedIn};
    for (int i = 0; i < 3; ++i) {
        int16_t x = static_cast<int16_t>(6 + i * 104);
        c_.fillSmoothRoundRect(x, 156, 100, 45, 8, rgb(theme::surface));
        c_.setTextDatum(textdatum_t::top_left);
        c_.setFont(&fonts::DejaVu9);
        c_.setTextColor(rgb(theme::textDim));
        c_.drawString(tiles[i]->label.c_str(), x + 8, 160);
        c_.setFont(fontSmallBold());
        c_.setTextColor(rgb(theme::text));
        c_.drawString(tiles[i]->value.c_str(), x + 8, 171);
        c_.setFont(&fonts::DejaVu9);
        c_.setTextColor(rgb(theme::textDim));
        c_.drawString(tiles[i]->note.c_str(), x + 8, 190);
    }
}

void Renderer::more(const ui::ScreenModel& model)
{
    constexpr const char* titles[] = {"Pair and setup codes", "Device status", "Screen and lights", "Factory reset"};
    constexpr const char* notes[] = {"Show the Apple Home code again", "Wi-Fi, account and data", "Brightness, LED bars, night",
                                     "Erase pairing, Wi-Fi and token"};
    for (int i = 0; i < 4; ++i) {
        int16_t y = static_cast<int16_t>(44 + i * 38);
        c_.fillSmoothRoundRect(6, y, 308, 34, 8, rgb(theme::surface));
        c_.setTextDatum(textdatum_t::top_left);
        c_.setFont(fontSmallBold());
        c_.setTextColor(i == 3 ? rgb(theme::fault) : rgb(theme::text));
        c_.drawString(titles[i], 16, y + 3);
        c_.setFont(&fonts::DejaVu9);
        c_.setTextColor(rgb(theme::textDim));
        c_.drawString(notes[i], 16, y + 21);
        c_.setFont(fontSmallBold());
        c_.setTextColor(rgb(theme::textDim));
        c_.setTextDatum(textdatum_t::middle_right);
        c_.drawString(">", 304, y + 17);
    }
    c_.setFont(&fonts::DejaVu9);
    c_.setTextDatum(textdatum_t::top_left);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(("Firmware " + model.status.firmware).c_str(), 8, 30);
}

void Renderer::pairing(const ui::ScreenModel& model)
{
    const ui::PairingView& view = model.pairing;
    bool token = view.step == ui::PairStep::addToken;
    const std::string& payload = token ? view.tokenUrl : view.qrPayload;
    c_.fillSmoothRoundRect(6, 30, 140, 140, 8, rgb(255, 255, 255));
    if (!payload.empty()) {
        c_.qrcode(payload.c_str(), 14, 38, 124, 1);
    }
    c_.setTextDatum(textdatum_t::top_left);
    int16_t x = 150;
    c_.setFont(fontSmallBold());
    c_.setTextColor(rgb(theme::primary));
    c_.drawString(token ? "Step 2 of 2" : "Step 1 of 2", x, 30);
    c_.setFont(&fonts::DejaVu12);
    c_.setTextColor(rgb(theme::text));
    const char* steps[3];
    if (token) {
        steps[0] = "Scan with phone";
        steps[1] = "Sign in to Flipped";
        steps[2] = "Approve display";
    } else {
        steps[0] = "Open Home app";
        steps[1] = "Tap + Add Accessory";
        steps[2] = "Scan this code";
    }
    for (int i = 0; i < 3; ++i) {
        int16_t y = static_cast<int16_t>(52 + i * 22);
        c_.fillSmoothCircle(x + 7, y + 7, 8, rgb(theme::surfaceHigh));
        c_.setTextDatum(textdatum_t::middle_center);
        c_.setTextColor(rgb(theme::primary));
        c_.drawString(std::to_string(i + 1).c_str(), x + 7, y + 8);
        c_.setTextDatum(textdatum_t::middle_left);
        c_.setTextColor(rgb(theme::text));
        c_.drawString(steps[i], x + 20, y + 8);
    }
    c_.setTextDatum(textdatum_t::top_left);
    c_.setFont(&fonts::DejaVu9);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(token ? "Or type this code" : "Or enter setup code", x, 124);
    c_.setFont(fontSmallBold());
    c_.setTextColor(rgb(theme::text));
    c_.drawString(token ? view.tokenCode.c_str() : view.manualCode.c_str(), x, 138);
    struct Dot {
        const char* name;
        bool done;
    };
    Dot dots[] = {{"Phone", view.bluetooth || view.wifiConnected}, {"Wi-Fi", view.wifiConnected}, {"Home", view.homes > 0},
                  {"Account", view.tokenSaved && !view.tokenRejected}};
    for (int i = 0; i < 4; ++i) {
        int16_t dx = static_cast<int16_t>(10 + i * 78);
        c_.fillSmoothRoundRect(dx - 4, 184, 74, 18, 9, dots[i].done ? blend(theme::low, theme::background, 150) : rgb(theme::surface));
        c_.fillSmoothCircle(dx + 6, 193, 4, dots[i].done ? rgb(theme::low) : rgb(theme::line));
        c_.setFont(&fonts::DejaVu9);
        c_.setTextDatum(textdatum_t::middle_left);
        c_.setTextColor(dots[i].done ? rgb(theme::text) : rgb(theme::textDim));
        c_.drawString(dots[i].name, dx + 14, 194);
    }
    c_.setTextDatum(textdatum_t::top_center);
    c_.setFont(&fonts::DejaVu9);
    c_.setTextColor(view.tokenRejected ? rgb(theme::fault) : rgb(theme::textDim));
    const char* footer = view.tokenRejected ? "Flipped refused the saved token. Scan again to link your account."
                         : token            ? "This links the display to your Flipped account."
                         : view.wifiConnected ? "Connected. Waiting for your home to finish."
                                              : "Keep the phone near the display while it connects.";
    c_.drawString(footer, 160, 208);
    c_.setFont(&fonts::DejaVu9);
    c_.drawString(model.configured ? "Hold the middle button any time to see this again" : "", 160, 224);
}

void Renderer::status(const ui::ScreenModel& model, const ui::NavState& nav)
{
    int16_t y = 32;
    std::size_t first = static_cast<std::size_t>(std::max(0, nav.scroll)) * 4;
    for (std::size_t i = first; i < model.status.rows.size() && y < 222; ++i) {
        const ui::StatusRow& row = model.status.rows[i];
        c_.setTextDatum(textdatum_t::top_left);
        c_.setFont(&fonts::DejaVu9);
        c_.setTextColor(rgb(theme::textDim));
        c_.drawString(row.name.c_str(), 8, y);
        c_.setFont(&fonts::DejaVu12);
        c_.setTextColor(row.tone == ui::Tone::neutral ? rgb(theme::text) : rgb(theme::toneColour(row.tone)));
        int16_t lines = wrap(8, y + 11, 304, row.value, 13, 4);
        y += static_cast<int16_t>(20 + 13 * lines);
    }
}

void Renderer::settings(const ui::ScreenModel& model, const UiSettings& settings, const ui::NavState& nav)
{
    (void)model;
    char value[16];
    std::snprintf(value, sizeof value, "%d%%", settings.brightness * 100 / 255);
    struct Row {
        const char* name;
        const char* note;
        std::string value;
        bool stepper;
    };
    Row rows[] = {{"Brightness", "Screen backlight", value, true},
                  {"LED bars", "Left: your rate. Right: wholesale", std::to_string(settings.leds ? settings.ledBrightness * 100 / 255 : 0) + "%", false},
                  {"Night dimming", "10 pm to 6 am", settings.dimAtNight ? "On" : "Off", false},
                  {"Beep", "Price alerts and rate changes", settings.beep ? "On" : "Off", false},
                  {"Virtual devices", "Plugs, meters and rate signals", model.virtualDevices ? "On" : "Off", false},
                  {"Spot prices", "Wholesale prices and alerts", model.showSpotPrices ? "On" : "Off", false}};
    const int first = std::min(nav.scroll, 2);
    for (int i = first; i < first + 4; ++i) {
        int16_t y = static_cast<int16_t>(kSettingsRowTop + (i - first) * kSettingsRowHeight);
        c_.fillSmoothRoundRect(6, y, 308, kSettingsRowHeight - 6, 8, rgb(theme::surface));
        c_.setTextDatum(textdatum_t::top_left);
        c_.setFont(fontSmallBold());
        c_.setTextColor(rgb(theme::text));
        c_.drawString(rows[i].name, 16, y + 4);
        c_.setFont(&fonts::DejaVu9);
        c_.setTextColor(rgb(theme::textDim));
        c_.drawString(rows[i].note, 16, y + 23);
        if (i == 1) {
            int16_t level = settings.leds ? settings.ledBrightness * 106 / 255 : 0;
            c_.fillSmoothRoundRect(200, y + 16, 106, 6, 3, rgb(theme::surfaceHigh));
            if (level > 0) {
                c_.fillSmoothRoundRect(200, y + 16, level, 6, 3, rgb(theme::primary));
            }
            c_.fillSmoothCircle(200 + level, y + 19, 7, rgb(theme::text));
        } else if (rows[i].stepper) {
            c_.fillSmoothRoundRect(200, y + 6, 26, 26, 13, rgb(theme::surfaceHigh));
            c_.fillSmoothRoundRect(280, y + 6, 26, 26, 13, rgb(theme::surfaceHigh));
            c_.setFont(fontSmallBold());
            c_.setTextDatum(textdatum_t::middle_center);
            c_.setTextColor(rgb(theme::primary));
            c_.drawString("-", 213, y + 19);
            c_.drawString("+", 293, y + 19);
            c_.setTextColor(rgb(theme::text));
            c_.drawString(rows[i].value.c_str(), 253, y + 19);
        } else {
            bool on = rows[i].value == "On";
            c_.fillSmoothRoundRect(262, y + 9, 44, 22, 11, on ? rgb(theme::primary) : rgb(theme::surfaceHigh));
            c_.fillSmoothCircle(on ? 295 : 273, y + 20, 8, rgb(theme::text));
        }
    }
}

void Renderer::reset(const ui::ScreenModel& model, uint8_t holdPercent)
{
    (void)model;
    c_.setTextDatum(textdatum_t::top_center);
    c_.setFont(fontMedium());
    c_.setTextColor(rgb(theme::fault));
    c_.drawString("Factory reset", 160, 44);
    c_.setFont(&fonts::DejaVu12);
    c_.setTextColor(rgb(theme::text));
    c_.drawString("Removes this display from your home,", 160, 82);
    c_.drawString("forgets Wi-Fi and the Flipped token.", 160, 100);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString("Hold the middle button for 3 seconds.", 160, 132);
    c_.fillSmoothRoundRect(40, 160, 240, 14, 7, rgb(theme::surface));
    if (holdPercent > 0) {
        c_.fillSmoothRoundRect(40, 160, static_cast<int16_t>(240 * holdPercent / 100), 14, 7, rgb(theme::fault));
    }
}

void Renderer::draw(const ui::ScreenModel& model, const ui::NavState& nav, const UiSettings& uiSettings, uint8_t holdPercent)
{
    c_.fillScreen(rgb(theme::background));
    switch (nav.overlay) {
    case ui::Overlay::pairing:
        header(model.pairing.step == ui::PairStep::addToken ? "Link your account" : "Add to Apple Home", model, model.configured);
        pairing(model);
        return;
    case ui::Overlay::status:
        header("Device status", model, true);
        status(model, nav);
        return;
    case ui::Overlay::settings:
        header("Screen and lights", model, true);
        settings(model, uiSettings, nav);
        return;
    case ui::Overlay::reset:
        header("Factory reset", model, true);
        reset(model, holdPercent);
        return;
    case ui::Overlay::none:
        break;
    }
    constexpr const char* titles[] = {"Flipped Energy", "Wholesale prices", "Your usage", "Your costs", "More"};
    std::size_t index = 0;
    for (std::size_t i = 0; i < ui::kTabs.size(); ++i) {
        if (ui::kTabs[i] == nav.tab) {
            index = i;
        }
    }
    header(titles[index], model, false);
    switch (nav.tab) {
    case ui::Tab::home:
        home(model);
        break;
    case ui::Tab::prices:
        prices(model, nav);
        break;
    case ui::Tab::usage:
        usage(model, nav);
        break;
    case ui::Tab::costs:
        costs(model);
        break;
    case ui::Tab::more:
        more(model);
        break;
    }
    tabBar(nav.tab, model.showSpotPrices);
}

void Renderer::drawBoot(uint8_t step, uint8_t steps, const char* line)
{
    c_.fillScreen(rgb(theme::background));
    float t = steps == 0 ? 1.0f : static_cast<float>(step) / steps;
    float grow = std::min(1.0f, t * 1.6f);
    float glow = 30.0f + 30.0f * grow;
    for (int r = static_cast<int>(glow); r > 0; r -= 3) {
        uint8_t amount = static_cast<uint8_t>(255 - 200 * r / glow);
        c_.fillSmoothCircle(160, 92, r, blend(theme::background, theme::primaryDark, static_cast<uint8_t>(amount * grow / 3)));
    }
    if (assets_.bolt) {
        float scale = 0.4f + 0.6f * grow;
        c_.drawPng(assets_.bolt, assets_.boltSize, 160, 92, 0, 0, 0, 0, scale, scale, textdatum_t::middle_center);
    }
    if (assets_.wordmark && t > 0.45f) {
        c_.drawPng(assets_.wordmark, assets_.wordmarkSize, 160, 166, 0, 0, 0, 0, 1.0f, 1.0f, textdatum_t::middle_center);
    }
    c_.fillSmoothRoundRect(110, 206, 100, 4, 2, rgb(theme::surface));
    c_.fillSmoothRoundRect(110, 206, static_cast<int16_t>(100 * t), 4, 2, rgb(theme::primary));
    c_.setFont(&fonts::DejaVu9);
    c_.setTextDatum(textdatum_t::top_center);
    c_.setTextColor(rgb(theme::textDim));
    c_.drawString(line, 160, 218);
}

}
