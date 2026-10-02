#include "flipped/core/parse.h"

#include "json_reader.h"

namespace flipped::core {

namespace {

Meter toMeter(JsonVariantConst value)
{
    Meter meter;
    if (!value.is<JsonObjectConst>()) {
        return meter;
    }
    JsonObjectConst object = value.as<JsonObjectConst>();
    meter.nmi = detail::stringField(object["nmi"]);
    meter.address = detail::stringField(object["address"]);
    return meter;
}

}

Parsed<MetersBody> parseMeters(ByteSource &source)
{
    JsonDocument filter;
    JsonObject meter = filter["meters"].add<JsonObject>();
    meter["nmi"] = true;
    meter["address"] = true;

    detail::SourceReader reader(source);
    JsonDocument document;
    if (std::optional<std::string> error = detail::readObject(reader, document, filter)) {
        return Invalid{*error};
    }
    MetersBody body;
    body.meters = detail::arrayField<Meter>(document["meters"], toMeter);
    return body;
}

}
