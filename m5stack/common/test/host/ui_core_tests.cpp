#include <cstdio>
#include <cstdlib>
#include <string>

#include "flipped/core/switch_plan.h"
#include "flipped/core/time.h"
#include "flipped/ui/build.h"
#include "flipped/ui/format.h"
#include "flipped/ui/led_frame.h"
#include "flipped/ui/navigator.h"
#include "flipped/ui/theme.h"

namespace core = flipped::core;
namespace ui = flipped::ui;

namespace {

int failures = 0;
int checks = 0;

void check(bool ok, const char* what, const std::string& got = {})
{
    ++checks;
    if (!ok) {
        ++failures;
        std::printf("FAIL %s %s\n", what, got.c_str());
    }
}

void checkEq(const std::string& got, const std::string& want, const char* what)
{
    check(got == want, what, "got '" + got + "' want '" + want + "'");
}

core::Instant at(const char* text) { return *core::parseInstant(text).instant; }

int64_t key(double cents) { return static_cast<int64_t>(cents * 1e7 + (cents >= 0 ? 0.5 : -0.5)); }

core::Segment segment(core::Band band, const char* name, double cents)
{
    core::Segment s;
    s.band = band;
    s.name = name;
    s.rateKey = key(cents);
    return s;
}

core::Signals sampleSignals()
{
    core::Signals s;
    s.account.accountNumber = "36200000000001";
    s.account.accountState = "ACTIVE";
    s.account.timeZone = "Australia/Melbourne";
    s.tariff.structure = core::Structure::timeOfUse;
    s.tariff.peak = true;
    s.tariff.period.segment = segment(core::Band::peak, "Evening", 46.75891);
    s.tariff.nextChange = at("2026-10-01T11:00:00Z");
    s.tariff.schedule = {{0, 540, segment(core::Band::shoulder, "Overnight", 13.80236)},
                         {540, 1020, segment(core::Band::offPeak, "Day", 11.31064)},
                         {1020, 1260, segment(core::Band::peak, "Evening", 46.75891)},
                         {1260, 1440, segment(core::Band::shoulder, "Overnight", 13.80236)}};
    s.price.centsPerKwh = 11.85;
    s.price.tier = "Normal";
    s.price.intervalStart = at("2026-10-01T07:30:00Z");
    core::EnergyEntry day;
    day.local = "2026-09-30T00:00:00";
    day.gridImportKwh = 20.8;
    day.costAud = 3.62;
    s.energy.days = {day};
    core::EnergyEntry a;
    a.local = "2026-09-30T18:00:00";
    a.gridImportKwh = 0.5;
    a.costAud = 0.23;
    core::EnergyEntry b = a;
    b.local = "2026-09-30T18:30:00";
    b.gridImportKwh = 0.44;
    b.costAud = 0.21;
    s.energy.intervals = {a, b};
    s.energy.latestIntervalEnd = at("2026-09-30T14:00:00Z");
    return s;
}

ui::DeviceFacts configuredFacts()
{
    ui::DeviceFacts f;
    f.wifiConnected = true;
    f.homes = 1;
    f.hasToken = true;
    return f;
}

void formatTests()
{
    checkEq(ui::fmtCents(46.75891), "46.8", "fmtCents rounds to one decimal");
    checkEq(ui::fmtCents(-0.04), "0.0", "fmtCents never shows negative zero");
    checkEq(ui::fmtCents(-3.25), "-3.3", "fmtCents keeps the sign");
    checkEq(ui::fmtClock(17, 0), "5:00 pm", "fmtClock afternoon");
    checkEq(ui::fmtClock(0, 5), "12:05 am", "fmtClock midnight hour");
    checkEq(ui::fmtRemaining(3 * 3600 + 9 * 60 + 30), "3 h 9 m", "fmtRemaining hours and minutes");
    checkEq(ui::fmtRemaining(45), "under 1 m", "fmtRemaining under a minute");
    checkEq(ui::fmtAud(-1.5), "-$1.50", "fmtAud negative");
    checkEq(ui::fmtKwh(0.94), "0.94 kWh", "fmtKwh small");
}

void buildTests()
{
    core::Instant now = at("2026-10-01T07:51:00Z");
    ui::ScreenModel m = ui::buildScreen(sampleSignals(), configuredFacts(), now);
    check(m.configured, "configured with Wi-Fi, a home and a token");
    checkEq(m.clock, "5:51 pm", "clock is local Melbourne time");
    checkEq(m.home.rate.value, "46.8", "home rate value");
    check(m.home.rate.tone == ui::Tone::high, "peak rate is the high tone");
    checkEq(m.home.periodLine, "Evening until 9:00 pm, 3 h 9 m left", "period line counts down to the next change");
    checkEq(m.home.periodDetail, "Next: Overnight 13.8 c/kWh", "period detail names the next segment");
    check(m.home.chips.size() == 5, "five signal chips");
    check(m.home.chips[ui::kPeakChip].on == std::optional<bool>(true) && m.home.chips[ui::kOffPeakChip].on == std::optional<bool>(false) &&
              m.home.chips[ui::kShoulderChip].on == std::optional<bool>(false),
          "peak, off-peak and shoulder chips during the peak period");
    checkEq(m.home.chips[ui::kShoulderChip].title, "Shoulder", "shoulder chip title");
    check(m.home.chips[ui::kShoulderChip].onTone == ui::Tone::mid, "shoulder chip is the mid tone like shoulder on the strip");
    check(m.home.chips[ui::kPriceHighChip].on == std::optional<bool>(false) && m.home.chips[ui::kPriceLowChip].on == std::optional<bool>(false),
          "wholesale chips");
    check(m.home.nowMinute == std::optional<int>(17 * 60 + 51), "day strip marker at the local minute");
    checkEq(m.home.spot.note, "Normal", "spot note is the tier");
    check(m.costs.schedule.size() == 3, "overnight runs merge into one cost row", std::to_string(m.costs.schedule.size()));
    checkEq(m.costs.schedule.front().name, "Day", "cost rows sorted by start time");
    checkEq(m.costs.schedule.back().window, "9:00 pm - 9:00 am", "merged overnight window crosses midnight");
    check(m.costs.schedule[1].current, "current segment row is marked");
    check(m.usage.days.size() == 1 && m.usage.days[0].title == "Wed 30 Sep", "usage day title", m.usage.days.empty() ? "" : m.usage.days[0].title);
    check(m.usage.days[0].hourImportKwh[18] && *m.usage.days[0].hourImportKwh[18] > 0.93 && *m.usage.days[0].hourImportKwh[18] < 0.95,
          "two half hours sum into one hour");
    checkEq(m.usage.latestLine, "Meter data to Wed 30 Sep 18:30", "latest reading line");

    core::Signals faulted = sampleSignals();
    faulted.tariff.fault = core::Fault{"invalid_response", std::nullopt, std::nullopt, std::nullopt, std::string("product.upcomingPlan is missing")};
    ui::ScreenModel f = ui::buildScreen(faulted, configuredFacts(), now);
    check(f.home.rate.faulted && f.home.rate.value == "--", "faulted tariff shows no rate");
    check(!f.home.chips[ui::kPeakChip].on && !f.home.chips[ui::kOffPeakChip].on && !f.home.chips[ui::kShoulderChip].on,
          "faulted tariff chips are unknown");
    check(f.home.chips[ui::kPriceHighChip].on && f.home.chips[ui::kPriceLowChip].on, "a faulted tariff leaves the wholesale chips known");

    core::Signals shoulder = sampleSignals();
    shoulder.tariff.peak = false;
    shoulder.tariff.period.segment = segment(core::Band::shoulder, "Overnight", 13.80236);
    shoulder.price.priceLow = true;
    ui::ScreenModel sh = ui::buildScreen(shoulder, configuredFacts(), now);
    check(sh.home.chips[ui::kShoulderChip].on == std::optional<bool>(true) && sh.home.chips[ui::kPeakChip].on == std::optional<bool>(false) &&
              sh.home.chips[ui::kOffPeakChip].on == std::optional<bool>(false),
          "only the shoulder chip is on during a shoulder period");
    check(sh.home.chips[ui::kPriceLowChip].on == std::optional<bool>(true), "price low chip follows price.priceLow");
    const auto switches = core::switchValues(shoulder);
    bool same = true;
    for (std::size_t i = 0; i < ui::kChipCount; ++i) {
        same = same && sh.home.chips[i].on == switches[i];
    }
    check(same, "every chip shows the value of its Matter switch");

    core::Signals noPrice = sampleSignals();
    noPrice.price.fault = core::Fault{"http_error", 503, std::string("down"), 4, std::nullopt};
    ui::ScreenModel np = ui::buildScreen(noPrice, configuredFacts(), now);
    check(!np.home.chips[ui::kPriceHighChip].on && !np.home.chips[ui::kPriceLowChip].on && np.home.chips[ui::kShoulderChip].on,
          "a faulted price group leaves only the wholesale chips unknown");
    check(f.status.rows[3].value.find("product.upcomingPlan is missing") != std::string::npos, "status shows the raw fault message");

    ui::DeviceFacts unpaired;
    ui::ScreenModel p = ui::buildScreen(sampleSignals(), unpaired, now);
    check(!p.configured && p.pairing.step == ui::PairStep::addToHome, "no home means step one of pairing");
    ui::DeviceFacts noToken = configuredFacts();
    noToken.hasToken = false;
    check(ui::buildScreen(sampleSignals(), noToken, now).pairing.step == ui::PairStep::addToken, "home but no token means step two");
}

void navigatorTests()
{
    ui::ScreenModel m = ui::buildScreen(sampleSignals(), configuredFacts(), at("2026-10-01T07:51:00Z"));
    ui::Navigator nav;
    nav.sync(m);
    check(nav.state().tab == ui::Tab::home && nav.state().overlay == ui::Overlay::none, "starts on home");
    check(nav.apply(ui::Input::tap, ui::Hit{96, 220}, m) && nav.state().tab == ui::Tab::prices && nav.direction() == 1, "tab bar tap slides right");
    check(nav.apply(ui::Input::swipeRight, {}, m) && nav.state().tab == ui::Tab::home, "swipe right goes back a tab");
    check(!nav.apply(ui::Input::swipeRight, {}, m), "no tab before home");
    nav.apply(ui::Input::tap, ui::Hit{160, 220}, m);
    check(nav.state().usageDay == 0, "usage starts on the newest day with data");
    check(nav.apply(ui::Input::tap, ui::Hit{100, 120}, m) && nav.state().usageHourly, "tapping the selected day opens hours");
    check(nav.apply(ui::Input::buttonA, {}, m) && !nav.state().usageHourly, "button A leaves the hourly view first");
    check(nav.apply(ui::Input::holdB, {}, m) && nav.state().overlay == ui::Overlay::pairing, "holding B opens pairing");
    check(nav.apply(ui::Input::buttonA, {}, m) && nav.state().overlay == ui::Overlay::none, "button A closes an overlay");

    nav.apply(ui::Input::tap, ui::Hit{96, 220}, m);
    m.showSpotPrices = false;
    nav.sync(m);
    check(nav.state().tab == ui::Tab::home, "hiding prices leaves the price screen");
    check(nav.apply(ui::Input::swipeLeft, {}, m) && nav.state().tab == ui::Tab::usage, "hidden prices are skipped by swipes");
    nav.apply(ui::Input::tap, ui::Hit{10, 220}, m);
    check(nav.apply(ui::Input::tap, ui::Hit{100, 220}, m) && nav.state().tab == ui::Tab::usage, "tab positions follow the visible tabs");

    ui::ScreenModel unpaired = ui::buildScreen(sampleSignals(), ui::DeviceFacts{}, std::nullopt);
    ui::Navigator setup;
    setup.sync(unpaired);
    check(setup.state().overlay == ui::Overlay::pairing, "unpaired device shows pairing");
    check(!setup.apply(ui::Input::buttonA, {}, unpaired) && setup.state().overlay == ui::Overlay::pairing, "pairing cannot be dismissed until set up");
}

void ledTests()
{
    ui::LedInput in;
    in.plan = ui::Tone::high;
    in.wholesale = ui::Tone::low;
    in.brightness = 255;
    auto frame = ui::ledFrame(in);
    check(frame[0] == ui::theme::high && frame[5] == ui::theme::low, "left bar is the plan, right bar is wholesale");
    in.enabled = false;
    auto off = ui::ledFrame(in);
    check(off[0] == ui::Rgb{0, 0, 0} && !ui::ledAnimated(in), "disabled LEDs are dark and still");
    in.enabled = true;
    in.wholesale = ui::Tone::spike;
    check(ui::ledAnimated(in), "a price spike pulses");
}

}

int main()
{
    formatTests();
    buildTests();
    navigatorTests();
    ledTests();
    std::printf("ui_core_tests: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
