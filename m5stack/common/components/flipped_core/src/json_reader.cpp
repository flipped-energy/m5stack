#include "json_reader.h"

#include <cstdlib>
#include <cstring>

namespace flipped::core::detail {

bool SourceReader::fill()
{
    if (ended_) {
        return false;
    }
    position_ = 0;
    length_ = source_.read(buffer_, sizeof buffer_);
    if (length_ == 0) {
        ended_ = true;
        return false;
    }
    return true;
}

int SourceReader::rawRead()
{
    if (position_ == length_ && !fill()) {
        return -1;
    }
    return buffer_[position_++];
}

int SourceReader::rawPeek()
{
    if (position_ == length_ && !fill()) {
        return -1;
    }
    return buffer_[position_];
}

bool isJsonNumber(const std::string &text)
{
    size_t i = 0;
    const auto digit = [&](size_t at) { return at < text.size() && text[at] >= '0' && text[at] <= '9'; };
    if (i < text.size() && text[i] == '-') {
        ++i;
    }
    if (!digit(i)) {
        return false;
    }
    if (text[i] == '0') {
        ++i;
    } else {
        while (digit(i)) {
            ++i;
        }
    }
    if (i < text.size() && text[i] == '.') {
        ++i;
        if (!digit(i)) {
            return false;
        }
        while (digit(i)) {
            ++i;
        }
    }
    if (i < text.size() && (text[i] == 'e' || text[i] == 'E')) {
        ++i;
        if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
            ++i;
        }
        if (!digit(i)) {
            return false;
        }
        while (digit(i)) {
            ++i;
        }
    }
    return i == text.size();
}

int SourceReader::read()
{
    if (pendingPosition_ < pending_.size()) {
        return static_cast<unsigned char>(pending_[pendingPosition_++]);
    }
    const int c = rawRead();
    if (c < 0) {
        return c;
    }
    if (inString_) {
        if (escaped_) {
            escaped_ = false;
        } else if (c == '\\') {
            escaped_ = true;
        } else if (c == '"') {
            inString_ = false;
        }
        return c;
    }
    if (c == '"') {
        inString_ = true;
        return c;
    }
    if (c != '-' && (c < '0' || c > '9')) {
        return c;
    }
    std::string number(1, static_cast<char>(c));
    for (;;) {
        const int next = rawPeek();
        if ((next >= '0' && next <= '9') || next == '.' || next == 'e' || next == 'E' || next == '+' ||
            next == '-') {
            number += static_cast<char>(rawRead());
        } else {
            break;
        }
    }
    if (!isJsonNumber(number) && !numberError_) {
        numberError_ = "invalid number " + number;
    }
    pending_.assign(1, NUMBER_MARK);
    pending_ += number;
    pending_ += '"';
    pendingPosition_ = 0;
    return '"';
}

size_t SourceReader::readBytes(char *buffer, size_t length)
{
    size_t copied = 0;
    while (copied < length) {
        const int c = read();
        if (c < 0) {
            break;
        }
        buffer[copied++] = static_cast<char>(c);
    }
    return copied;
}

int SourceReader::peek()
{
    if (pendingPosition_ < pending_.size()) {
        return static_cast<unsigned char>(pending_[pendingPosition_]);
    }
    return rawPeek();
}

void SourceReader::skipSpace()
{
    for (;;) {
        const int c = peek();
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r') {
            return;
        }
        read();
    }
}

std::optional<std::string> expectEnd(SourceReader &reader)
{
    reader.skipSpace();
    if (reader.peek() != -1) {
        return std::string("characters after the end of the JSON value");
    }
    return std::nullopt;
}

std::optional<std::string> readObject(SourceReader &reader, JsonDocument &document, const JsonDocument &filter)
{
    const DeserializationError error = deserializeJson(document, reader, DeserializationOption::Filter(filter));
    if (reader.numberError()) {
        return *reader.numberError();
    }
    if (error) {
        return std::string(error.c_str());
    }
    if (!document.is<JsonObjectConst>()) {
        return std::string("top-level value is not an object");
    }
    return expectEnd(reader);
}

bool isMarkedNumber(JsonVariantConst value)
{
    if (!value.is<JsonString>()) {
        return false;
    }
    const JsonString text = value.as<JsonString>();
    return text.size() > 1 && text.c_str()[0] == NUMBER_MARK;
}

double markedNumber(JsonVariantConst value)
{
    return std::strtod(value.as<JsonString>().c_str() + 1, nullptr);
}

Field<std::string> stringField(JsonVariantConst value)
{
    Field<std::string> field;
    if (value.isUnbound() || value.isNull()) {
        field.presence = Presence::null;
    } else if (!value.is<JsonString>() || isMarkedNumber(value)) {
        field.presence = Presence::wrongType;
    } else {
        const JsonString text = value.as<JsonString>();
        field.presence = Presence::present;
        field.value.assign(text.c_str(), text.size());
    }
    return field;
}

Field<double> numberField(JsonVariantConst value)
{
    Field<double> field;
    if (value.isUnbound() || value.isNull()) {
        field.presence = Presence::null;
    } else if (!isMarkedNumber(value)) {
        field.presence = Presence::wrongType;
    } else {
        field.presence = Presence::present;
        field.value = markedNumber(value);
    }
    return field;
}

Field<bool> boolField(JsonVariantConst value)
{
    Field<bool> field;
    if (value.isUnbound() || value.isNull()) {
        field.presence = Presence::null;
    } else if (!value.is<bool>()) {
        field.presence = Presence::wrongType;
    } else {
        field.presence = Presence::present;
        field.value = value.as<bool>();
    }
    return field;
}

}
