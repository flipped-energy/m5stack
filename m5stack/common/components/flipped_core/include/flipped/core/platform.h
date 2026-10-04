#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "flipped/core/byte_source.h"
#include "flipped/core/types.h"

namespace flipped::core {

enum class TimerId : uint8_t { startup, price, retryAfter, accountSync, usageSync, evaluation };
inline constexpr size_t TIMER_COUNT = 6;
const char *timerName(TimerId timer);

enum class Endpoint : uint8_t { account, meters, tokens, outlook, wait, usageHalfHourly, usageDaily };
inline constexpr size_t ENDPOINT_COUNT = 7;
const char *endpointPath(Endpoint endpoint);

struct QueryParameter {
    std::string name;
    std::string value;
};

struct Request {
    Endpoint endpoint = Endpoint::account;
    std::vector<QueryParameter> query;
    int64_t timeoutSeconds = 0;
    std::string nmi;
};

std::string requestTarget(const Request &request);

using RequestId = uint32_t;

enum class ResponseKind : uint8_t { body, noContent, http, network, invalid };

using ResponseBody = std::variant<std::monostate, AccountBody, MetersBody, TokensBody, OutlookBody, WaitBody, UsageBody>;

struct Response {
    ResponseKind kind = ResponseKind::network;
    int status = 0;
    ResponseBody body;
    std::string errorBody;
    size_t errorBodyBytes = 0;
    std::string message;
    std::optional<std::string> retryAfter;
    std::optional<std::string> location;
    std::optional<std::string> dailyLimitRemaining;
};

Response parsedResponse(const Request &request, int status, ByteSource &body);
Response errorResponse(int status, std::string_view keptBody, size_t bodyBytes);
Response networkResponse(std::string message);
std::string_view keptErrorBody(std::string_view body);
std::optional<std::string> gatewayErrorCode(std::string_view body);

enum class LogLevel : uint8_t { error, warning, info };

struct Platform {
    virtual ~Platform() = default;
    virtual std::optional<Instant> now() = 0;
    virtual void armAt(TimerId timer, Instant target, int64_t delaySeconds) = 0;
    virtual void cancel(TimerId timer) = 0;
    virtual void httpGet(RequestId id, const Request &request) = 0;
    virtual void log(LogLevel level, std::string_view line) = 0;
    virtual void storeAccountNumber(std::string_view accountNumber) = 0;
};

}
