#include "flipped/ui/build.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <string_view>

#include "flipped/core/constants.h"
#include "flipped/core/switch_plan.h"
#include "flipped/core/time.h"
#include "flipped/core/tz_table.h"
#include "flipped/ui/format.h"

namespace flipped::ui {

namespace {

constexpr const char* kWeekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

double cents(int64_t key) { return static_cast<double>(key) / core::RATE_OUTPUT_DIVISOR; }

struct Clock {
    std::optional<core::TimeZone> zone;

    std::optional<core::LocalTime> local(core::Instant instant) const
    {
        if (!zone) {
            return std::nullopt;
        }
        return core::toLocal(instant, *zone);
    }

    std::string time(core::Instant instant) const
    {
        auto lt = local(instant);
        return lt ? fmtClock(lt->minuteOfDay / 60, lt->minuteOfDay % 60) : std::string("--");
    }
};

int dayOfWeek(std::string_view date)
{
    int year = std::atoi(std::string(date.substr(0, 4)).c_str());
    int month = std::atoi(std::string(date.substr(5, 2)).c_str());
    int day = std::atoi(std::string(date.substr(8, 2)).c_str());
    int64_t days = core::daysFromCivil(year, month, day);
    int64_t weekday = (days + 4) % 7;
    return static_cast<int>(weekday < 0 ? weekday + 7 : weekday);
}

std::string dayTitle(std::string_view date)
{
    int month = std::atoi(std::string(date.substr(5, 2)).c_str());
    int day = std::atoi(std::string(date.substr(8, 2)).c_str());
    char buffer[24];
    std::snprintf(buffer, sizeof buffer, "%s %d %s", kWeekdays[dayOfWeek(date)], day, kMonths[(month + 11) % 12]);
    return buffer;
}

std::string faultLine(const core::Fault& fault)
{
    std::string text = fault.code;
    if (fault.httpStatus) {
        text += " (HTTP " + std::to_string(*fault.httpStatus) + ")";
    }
    return text;
}

Tone bandTone(core::Band band, int64_t rateKey)
{
    if (rateKey == 0) {
        return Tone::free;
    }
    switch (band) {
    case core::Band::peak:
        return Tone::high;
    case core::Band::offPeak:
        return Tone::low;
    case core::Band::shoulder:
        return Tone::mid;
    case core::Band::anytime:
        return Tone::mid;
    }
    return Tone::mid;
}

const char* bandName(core::Band band)
{
    switch (band) {
    case core::Band::peak:
        return "Peak";
    case core::Band::offPeak:
        return "Off-peak";
    case core::Band::shoulder:
        return "Shoulder";
    case core::Band::anytime:
        return "Anytime";
    }
    return "";
}

std::string segmentName(const core::Segment& segment)
{
    return segment.name.empty() ? std::string(bandName(segment.band)) : segment.name;
}

Tone tierTone(const std::string& tier, bool high, bool low)
{
    if (tier == "Spike") {
        return Tone::spike;
    }
    if (high || tier == "Elevated") {
        return Tone::high;
    }
    if (low || tier == "UnusuallyLow") {
        return Tone::low;
    }
    return Tone::mid;
}

std::string tierLabel(const std::string& tier)
{
    if (tier == "UnusuallyLow") {
        return "Unusually low";
    }
    return tier;
}

std::string window(int startMinute, int endMinute)
{
    return fmtClock(startMinute / 60, startMinute % 60) + " - " + fmtClock((endMinute % 1440) / 60, endMinute % 60);
}

Chart forecastChart(const core::PriceForecast& forecast, const Clock& clock, const DeviceFacts& facts)
{
    Chart chart;
    for (const auto& point : forecast.points) {
        Bar bar;
        bar.value = point.centsPerKwh;
        bool high = facts.priceHighThreshold ? point.centsPerKwh >= *facts.priceHighThreshold : false;
        bool low = facts.priceLowThreshold ? point.centsPerKwh <= *facts.priceLowThreshold : false;
        bar.tone = point.centsPerKwh < 0 ? Tone::low : tierTone("", high, low);
        bar.label = clock.time(point.start);
        bar.detail = fmtCents(point.centsPerKwh) + " c/kWh";
        chart.bars.push_back(bar);
    }
    if (!forecast.points.empty()) {
        chart.axisStart = clock.time(forecast.points.front().start);
        chart.axisMiddle = clock.time(forecast.points[forecast.points.size() / 2].start);
        chart.axisEnd = clock.time(forecast.points.back().start);
    }
    chart.highLine = facts.priceHighThreshold;
    chart.lowLine = facts.priceLowThreshold;
    return chart;
}

constexpr bool chipIsSwitch(ChipSlot slot, std::string_view key) { return core::SWITCH_IDENTITIES[slot].key == key; }

void buildHome(ScreenModel& screen, const core::Signals& s, const Clock& clock, std::optional<core::Instant> now)
{
    HomeView& home = screen.home;
    const core::TariffGroup& tariff = s.tariff;
    if (tariff.fault) {
        home.rate = {"Your rate", "--", "c/kWh", faultLine(*tariff.fault), Tone::fault, true};
        home.periodLine = "Rates unavailable";
        home.periodDetail = tariff.fault->message.value_or(tariff.fault->code) + "  (More > Device status)";
    } else {
        const core::Segment& seg = tariff.period.segment;
        Tone tone = bandTone(seg.band, seg.rateKey);
        std::string note = std::string(bandName(seg.band));
        if (!seg.name.empty() && seg.name != note) {
            note += "  " + seg.name;
        }
        if (seg.wholesaleLinked) {
            home.rate = {"Fixed part", fmtCents(cents(seg.rateKey)), "c/kWh", "+ wholesale", Tone::mid, false};
        } else {
            home.rate = {"Your rate", fmtCents(cents(seg.rateKey)), "c/kWh", note, tone, false};
        }
        if (tariff.structure == core::Structure::flat || !tariff.nextChange) {
            home.periodLine = segmentName(seg) + ", same rate all day";
        } else {
            std::string remaining = now ? fmtRemaining(*tariff.nextChange - *now) : std::string("--");
            home.periodLine = segmentName(seg) + " until " + clock.time(*tariff.nextChange) + ", " + remaining + " left";
        }
        if (seg.kwhLimit && seg.rateAfterLimitKey) {
            char line[80];
            std::snprintf(line, sizeof line, "First %.0f kWh/day at %s c, then %s c", *seg.kwhLimit, fmtCents(cents(seg.rateKey)).c_str(),
                          fmtCents(cents(*seg.rateAfterLimitKey)).c_str());
            home.periodDetail = line;
        } else if (tariff.nextChange) {
            auto lt = clock.local(*tariff.nextChange);
            for (const auto& entry : tariff.schedule) {
                if (lt && entry.startMinute == lt->minuteOfDay) {
                    home.periodDetail = "Next: " + segmentName(entry.segment) + " " + fmtCents(cents(entry.segment.rateKey)) + " c/kWh";
                }
            }
        }
        for (const auto& entry : tariff.schedule) {
            home.strip.push_back({entry.startMinute, entry.endMinute, bandTone(entry.segment.band, entry.segment.rateKey)});
        }
    }
    if (now) {
        if (auto lt = clock.local(*now)) {
            home.nowMinute = lt->minuteOfDay;
        }
    }
    const core::PriceGroup& price = s.price;
    if (price.fault) {
        home.spot = {"Spot price", "--", "c/kWh", faultLine(*price.fault), Tone::fault, true};
    } else {
        Tone tone = price.negative ? Tone::low : tierTone(price.tier, price.priceHigh, price.priceLow);
        home.spot = {"Spot price", fmtCents(price.centsPerKwh), "c/kWh", tierLabel(price.tier), tone, false};
    }
    static_assert(kChipCount == core::SWITCH_COUNT && chipIsSwitch(kPeakChip, "peak_rate") &&
                      chipIsSwitch(kOffPeakChip, "off_peak_rate") && chipIsSwitch(kShoulderChip, "shoulder_rate") &&
                      chipIsSwitch(kPriceHighChip, "wholesale_price_high") && chipIsSwitch(kPriceLowChip, "wholesale_price_low"),
                  "the chip slots follow the Matter switch order");
    const std::array<std::optional<bool>, core::SWITCH_COUNT> switches = core::switchValues(s);
    home.chips[kPeakChip] = Chip{"Peak", switches[kPeakChip], Tone::high};
    home.chips[kOffPeakChip] = Chip{"Off-peak", switches[kOffPeakChip], Tone::low};
    home.chips[kShoulderChip] = Chip{"Shoulder", switches[kShoulderChip], Tone::mid};
    home.chips[kPriceHighChip] = Chip{"Price high", switches[kPriceHighChip], Tone::high};
    home.chips[kPriceLowChip] = Chip{"Price low", switches[kPriceLowChip], Tone::low};
}

void buildPrices(ScreenModel& screen, const core::Signals& s, const Clock& clock, const DeviceFacts& facts)
{
    PricesView& view = screen.prices;
    const core::PriceGroup& price = s.price;
    if (price.fault) {
        view.now = {"Spot price now", "--", "c/kWh ex GST", "", Tone::fault, true};
        view.faultText = faultLine(*price.fault);
        return;
    }
    Tone tone = price.negative ? Tone::low : tierTone(price.tier, price.priceHigh, price.priceLow);
    view.now = {"Spot price now", fmtCents(price.centsPerKwh), "c/kWh ex GST", "", tone, false};
    view.tierLine = tierLabel(price.tier);
    std::string published = price.nextHour ? clock.time(price.nextHour->publishedAt) : clock.time(price.intervalStart);
    view.sourceLine = "AEMO " + published + ". Wholesale price, not your rate.";
    if (price.nextHour) {
        view.nextHour = forecastChart(*price.nextHour, clock, facts);
    }
    if (price.ahead) {
        view.ahead = forecastChart(*price.ahead, clock, facts);
    }
}

void buildUsage(ScreenModel& screen, const core::Signals& s)
{
    UsageView& view = screen.usage;
    const core::EnergyGroup& energy = s.energy;
    if (energy.fault) {
        view.faultText = faultLine(*energy.fault);
        return;
    }
    std::map<std::string, UsageDay> byDate;
    for (const auto& entry : energy.days) {
        std::string date = entry.local.substr(0, 10);
        UsageDay& day = byDate[date];
        day.importKwh += entry.gridImportKwh + entry.controlledLoadKwh;
        day.exportKwh += entry.solarExportKwh;
        if (entry.costAud) {
            day.costAud = day.costAud.value_or(0) + *entry.costAud;
        }
        if (entry.feedInCreditAud) {
            day.creditAud = day.creditAud.value_or(0) + *entry.feedInCreditAud;
        }
        day.hasData = day.hasData || entry.gridImportKwh + entry.controlledLoadKwh + entry.solarExportKwh > 0;
    }
    for (const auto& entry : energy.intervals) {
        std::string date = entry.local.substr(0, 10);
        int hour = std::atoi(entry.local.substr(11, 2).c_str());
        UsageDay& day = byDate[date];
        day.hourImportKwh[hour] = day.hourImportKwh[hour].value_or(0) + entry.gridImportKwh + entry.controlledLoadKwh;
        day.hourExportKwh[hour] = day.hourExportKwh[hour].value_or(0) + entry.solarExportKwh;
        if (entry.costAud) {
            day.hourCostAud[hour] = day.hourCostAud[hour].value_or(0) + *entry.costAud;
        }
    }
    for (auto& [date, day] : byDate) {
        day.weekday = kWeekdays[dayOfWeek(date)];
        day.dayOfMonth = std::to_string(std::atoi(date.substr(8, 2).c_str()));
        day.title = dayTitle(date);
        view.days.push_back(day);
    }
    if (energy.latestIntervalEnd && !energy.intervals.empty()) {
        const auto& last = energy.intervals.back();
        view.latestLine = "Meter data to " + dayTitle(last.local.substr(0, 10)) + " " + last.local.substr(11, 5);
    } else {
        view.latestLine = "No meter readings yet";
    }
}

void buildCosts(ScreenModel& screen, const core::Signals& s, const Clock& clock)
{
    CostsView& view = screen.costs;
    const core::TariffGroup& tariff = s.tariff;
    if (tariff.fault) {
        view.rateNow = {"Your rate now", "--", "c/kWh inc GST", faultLine(*tariff.fault), Tone::fault, true};
        view.faultText = tariff.fault->message.value_or(tariff.fault->code);
    } else {
        const core::Segment& seg = tariff.period.segment;
        std::string until = tariff.nextChange ? "until " + clock.time(*tariff.nextChange) : std::string("all day");
        view.rateNow = {"Now: " + segmentName(seg), fmtCents(cents(seg.rateKey)), "c/kWh inc GST", until, bandTone(seg.band, seg.rateKey), false};
        for (const auto& entry : tariff.schedule) {
            std::string name = segmentName(entry.segment);
            std::string rate = fmtCents(cents(entry.segment.rateKey)) + " c";
            bool merged = false;
            for (auto& row : view.schedule) {
                if (row.name == name && row.rate == rate) {
                    merged = true;
                }
            }
            if (merged) {
                continue;
            }
            int start = entry.startMinute;
            int end = entry.endMinute;
            for (const auto& other : tariff.schedule) {
                if (&other != &entry && segmentName(other.segment) == name && other.segment.rateKey == entry.segment.rateKey) {
                    if (other.endMinute == 1440 && entry.startMinute == 0) {
                        start = other.startMinute;
                    }
                    if (other.startMinute == 0 && entry.endMinute == 1440) {
                        end = other.endMinute;
                    }
                }
            }
            bool current = segmentName(seg) == name && seg.rateKey == entry.segment.rateKey;
            view.schedule.push_back({start, name, window(start, end), rate, bandTone(entry.segment.band, entry.segment.rateKey), current});
        }
        std::sort(view.schedule.begin(), view.schedule.end(), [](const CostRow& a, const CostRow& b) { return a.startMinute < b.startMinute; });
        if (seg.kwhLimit && seg.rateAfterLimitKey) {
            char line[80];
            std::snprintf(line, sizeof line, "First %.0f kWh/day, then %s c", *seg.kwhLimit, fmtCents(cents(*seg.rateAfterLimitKey)).c_str());
            view.allowanceLine = line;
        }
    }
    const UsageView& usage = screen.usage;
    const UsageDay* last = nullptr;
    double weekCost = 0;
    double weekKwh = 0;
    double credit = 0;
    double exported = 0;
    bool anyCost = false;
    for (const auto& day : usage.days) {
        if (!day.hasData) {
            continue;
        }
        last = &day;
        weekKwh += day.importKwh;
        exported += day.exportKwh;
        if (day.costAud) {
            weekCost += *day.costAud;
            anyCost = true;
        }
        credit += day.creditAud.value_or(0);
    }
    if (last) {
        view.lastDay = {last->weekday + " " + last->dayOfMonth, fmtAudOrDash(last->costAud), "", fmtKwh(last->importKwh) + " used", Tone::neutral, false};
    } else {
        view.lastDay = {"Last day", "--", "", "no readings", Tone::neutral, false};
    }
    view.lastWeek = {"Last " + std::to_string(usage.days.size()) + " days", anyCost ? fmtAud(weekCost) : std::string("--"), "",
                     fmtKwh(weekKwh) + " used", Tone::neutral, false};
    view.feedIn = {"Feed-in credit", fmtAud(credit), "", fmtKwh(exported) + " exported", Tone::low, false};
}

void buildPairing(ScreenModel& screen, const DeviceFacts& facts)
{
    PairingView& view = screen.pairing;
    view.qrPayload = facts.qrPayload;
    view.manualCode = facts.manualCode;
    view.bluetooth = facts.bluetooth;
    view.wifiConnected = facts.wifiConnected;
    view.wifiName = facts.wifiName;
    view.homes = facts.homes;
    view.windowOpen = facts.commissioningWindowOpen;
    view.tokenSaved = facts.hasToken;
    view.tokenRejected = facts.tokenRejected;
    view.tokenUrl = facts.tokenUrl;
    view.tokenCode = facts.tokenCode;
    if (facts.homes == 0) {
        view.step = facts.wifiConnected ? PairStep::connecting : PairStep::addToHome;
    } else if (!facts.hasToken || facts.tokenRejected) {
        view.step = PairStep::addToken;
    } else {
        view.step = PairStep::done;
    }
}

void buildStatus(ScreenModel& screen, const core::Signals& s, const DeviceFacts& facts, const Clock& clock)
{
    StatusView& view = screen.status;
    view.firmware = facts.firmwareVersion;
    char line[96];
    if (facts.wifiConnected) {
        std::snprintf(line, sizeof line, "%s, %d dBm, %s", facts.wifiName.c_str(), facts.wifiRssi, facts.wifiAddress.c_str());
        view.rows.push_back({"Wi-Fi", line, Tone::low});
    } else {
        view.rows.push_back({"Wi-Fi", facts.wifiName.empty() ? "Not set up" : "Not connected to " + facts.wifiName, Tone::fault});
    }
    view.rows.push_back({"Apple Home", facts.homes == 0 ? "Not paired" : "Paired with " + std::to_string(facts.homes) + (facts.homes == 1 ? " home" : " homes"),
                         facts.homes == 0 ? Tone::high : Tone::low});
    if (!facts.hasToken) {
        view.rows.push_back({"Flipped account", "No token yet", Tone::high});
    } else if (facts.tokenRejected) {
        view.rows.push_back({"Flipped account", "Token refused by Flipped", Tone::fault});
    } else {
        std::string account = s.account.fault ? faultLine(*s.account.fault)
                                              : s.account.accountNumber.value_or("") + ", " + s.account.accountState.value_or("");
        view.rows.push_back({"Flipped account", account, s.account.fault ? Tone::fault : Tone::neutral});
    }
    auto faultText = [](const core::Fault& fault) {
        std::string text = faultLine(fault);
        if (fault.message) {
            text += ": " + *fault.message;
        }
        if (fault.body) {
            text += " " + *fault.body;
        }
        return text;
    };
    view.rows.push_back({"Rates", s.tariff.fault ? faultText(*s.tariff.fault) : "ok", s.tariff.fault ? Tone::fault : Tone::neutral});
    std::string priceLine = s.price.fault ? faultLine(*s.price.fault) : "ok, " + clock.time(s.price.intervalStart);
    if (facts.dailyLimitRemaining) {
        priceLine += ", " + std::to_string(*facts.dailyLimitRemaining) + " calls left today";
    }
    view.rows.push_back({"Wholesale price", priceLine, s.price.fault ? Tone::fault : Tone::neutral});
    view.rows.push_back({"Usage history", s.energy.fault ? faultLine(*s.energy.fault) : screen.usage.latestLine, s.energy.fault ? Tone::fault : Tone::neutral});
    view.rows.push_back({"Clock", facts.clockSynced ? "Synchronised" : "Waiting for network time", facts.clockSynced ? Tone::neutral : Tone::high});
    view.rows.push_back({"Firmware", facts.firmwareVersion, Tone::neutral});
}

}

ScreenModel buildScreen(const core::Signals& signals, const DeviceFacts& facts, std::optional<core::Instant> now)
{
    ScreenModel screen;
    Clock clock;
    if (signals.account.timeZone) {
        clock.zone = core::timeZoneFor(*signals.account.timeZone);
    }
    screen.wifiConnected = facts.wifiConnected;
    screen.homes = facts.homes;
    if (now) {
        if (auto lt = clock.local(*now)) {
            screen.clock = fmtClock(lt->minuteOfDay / 60, lt->minuteOfDay % 60);
            screen.localMinute = lt->minuteOfDay;
        }
    }
    buildHome(screen, signals, clock, now);
    buildPrices(screen, signals, clock, facts);
    buildUsage(screen, signals);
    buildCosts(screen, signals, clock);
    buildPairing(screen, facts);
    buildStatus(screen, signals, facts, clock);
    screen.configured = screen.pairing.step == PairStep::done;
    return screen;
}

}
