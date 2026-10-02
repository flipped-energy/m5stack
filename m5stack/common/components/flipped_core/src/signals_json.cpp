#include "flipped/core/signals_json.h"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#include "flipped/core/constants.h"
#include "flipped/core/time.h"

namespace flipped::core {

namespace {

class Writer {
public:
    std::string text;

    void beginObject() { open('{'); }
    void endObject() { close('}'); }
    void beginArray() { open('['); }
    void endArray() { close(']'); }

    void key(std::string_view name)
    {
        separate();
        quote(name);
        text += ':';
        afterKey_ = true;
    }

    void null()
    {
        separate();
        text += "null";
    }

    void boolean(bool value)
    {
        separate();
        text += value ? "true" : "false";
    }

    void number(double value)
    {
        separate();
        char buffer[40];
        std::snprintf(buffer, sizeof buffer, "%.15g", value);
        if (std::strtod(buffer, nullptr) != value) {
            std::snprintf(buffer, sizeof buffer, "%.17g", value);
        }
        text += buffer;
    }

    void integer(long long value)
    {
        separate();
        text += std::to_string(value);
    }

    void string(std::string_view value)
    {
        separate();
        quote(value);
    }

private:
    void open(char c)
    {
        separate();
        text += c;
        first_ = true;
    }

    void close(char c)
    {
        text += c;
        first_ = false;
    }

    void separate()
    {
        if (afterKey_) {
            afterKey_ = false;
            return;
        }
        if (!first_ && !text.empty()) {
            text += ',';
        }
        first_ = false;
    }

    void quote(std::string_view value)
    {
        text += '"';
        for (unsigned char c : value) {
            switch (c) {
            case '"':
                text += "\\\"";
                break;
            case '\\':
                text += "\\\\";
                break;
            case '\n':
                text += "\\n";
                break;
            case '\r':
                text += "\\r";
                break;
            case '\t':
                text += "\\t";
                break;
            default:
                if (c < 0x20) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof escaped, "\\u%04x", c);
                    text += escaped;
                } else {
                    text += static_cast<char>(c);
                }
            }
        }
        text += '"';
    }

    bool first_ = true;
    bool afterKey_ = false;
};

void optionalString(Writer &out, std::string_view name, const std::optional<std::string> &value)
{
    out.key(name);
    if (value) {
        out.string(*value);
    } else {
        out.null();
    }
}

void optionalInstant(Writer &out, std::string_view name, const std::optional<Instant> &value)
{
    out.key(name);
    if (value) {
        out.string(formatInstant(*value));
    } else {
        out.null();
    }
}

void optionalNumber(Writer &out, std::string_view name, const std::optional<double> &value)
{
    out.key(name);
    if (value) {
        out.number(*value);
    } else {
        out.null();
    }
}

void optionalBool(Writer &out, std::string_view name, const std::optional<bool> &value)
{
    out.key(name);
    if (value) {
        out.boolean(*value);
    } else {
        out.null();
    }
}

double cents(int64_t key)
{
    return static_cast<double>(key) / RATE_OUTPUT_DIVISOR;
}

void writeFault(Writer &out, const std::optional<Fault> &fault)
{
    out.key("status");
    out.string(fault ? "faulted" : "ok");
    out.key("fault");
    if (!fault) {
        out.null();
        return;
    }
    out.beginObject();
    out.key("code");
    out.string(fault->code);
    if (fault->httpStatus) {
        out.key("httpStatus");
        out.integer(*fault->httpStatus);
    }
    if (fault->body) {
        out.key("body");
        out.string(*fault->body);
    }
    if (fault->bodyBytes) {
        out.key("bodyBytes");
        out.integer(static_cast<long long>(*fault->bodyBytes));
    }
    if (fault->message) {
        out.key("message");
        out.string(*fault->message);
    }
    out.endObject();
}

void writeNulls(Writer &out, std::initializer_list<std::string_view> names)
{
    for (std::string_view name : names) {
        out.key(name);
        out.null();
    }
}

void writeSegment(Writer &out, const Segment &segment)
{
    out.key("band");
    out.string(bandName(segment.band));
    out.key("name");
    out.string(segment.name);
    out.key("rateCentsPerKwh");
    out.number(cents(segment.rateKey));
    optionalNumber(out, "kwhLimit", segment.kwhLimit);
    out.key("rateAfterLimitCentsPerKwh");
    if (segment.rateAfterLimitKey) {
        out.number(cents(*segment.rateAfterLimitKey));
    } else {
        out.null();
    }
    out.key("blocks");
    out.beginArray();
    for (const RateBlock &block : segment.blocks) {
        out.beginObject();
        out.key("fromKwh");
        out.number(block.fromKwh);
        optionalNumber(out, "toKwh", block.toKwh);
        out.key("rateCentsPerKwh");
        out.number(cents(block.rateKey));
        out.endObject();
    }
    out.endArray();
    out.key("wholesaleLinked");
    out.boolean(segment.wholesaleLinked);
    out.key("wholesaleCapCentsPerKwh");
    if (segment.wholesaleCapKey) {
        out.number(cents(*segment.wholesaleCapKey));
    } else {
        out.null();
    }
}

void writeAccount(Writer &out, const AccountGroup &group)
{
    out.beginObject();
    writeFault(out, group.fault);
    if (group.fault) {
        writeNulls(out, {"accountNumber", "accountState", "productName", "region", "timeZone", "tokenExpiresAt",
                         "tokenScope", "tokenExpiringSoon"});
    } else {
        optionalString(out, "accountNumber", group.accountNumber);
        optionalString(out, "accountState", group.accountState);
        optionalString(out, "productName", group.productName);
        optionalString(out, "region", group.region);
        optionalString(out, "timeZone", group.timeZone);
        optionalInstant(out, "tokenExpiresAt", group.tokenExpiresAt);
        optionalString(out, "tokenScope", group.tokenScope);
        optionalBool(out, "tokenExpiringSoon", group.tokenExpiringSoon);
    }
    out.endObject();
}

