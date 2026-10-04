#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace flipped::ui {

std::string fmtCents(double centsPerKwh);
std::string fmtClock(int hour24, int minute);
std::string fmtHour(int hour24);
std::string fmtRemaining(int64_t seconds);
std::string fmtKwh(double kwh);
std::string fmtAud(double aud);
std::string fmtAudOrDash(const std::optional<double>& aud);

}
