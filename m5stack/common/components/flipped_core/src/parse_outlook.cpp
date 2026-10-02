#include "flipped/core/parse.h"

#include "json_reader.h"

namespace flipped::core {

namespace {

void pointFilter(JsonObject point)
{
    point["time"] = true;
    point["averageCentsPerKwh"] = true;
}

void forecastFilter(JsonObject forecast)
{
    forecast["from"] = true;
    forecast["to"] = true;
    forecast["publishedAt"] = true;
    forecast["minCentsPerKwh"] = true;
    forecast["maxCentsPerKwh"] = true;
    pointFilter(forecast["points"].add<JsonObject>());
}

PricePoint toPoint(JsonObjectConst object)
{
    PricePoint point;
    point.time = detail::stringField(object["time"]);
    point.averageCentsPerKwh = detail::numberField(object["averageCentsPerKwh"]);
    return point;
}

PricePoint toPointElement(JsonVariantConst value)
{
    if (!value.is<JsonObjectConst>()) {
        return PricePoint{};
    }
    return toPoint(value.as<JsonObjectConst>());
}

Forecast toForecast(JsonObjectConst object)
{
    Forecast forecast;
    forecast.from = detail::stringField(object["from"]);
    forecast.to = detail::stringField(object["to"]);
    forecast.publishedAt = detail::stringField(object["publishedAt"]);
    forecast.minCentsPerKwh = detail::numberField(object["minCentsPerKwh"]);
    forecast.maxCentsPerKwh = detail::numberField(object["maxCentsPerKwh"]);
    forecast.points = detail::arrayField<PricePoint>(object["points"], toPointElement);
    return forecast;
}

Assessment toAssessment(JsonObjectConst object)
{
    Assessment assessment;
    assessment.tier = detail::stringField(object["tier"]);
    return assessment;
}

}

Parsed<OutlookBody> parseOutlook(ByteSource &source)
{
    JsonDocument filter;
    pointFilter(filter["now"].to<JsonObject>());
    filter["nowAssessment"]["tier"] = true;
    forecastFilter(filter["nextHour"].to<JsonObject>());
    filter["nextHourPeak"]["tier"] = true;
    forecastFilter(filter["ahead"].to<JsonObject>());
    filter["aheadPeak"]["tier"] = true;

    detail::SourceReader reader(source);
    JsonDocument document;
    if (std::optional<std::string> error = detail::readObject(reader, document, filter)) {
        return Invalid{*error};
    }
    OutlookBody body;
    body.now = detail::objectField<PricePoint>(document["now"], toPoint);
    body.nowAssessment = detail::objectField<Assessment>(document["nowAssessment"], toAssessment);
    body.nextHour = detail::objectField<Forecast>(document["nextHour"], toForecast);
    body.nextHourPeak = detail::objectField<Assessment>(document["nextHourPeak"], toAssessment);
    body.ahead = detail::objectField<Forecast>(document["ahead"], toForecast);
    body.aheadPeak = detail::objectField<Assessment>(document["aheadPeak"], toAssessment);
    return body;
}

}
