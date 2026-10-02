#pragma once

#include <cstddef>
#include <cstdint>

namespace flipped::core {

inline constexpr const char *BASE_URL = "https://mcp-api.flipped.energy/developer/v1";
inline constexpr int64_t WAIT_TIMEOUT_S = 55;
inline constexpr int64_t WAIT_HTTP_TIMEOUT_S = 70;
inline constexpr int64_t HTTP_TIMEOUT_S = 60;
inline constexpr int64_t DISPATCH_INTERVAL_S = 300;
inline constexpr int WAIT_HOLDS_PER_INTERVAL = 3;
inline constexpr int64_t PRICE_STALE_AFTER_S = 900;
inline constexpr int64_t ACCOUNT_MAX_AGE_S = 172800;
inline constexpr int64_t SCAN_LIMIT_MIN = 1500;
inline constexpr int64_t PLAN_CHANGE_HORIZON_DAYS = 31;
inline constexpr int64_t KWH_UNLIMITED = 999999999;
inline constexpr int64_t TIMER_MAX_AHEAD_S = 86400;
inline constexpr int USAGE_LOOKBACK_DAYS_MIN = 2;
inline constexpr int USAGE_LOOKBACK_DAYS_MAX = 7;
inline constexpr int ACCOUNT_SYNC_MINUTE_OF_DAY = 1;
inline constexpr int USAGE_SYNC_MINUTE_OF_DAY = 12 * 60 + 1;
inline constexpr int64_t TOKEN_EXPIRY_WARNING_DAYS = 14;
inline constexpr int64_t USAGE_REFRESH_MIN_INTERVAL_S = 60;
inline constexpr size_t FIRMWARE_ERROR_BODY_MAX_BYTES = 2048;
inline constexpr double RATE_KEY_SCALE = 1e9;
inline constexpr double RATE_OUTPUT_DIVISOR = 1e7;

}
