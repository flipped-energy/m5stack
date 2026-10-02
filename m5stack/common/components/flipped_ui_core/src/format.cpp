#include "flipped/ui/format.h"

#include <cmath>
#include <cstdio>

namespace flipped::ui {

namespace {

std::string printf1(const char* pattern, double value)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, pattern, value);
    return buffer;
}

}

std::string fmtCents(double centsPerKwh)
{
    double rounded = std::round(centsPerKwh * 10.0) / 10.0;
    if (rounded == 0.0) {
        rounded = 0.0;
    }
    if (std::fabs(rounded) >= 1000.0) {
        return printf1("%.0f", rounded);
    }
    return printf1("%.1f", rounded);
}

std::string fmtClock(int hour24, int minute)
{
    int hour12 = hour24 % 12 == 0 ? 12 : hour24 % 12;
    char buffer[16];
    std::snprintf(buffer, sizeof buffer, "%d:%02d %s", hour12, minute, hour24 < 12 ? "am" : "pm");
    return buffer;
}

std::string fmtHour(int hour24)
{
    int hour12 = hour24 % 12 == 0 ? 12 : hour24 % 12;
    char buffer[8];
    std::snprintf(buffer, sizeof buffer, "%d%s", hour12, hour24 % 24 < 12 ? "am" : "pm");
    return buffer;
}

std::string fmtRemaining(int64_t seconds)
{
    if (seconds < 60) {
        return "under 1 m";
    }
    int64_t minutes = seconds / 60;
    int64_t hours = minutes / 60;
    minutes %= 60;
    char buffer[24];
    if (hours >= 24) {
        std::snprintf(buffer, sizeof buffer, "%lld d %lld h", static_cast<long long>(hours / 24), static_cast<long long>(hours % 24));
    } else if (hours > 0) {
        std::snprintf(buffer, sizeof buffer, "%lld h %lld m", static_cast<long long>(hours), static_cast<long long>(minutes));
    } else {
        std::snprintf(buffer, sizeof buffer, "%lld m", static_cast<long long>(minutes));
    }
    return buffer;
}

std::string fmtKwh(double kwh)
{
    return printf1(std::fabs(kwh) < 10.0 ? "%.2f kWh" : "%.1f kWh", kwh);
}

std::string fmtAud(double aud)
{
    if (aud < 0) {
        return "-" + printf1("$%.2f", -aud);
    }
    return printf1("$%.2f", aud);
}

std::string fmtAudOrDash(const std::optional<double>& aud)
{
    return aud ? fmtAud(*aud) : std::string("--");
}

}