void writeTariff(Writer &out, const TariffGroup &group)
{
    out.beginObject();
    writeFault(out, group.fault);
    if (group.fault) {
        writeNulls(out, {"structure", "spotLinked", "peak", "offPeak", "period", "nextChange", "schedule"});
        out.endObject();
        return;
    }
    out.key("structure");
    out.string(structureName(group.structure));
    out.key("spotLinked");
    out.boolean(group.spotLinked);
    out.key("peak");
    out.boolean(group.peak);
    out.key("offPeak");
    out.boolean(group.offPeak);
    out.key("period");
    out.beginObject();
    writeSegment(out, group.period.segment);
    optionalInstant(out, "start", group.period.start);
    optionalInstant(out, "end", group.period.end);
    out.endObject();
    optionalInstant(out, "nextChange", group.nextChange);
    out.key("schedule");
    out.beginArray();
    for (const ScheduleEntry &entry : group.schedule) {
        out.beginObject();
        out.key("startMinute");
        out.integer(entry.startMinute);
        out.key("endMinute");
        out.integer(entry.endMinute);
        writeSegment(out, entry.segment);
        out.endObject();
    }
    out.endArray();
    out.endObject();
}

void writeForecast(Writer &out, std::string_view name, const std::optional<PriceForecast> &forecast)
{
    out.key(name);
    if (!forecast) {
        out.null();
        return;
    }
    out.beginObject();
    out.key("from");
    out.string(formatInstant(forecast->from));
    out.key("to");
    out.string(formatInstant(forecast->to));
    out.key("publishedAt");
    out.string(formatInstant(forecast->publishedAt));
    out.key("minCentsPerKwh");
    out.number(forecast->minCentsPerKwh);
    out.key("maxCentsPerKwh");
    out.number(forecast->maxCentsPerKwh);
    out.key("tier");
    out.string(forecast->tier);
    out.key("points");
    out.beginArray();
    for (const ForecastPoint &point : forecast->points) {
        out.beginObject();
        out.key("start");
        out.string(formatInstant(point.start));
        out.key("centsPerKwh");
        out.number(point.centsPerKwh);
        out.endObject();
    }
    out.endArray();
    out.endObject();
}

void writePrice(Writer &out, const PriceGroup &group)
{
    out.beginObject();
    writeFault(out, group.fault);
    if (group.fault) {
        writeNulls(out, {"centsPerKwh", "intervalStart", "tier", "priceHigh", "priceLow", "negative", "forecast"});
        out.endObject();
        return;
    }
    out.key("centsPerKwh");
    out.number(group.centsPerKwh);
    out.key("intervalStart");
    out.string(formatInstant(group.intervalStart));
    out.key("tier");
    out.string(group.tier);
    out.key("priceHigh");
    out.boolean(group.priceHigh);
    out.key("priceLow");
    out.boolean(group.priceLow);
    out.key("negative");
    out.boolean(group.negative);
    out.key("forecast");
    out.beginObject();
    writeForecast(out, "nextHour", group.nextHour);
    writeForecast(out, "ahead", group.ahead);
    out.endObject();
    out.endObject();
}

void writeEntries(Writer &out, std::string_view name, const std::vector<EnergyEntry> &entries)
{
    out.key(name);
    out.beginArray();
    for (const EnergyEntry &entry : entries) {
        out.beginObject();
        out.key("local");
        out.string(entry.local);
        out.key("start");
        out.string(formatInstant(entry.start));
        out.key("durationMinutes");
        out.integer(entry.durationMinutes);
        out.key("gridImportKwh");
        out.number(entry.gridImportKwh);
        out.key("controlledLoadKwh");
        out.number(entry.controlledLoadKwh);
        out.key("solarExportKwh");
        out.number(entry.solarExportKwh);
        optionalNumber(out, "costAud", entry.costAud);
        optionalNumber(out, "feedInCreditAud", entry.feedInCreditAud);
        out.endObject();
    }
    out.endArray();
}

void writeEnergy(Writer &out, const EnergyGroup &group)
{
    out.beginObject();
    writeFault(out, group.fault);
    if (group.fault) {
        writeNulls(out, {"nmi", "intervals", "days", "latestIntervalEnd"});
        out.endObject();
        return;
    }
    out.key("nmi");
    out.string(group.nmi);
    writeEntries(out, "intervals", group.intervals);
    writeEntries(out, "days", group.days);
    optionalInstant(out, "latestIntervalEnd", group.latestIntervalEnd);
    out.endObject();
}

}

const char *bandName(Band band)
{
    switch (band) {
    case Band::anytime:
        return "anytime";
    case Band::offPeak:
        return "offPeak";
    case Band::peak:
        return "peak";
    case Band::shoulder:
        return "shoulder";
    }
    std::abort();
}

const char *structureName(Structure structure)
{
    switch (structure) {
    case Structure::flat:
        return "flat";
    case Structure::timeOfUse:
        return "timeOfUse";
    }
    std::abort();
}

std::string signalsJson(const Signals &signals)
{
    Writer out;
    out.beginObject();
    out.key("account");
    writeAccount(out, signals.account);
    out.key("tariff");
    writeTariff(out, signals.tariff);
    out.key("price");
    writePrice(out, signals.price);
    out.key("energy");
    writeEnergy(out, signals.energy);
    optionalInstant(out, "nextEvaluation", signals.nextEvaluation);
    out.endObject();
    return out.text;
}

}
