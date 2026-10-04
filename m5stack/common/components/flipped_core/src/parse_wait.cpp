#include "flipped/core/parse.h"

#include "flipped/core/time.h"
#include "json_reader.h"

namespace flipped::core {

Parsed<WaitBody> parseWait(ByteSource &source)
{
    JsonDocument filter;
    filter["time"] = true;
    filter["averageCentsPerKwh"] = true;

    WaitBody body;
    std::optional<std::string> invalid;
    detail::SourceReader reader(source);
    std::optional<std::string> error = detail::readArray(reader, filter, [&](JsonObjectConst element) {
        if (invalid) {
            return;
        }
        const Field<std::string> time = detail::stringField(element["time"]);
        const Field<double> average = detail::numberField(element["averageCentsPerKwh"]);
        if (!time.present()) {
            invalid = "wait point time is missing or not a string";
            return;
        }
        const InstantText start = parseInstant(time.value);
        if (!start.instant) {
            invalid = "wait point time " + time.value + " is not a date-time with a UTC offset";
            return;
        }
        if (!average.present()) {
            invalid = "wait point " + time.value + " averageCentsPerKwh is missing or not a number";
            return;
        }
        body.points.push_back(WaitPoint{*start.instant, time.value, average.value});
    });
    if (error) {
        return Invalid{*error};
    }
    if (invalid) {
        return Invalid{*invalid};
    }
    return body;
}

}
