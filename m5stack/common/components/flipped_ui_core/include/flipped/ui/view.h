#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace flipped::ui {

enum class Tone : uint8_t { neutral, low, mid, high, spike, fault, free };

enum class Tab : uint8_t { home, prices, usage, costs, more };
constexpr std::array<Tab, 5> kTabs{Tab::home, Tab::prices, Tab::usage, Tab::costs, Tab::more};

inline std::size_t tabCount(bool showSpotPrices) { return showSpotPrices ? kTabs.size() : kTabs.size() - 1; }
inline Tab tabAt(std::size_t index, bool showSpotPrices) { return kTabs[index + (!showSpotPrices && index >= 1 ? 1 : 0)]; }

struct Figure {
    std::string label;
    std::string value;
    std::string unit;
    std::string note;
    Tone tone = Tone::neutral;
    bool faulted = false;
};

struct Chip {
    std::string title;
    std::optional<bool> on;
    Tone onTone = Tone::mid;
};

enum ChipSlot : std::size_t { kPeakChip, kOffPeakChip, kShoulderChip, kPriceHighChip, kPriceLowChip, kChipCount };

struct StripRun {
    int startMinute = 0;
    int endMinute = 0;
    Tone tone = Tone::neutral;
};

struct HomeView {
    Figure rate;
    Figure spot;
    std::string periodLine;
    std::string periodDetail;
    std::vector<StripRun> strip;
    std::optional<int> nowMinute;
    std::array<Chip, kChipCount> chips;
};

struct Bar {
    double value = 0;
    Tone tone = Tone::mid;
    std::string label;
    std::string detail;
};

struct Chart {
    std::vector<Bar> bars;
    std::string axisStart;
    std::string axisMiddle;
    std::string axisEnd;
    std::optional<double> highLine;
    std::optional<double> lowLine;
};

struct PricesView {
    Figure now;
    std::string tierLine;
    std::string sourceLine;
    Chart nextHour;
    Chart ahead;
    std::string faultText;
};

struct UsageDay {
    std::string weekday;
    std::string dayOfMonth;
    std::string title;
    double importKwh = 0;
    double exportKwh = 0;
    std::optional<double> costAud;
    std::optional<double> creditAud;
    bool hasData = false;
    std::array<std::optional<double>, 24> hourImportKwh{};
    std::array<std::optional<double>, 24> hourExportKwh{};
    std::array<std::optional<double>, 24> hourCostAud{};
};

struct UsageView {
    std::vector<UsageDay> days;
    std::string latestLine;
    std::string faultText;
};

struct CostRow {
    int startMinute = 0;
    std::string name;
    std::string window;
    std::string rate;
    Tone tone = Tone::neutral;
    bool current = false;
};

struct CostsView {
    Figure rateNow;
    std::vector<CostRow> schedule;
    std::string allowanceLine;
    Figure lastDay;
    Figure lastWeek;
    Figure feedIn;
    std::string faultText;
};

enum class PairStep : uint8_t { addToHome, connecting, addToken, done };

struct PairingView {
    PairStep step = PairStep::addToHome;
    std::string qrPayload;
    std::string manualCode;
    bool bluetooth = false;
    bool wifiConnected = false;
    std::string wifiName;
    uint8_t homes = 0;
    bool windowOpen = false;
    bool tokenSaved = false;
    bool tokenRejected = false;
    std::string tokenUrl;
    std::string tokenCode;
};

struct StatusRow {
    std::string name;
    std::string value;
    Tone tone = Tone::neutral;
};

struct StatusView {
    std::vector<StatusRow> rows;
    std::string firmware;
};

struct ScreenModel {
    bool configured = false;
    bool showSpotPrices = true;
    bool virtualDevices = true;
    std::string clock;
    std::optional<int> localMinute;
    std::string title;
    bool wifiConnected = false;
    uint8_t homes = 0;
    HomeView home;
    PricesView prices;
    UsageView usage;
    CostsView costs;
    PairingView pairing;
    StatusView status;
};

}
