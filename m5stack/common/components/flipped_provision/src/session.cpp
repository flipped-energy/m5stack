#include "flipped/provision/session.h"

#include <cstdio>
#include <cstdlib>
#include <utility>

namespace flipped::provision {

namespace {

constexpr char BASE64URL[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr std::string_view SCHEME = "http://";
constexpr std::string_view FRAGMENT = "#";

struct Fields {
    std::string_view a;
    std::string_view c;
};

int hexValue(char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

bool isHex(std::string_view text)
{
    for (const char ch : text) {
        if (hexValue(ch) < 0) {
            return false;
        }
    }
    return true;
}

uint8_t hexByte(std::string_view text, size_t index)
{
    return static_cast<uint8_t>(hexValue(text[2 * index]) << 4 | hexValue(text[2 * index + 1]));
}

std::optional<Fields> fieldsOf(std::string_view body)
{
    if (body.substr(0, 2) != "a=") {
        return std::nullopt;
    }
    const size_t separator = body.find('&');
    if (separator == std::string_view::npos || body.substr(separator, 3) != "&c=") {
        return std::nullopt;
    }
    Fields fields{body.substr(2, separator - 2), body.substr(separator + 3)};
    if (fields.a.size() != 2 * AUTH_BYTES || !isHex(fields.a) || fields.c.size() % 2 != 0 || !isHex(fields.c)) {
        return std::nullopt;
    }
    return fields;
}

std::string quoted(std::optional<std::string_view> value)
{
    if (!value) {
        return "missing";
    }
    return "'" + std::string(*value) + "'";
}

void appendBase64url(std::string &out, const uint8_t *data, size_t size)
{
    size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const uint32_t group = uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8 | data[i + 2];
        out += BASE64URL[group >> 18 & 0x3F];
        out += BASE64URL[group >> 12 & 0x3F];
        out += BASE64URL[group >> 6 & 0x3F];
        out += BASE64URL[group & 0x3F];
    }
    if (size - i == 1) {
        const uint32_t group = uint32_t(data[i]) << 16;
        out += BASE64URL[group >> 18 & 0x3F];
        out += BASE64URL[group >> 12 & 0x3F];
    }
    if (size - i == 2) {
        const uint32_t group = uint32_t(data[i]) << 16 | uint32_t(data[i + 1]) << 8;
        out += BASE64URL[group >> 18 & 0x3F];
        out += BASE64URL[group >> 12 & 0x3F];
        out += BASE64URL[group >> 6 & 0x3F];
    }
}

size_t base64urlSize(size_t bytes)
{
    return bytes / 3 * 4 + (bytes % 3 == 0 ? 0 : bytes % 3 + 1);
}

}

void zeroize(void *data, size_t size)
{
    volatile uint8_t *bytes = static_cast<volatile uint8_t *>(data);
    for (size_t i = 0; i < size; ++i) {
        bytes[i] = 0;
    }
}

std::string base64url(const uint8_t *data, size_t size)
{
    std::string out;
    out.reserve(base64urlSize(size));
    appendBase64url(out, data, size);
    return out;
}

const char *statusLine(int status)
{
    switch (status) {
    case 200:
        return "200 OK";
    case 400:
        return "400 Bad Request";
    case 403:
        return "403 Forbidden";
    case 404:
        return "404 Not Found";
    case 422:
        return "422 Unprocessable Content";
    }
    std::fprintf(stderr, "flipped_provision: no status line for %d\n", status);
    std::abort();
}

Session::Session(std::string_view ipv4, const Secret &secret)
    : address_(ipv4), origin_(std::string(SCHEME) + std::string(ipv4)), secret_(secret)
{
    url_.reserve(SCHEME.size() + ipv4.size() + std::string_view(PAGE_PATH).size() + FRAGMENT.size() +
                 base64urlSize(SECRET_BYTES));
    url_ += SCHEME;
    url_ += ipv4;
    url_ += PAGE_PATH;
    url_ += FRAGMENT;
    appendBase64url(url_, secret_.data(), secret_.size());
}

Session::~Session() { wipe(); }

void Session::wipe()
{
    zeroize(secret_.data(), secret_.size());
    zeroize(url_.data(), url_.size());
    url_.clear();
}

void Session::close()
{
    state_ = State::closed;
    wipe();
}

std::optional<Reply> Session::refuseRequest(const Request &request) const
{
    if (!request.host || *request.host != address_) {
        return Reply{400, "Host is " + quoted(request.host) + ", not '" + address_ + "'."};
    }
    if (request.origin && *request.origin != origin_) {
        return Reply{403, "Origin is " + quoted(request.origin) + ", not '" + origin_ + "'."};
    }
    if (state_ != State::open) {
        return Reply{403, CLOSED_TEXT};
    }
    if (request.contentLength > MAX_BODY_BYTES) {
        return Reply{400, "Body is " + std::to_string(request.contentLength) + " bytes, more than " +
                              std::to_string(MAX_BODY_BYTES) + "."};
    }
    const std::optional<Fields> fields = fieldsOf(request.body);
    if (!fields) {
        return Reply{400, "Body is not a=<16 hex digits>&c=<hex digits, two per byte>."};
    }
    const size_t bytes = fields->c.size() / 2;
    if (bytes < MIN_TOKEN_BYTES || bytes > PAD_BYTES) {
        return Reply{400, "Token is " + std::to_string(bytes) + " bytes, not " + std::to_string(MIN_TOKEN_BYTES) +
                              " to " + std::to_string(PAD_BYTES) + "."};
    }
    return std::nullopt;
}

Reply Session::post(const Request &request, const SetToken &setToken)
{
    if (std::optional<Reply> refusal = refuseRequest(request)) {
        return std::move(*refusal);
    }
    const Fields fields = *fieldsOf(request.body);
    uint8_t difference = 0;
    for (size_t i = 0; i < AUTH_BYTES; ++i) {
        difference |= static_cast<uint8_t>(hexByte(fields.a, i) ^ secret_[i]);
    }
    if (difference != 0) {
        ++refusals_;
        std::string text = "Wrong code, refusal " + std::to_string(refusals_) + " of " + std::to_string(MAX_REFUSALS) + ".";
        if (refusals_ >= MAX_REFUSALS) {
            close();
            text += " ";
            text += CLOSED_TEXT;
        }
        return Reply{403, std::move(text)};
    }
    std::string token(fields.c.size() / 2, '\0');
    for (size_t i = 0; i < token.size(); ++i) {
        token[i] = static_cast<char>(hexByte(fields.c, i) ^ secret_[AUTH_BYTES + i]);
    }
    state_ = State::consumed;
    wipe();
    const core::ConfigResult result = setToken(token);
    zeroize(token.data(), token.size());
    if (result.accepted) {
        return Reply{200, SAVED_TEXT};
    }
    return Reply{422, result.text};
}

}
