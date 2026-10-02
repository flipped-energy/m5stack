#pragma once

#include <cstdint>

namespace flipped::matter {

inline constexpr uint16_t ROOT_ENDPOINT = 0;
inline constexpr uint16_t AGGREGATOR_ENDPOINT = 1;
inline constexpr uint16_t UTILITY_METER_ENDPOINT = 2;
inline constexpr uint16_t ELECTRICAL_METER_ENDPOINT = 3;
inline constexpr const char *UTILITY_METER_LABEL = "Flipped Energy";
inline constexpr const char *GRID_ENERGY_LABEL = "Grid Energy";

inline constexpr uint32_t FLIPPED_ENERGY_CLUSTER_ID = 0xFFF1FC01;
inline constexpr uint16_t FLIPPED_ENERGY_CLUSTER_REVISION = 1;

inline constexpr uint32_t COMMISSIONING_WINDOW_S = 900;
inline constexpr uint32_t SPAKE2P_ITERATIONS = 1000;

inline constexpr const char *NVS_NAMESPACE = "flipped_mtr";
inline constexpr const char *NVS_SWITCHES = "switches";
inline constexpr const char *NVS_PASSCODE = "passcode";
inline constexpr const char *NVS_DISCRIMINATOR = "discrim";
inline constexpr const char *NVS_SALT = "salt";
inline constexpr const char *NVS_GRID_ENERGY_ENDPOINT = "grid_ep";

inline constexpr const char *VENDOR_NAME = "Flipped Energy";

inline constexpr uint32_t EVE_HISTORY_CLUSTER_ID = 0x130AFC01;
inline constexpr uint16_t EVE_HISTORY_CLUSTER_REVISION = 1;
inline constexpr const char *EVE_HISTORY_LABEL = "Energy History";
inline constexpr const char *NVS_EVE_ENDPOINT = "eve_ep";

namespace eve {
inline constexpr uint32_t HISTORY_STATUS = 0x130A0002;
inline constexpr uint32_t HISTORY_ENTRIES = 0x130A0003;
inline constexpr uint32_t HISTORY_REQUEST = 0x130A0004;
inline constexpr uint32_t HISTORY_SET_TIME = 0x130A0005;
inline constexpr uint32_t POWER_CONSUMPTION = 0x130A000A;
inline constexpr uint32_t TOTAL_CONSUMPTION = 0x130A000B;
inline constexpr uint16_t WRITE_MAX_BYTES = 254;
}

namespace flipped_energy {
inline constexpr uint32_t TARIFF_FAULT = 0x0000;
inline constexpr uint32_t STRUCTURE = 0x0004;
inline constexpr uint32_t SPOT_LINKED = 0x0005;
inline constexpr uint32_t RATE_PERIOD = 0x0006;
inline constexpr uint32_t RATE_PERIOD_NAME = 0x0007;
inline constexpr uint32_t CURRENT_RATE = 0x0008;
inline constexpr uint32_t RATE_ALLOWANCE_KWH = 0x0009;
inline constexpr uint32_t RATE_AFTER_ALLOWANCE = 0x000A;
inline constexpr uint32_t NEXT_RATE_CHANGE = 0x000B;
inline constexpr uint32_t WHOLESALE_LINKED_RATE = 0x000C;
inline constexpr uint32_t FIXED_RATE_COMPONENT = 0x000D;
inline constexpr uint32_t WHOLESALE_RATE_CAP = 0x000E;
inline constexpr uint32_t PRICE_FAULT = 0x0010;
inline constexpr uint32_t WHOLESALE_PRICE = 0x0014;
inline constexpr uint32_t WHOLESALE_INTERVAL_START = 0x0015;
inline constexpr uint32_t WHOLESALE_PRICE_LEVEL = 0x0016;
inline constexpr uint32_t WHOLESALE_PRICE_NEGATIVE = 0x0017;
inline constexpr uint32_t ENERGY_FAULT = 0x0020;
inline constexpr uint32_t LATEST_INTERVAL_END = 0x0024;
inline constexpr uint32_t LAST_DAY_START = 0x0025;
inline constexpr uint32_t LAST_DAY_GRID_IMPORT = 0x0026;
inline constexpr uint32_t LAST_DAY_CONTROLLED_LOAD = 0x0027;
inline constexpr uint32_t LAST_DAY_SOLAR_EXPORT = 0x0028;
inline constexpr uint32_t LAST_DAY_USAGE_COST = 0x0029;
inline constexpr uint32_t LAST_DAY_FEED_IN_CREDIT = 0x002A;
inline constexpr uint32_t ACCOUNT_FAULT = 0x0030;
inline constexpr uint32_t TOKEN_EXPIRES_AT = 0x0034;
inline constexpr uint32_t TOKEN_EXPIRING_SOON = 0x0035;
inline constexpr uint32_t DAILY_LIMIT_REMAINING = 0x0036;

inline constexpr uint32_t FAULT_CODE = 0;
inline constexpr uint32_t FAULT_HTTP_STATUS = 1;
inline constexpr uint32_t FAULT_TEXT = 2;
inline constexpr uint32_t FAULT_BODY_BYTES = 3;
}

}
