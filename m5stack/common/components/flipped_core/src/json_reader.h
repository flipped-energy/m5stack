#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <ArduinoJson.h>

#include "flipped/core/byte_source.h"
#include "flipped/core/types.h"

namespace flipped::core::detail {

inline constexpr char NUMBER_MARK = 0x01;

class SourceReader {
public:
    explicit SourceReader(ByteSource &source) : source_(source) {}

    int read();
    size_t readBytes(char *buffer, size_t length);
    int peek();
    void skipSpace();
    const std::optional<std::string> &numberError() const { return numberError_; }

private:
    bool fill();
    int rawRead();
    int rawPeek();

    ByteSource &source_;
    uint8_t buffer_[64];
    size_t position_ = 0;
    size_t length_ = 0;
    bool ended_ = false;
    bool inString_ = false;
    bool escaped_ = false;
    std::string pending_;
    size_t pendingPosition_ = 0;
    std::optional<std::string> numberError_;
};

bool isJsonNumber(const std::string &text);

std::optional<std::string> readObject(SourceReader &reader, JsonDocument &document, const JsonDocument &filter);
std::optional<std::string> expectEnd(SourceReader &reader);

template <typename OnElement>
std::optional<std::string> readArray(SourceReader &reader, const JsonDocument &filter, OnElement onElement)
{
    reader.skipSpace();
    if (reader.read() != '[') {
        return std::string("top-level value is not an array");
    }
    reader.skipSpace();
    if (reader.peek() == ']') {
        reader.read();
        return expectEnd(reader);
    }
    for (;;) {
        JsonDocument element;
        const DeserializationError error = deserializeJson(element, reader, DeserializationOption::Filter(filter));
        if (reader.numberError()) {
            return *reader.numberError();
        }
        if (error) {
            return std::string(error.c_str());
        }
        if (!element.is<JsonObjectConst>()) {
            return std::string("array element is not an object");
        }
        onElement(element.as<JsonObjectConst>());
        reader.skipSpace();
        const int next = reader.read();
        if (next == ']') {
            return expectEnd(reader);
        }
        if (next != ',') {
            return std::string("expected ',' or ']' after an array element");
        }
    }
}

bool isMarkedNumber(JsonVariantConst value);
double markedNumber(JsonVariantConst value);
Field<std::string> stringField(JsonVariantConst value);
Field<double> numberField(JsonVariantConst value);
Field<bool> boolField(JsonVariantConst value);

template <typename T, typename Convert>
Field<T> objectField(JsonVariantConst value, Convert convert)
{
    Field<T> field;
    if (value.isUnbound() || value.isNull()) {
        field.presence = Presence::null;
    } else if (!value.is<JsonObjectConst>()) {
        field.presence = Presence::wrongType;
    } else {
        field.presence = Presence::present;
        field.value = convert(value.as<JsonObjectConst>());
    }
    return field;
}

template <typename T, typename Convert>
Field<std::vector<T>> arrayField(JsonVariantConst value, Convert convert)
{
    Field<std::vector<T>> field;
    if (value.isUnbound() || value.isNull()) {
        field.presence = Presence::null;
    } else if (!value.is<JsonArrayConst>()) {
        field.presence = Presence::wrongType;
    } else {
        field.presence = Presence::present;
        for (JsonVariantConst element : value.as<JsonArrayConst>()) {
            field.value.push_back(convert(element));
        }
    }
    return field;
}

}
