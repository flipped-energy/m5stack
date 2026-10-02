#include "flipped/core/platform.h"

#include <cstdlib>

#include <ArduinoJson.h>

#include "flipped/core/constants.h"
#include "flipped/core/parse.h"

namespace flipped::core {

namespace {

template <typename Body>
Response fromParsed(int status, Parsed<Body> parsed)
{
    Response response;
    response.status = status;
    if (const Invalid *invalid = std::get_if<Invalid>(&parsed)) {
        response.kind = ResponseKind::invalid;
        response.message = invalid->message;
        return response;
    }
    response.kind = ResponseKind::body;
    response.body = std::move(std::get<Body>(parsed));
    return response;
}

bool unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_' || c == '~';
}

void appendEncoded(std::string &target, std::string_view text)
{
    static constexpr char hex[] = "0123456789ABCDEF";
    for (const char c : text) {
        if (unreserved(c)) {
            target.push_back(c);
            continue;
        }
        const auto byte = static_cast<unsigned char>(c);
        target.push_back('%');
        target.push_back(hex[byte >> 4]);
        target.push_back(hex[byte & 0x0F]);
    }
}

}

const char *timerName(TimerId timer)
{
    switch (timer) {
    case TimerId::startup:
        return "startup";
    case TimerId::price:
        return "price";
    case TimerId::retryAfter:
        return "retryAfter";
    case TimerId::accountSync:
        return "accountSync";
    case TimerId::usageSync:
        return "usageSync";
    case TimerId::evaluation:
        return "evaluation";
    }
    std::abort();
}

const char *endpointPath(Endpoint endpoint)
{
    switch (endpoint) {
    case Endpoint::account:
        return "/api/MyAccount/GetAccountData";
    case Endpoint::meters:
        return "/api/Billing/meters";
    case Endpoint::tokens:
        return "/tokens";
    case Endpoint::outlook:
        return "/api/Live/nempricing/outlook";
    case Endpoint::wait:
        return "/api/Live/nempricing/wait";
    case Endpoint::usageHalfHourly:
        return "/api/Usage/usage/projectreads/halfhourly";
    case Endpoint::usageDaily:
        return "/api/Usage/usage/projectreads/daily";
    }
    std::abort();
}

std::string requestTarget(const Request &request)
{
    std::string target = endpointPath(request.endpoint);
    char separator = '?';
    for (const QueryParameter &parameter : request.query) {
        target.push_back(separator);
        appendEncoded(target, parameter.name);
        target.push_back('=');
        appendEncoded(target, parameter.value);
        separator = '&';
    }
    return target;
}

Response parsedResponse(const Request &request, int status, ByteSource &body)
{
    if (status == 204) {
        Response response;
        response.status = status;
        if (request.endpoint == Endpoint::wait) {
            response.kind = ResponseKind::noContent;
            return response;
        }
        response.kind = ResponseKind::invalid;
        response.message = std::string(endpointPath(request.endpoint)) + " answered 204 with no body";
        return response;
    }
    switch (request.endpoint) {
    case Endpoint::account:
        return fromParsed(status, parseAccount(body));
    case Endpoint::meters:
        return fromParsed(status, parseMeters(body));
    case Endpoint::tokens:
        return fromParsed(status, parseTokens(body));
    case Endpoint::outlook:
        return fromParsed(status, parseOutlook(body));
    case Endpoint::wait:
        return fromParsed(status, parseWait(body));
    case Endpoint::usageHalfHourly:
    case Endpoint::usageDaily:
        return fromParsed(status, parseUsage(body, request.nmi));
    }
    std::abort();
}

Response errorResponse(int status, std::string_view keptBody, size_t bodyBytes)
{
    Response response;
    response.kind = ResponseKind::http;
    response.status = status;
    response.errorBody = std::string(keptBody);
    response.errorBodyBytes = bodyBytes;
    return response;
}

Response networkResponse(std::string message)
{
    Response response;
    response.kind = ResponseKind::network;
    response.message = std::move(message);
    return response;
}

std::string_view keptErrorBody(std::string_view body)
{
    if (body.size() <= FIRMWARE_ERROR_BODY_MAX_BYTES) {
        return body;
    }
    size_t length = FIRMWARE_ERROR_BODY_MAX_BYTES;
    while (length > 0 && (static_cast<unsigned char>(body[length]) & 0xC0) == 0x80) {
        --length;
    }
    return body.substr(0, length);
}

std::optional<std::string> gatewayErrorCode(std::string_view body)
{
    JsonDocument filter;
    filter["error"] = true;
    JsonDocument document;
    if (deserializeJson(document, body.data(), body.size(), DeserializationOption::Filter(filter))) {
        return std::nullopt;
    }
    if (!document.is<JsonObjectConst>()) {
        return std::nullopt;
    }
    JsonVariantConst error = document["error"];
    if (!error.is<const char *>()) {
        return std::nullopt;
    }
    return std::string(error.as<const char *>());
}

}
