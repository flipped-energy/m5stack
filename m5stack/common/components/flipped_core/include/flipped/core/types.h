#pragma once

#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <string>
#include <deque>
#include <vector>

namespace flipped::core {

using Instant = int64_t;
static_assert(sizeof(time_t) == 8, "time_t must be 64-bit");

enum class Presence : uint8_t { null, wrongType, present };

template <typename T>
struct Field {
    Presence presence = Presence::null;
    T value{};

    bool present() const { return presence == Presence::present; }
    bool absent() const { return presence == Presence::null; }
};

struct BillingUnit {
    Field<std::string> billingUnitId;
    Field<std::string> name;
    Field<std::string> billingUnitType;
    Field<double> chargePerKwh;
    Field<double> chargePerKwhIncludingGst;
    Field<double> timeOfDayStartMinutes;
    Field<double> timeOfDayEndMinutes;
    Field<double> kwhStart;
    Field<double> kwhEnd;
    Field<double> capPerKwhIncludingGst;
};

struct Plan {
    Field<std::string> start;
    Field<std::string> end;
    Field<std::vector<BillingUnit>> billingUnits;
};

struct Product {
    Field<std::string> gridType;
    Field<std::string> timeZone;
    Field<Plan> currentPlan;
    Field<Plan> upcomingPlan;
};

struct Account {
    Field<std::string> accountNumber;
    Field<std::string> siteAddress;
    Field<std::string> accountState;
    Field<std::string> productName;
    Field<Product> product;
};

struct AccountBody {
    Field<std::vector<Account>> accounts;
};

struct Meter {
    Field<std::string> nmi;
    Field<std::string> address;
};

struct MetersBody {
    Field<std::vector<Meter>> meters;
};

struct Token {
    Field<std::string> tokenPreview;
    Field<std::string> scope;
    Field<std::string> expiresAt;
    Field<std::string> revokedAt;
};

struct TokensBody {
    Field<std::vector<Token>> tokens;
};

struct PricePoint {
    Field<std::string> time;
    Field<double> averageCentsPerKwh;
};

struct Forecast {
    Field<std::string> from;
    Field<std::string> to;
    Field<std::string> publishedAt;
    Field<double> minCentsPerKwh;
    Field<double> maxCentsPerKwh;
    Field<std::vector<PricePoint>> points;
};

struct Assessment {
    Field<std::string> tier;
};

struct OutlookBody {
    Field<PricePoint> now;
    Field<Assessment> nowAssessment;
    Field<Forecast> nextHour;
    Field<Assessment> nextHourPeak;
    Field<Forecast> ahead;
    Field<Assessment> aheadPeak;
};

struct WaitPoint {
    Instant start = 0;
    std::string time;
    double averageCentsPerKwh = 0;
};

struct WaitBody {
    std::vector<WaitPoint> points;
};

enum class CostState : uint8_t { noRows, value, null };

struct KeyCost {
    CostState state = CostState::noRows;
    double value = 0;
};

struct UsageBucket {
    std::string time;
    double gridImportKwh = 0;
    double controlledLoadKwh = 0;
    double solarExportKwh = 0;
    KeyCost exportGeneral;
    KeyCost exportControlledLoad;
    KeyCost importGeneral;
};

struct UsageBody {
    std::string nmi;
    std::deque<UsageBucket> buckets;
    std::optional<std::string> invalid;
};

enum class ErrorKind : uint8_t { http, network, invalid };

struct SnapshotError {
    ErrorKind kind = ErrorKind::invalid;
    int httpStatus = 0;
    std::string body;
    std::optional<size_t> bodyBytes;
    std::string message;
};

template <typename Body>
struct Snapshot {
    std::optional<Body> body;
    std::optional<Instant> fetchedAt;
    std::optional<SnapshotError> error;
};

struct Fault {
    std::string code;
    std::optional<int> httpStatus;
    std::optional<std::string> body;
    std::optional<size_t> bodyBytes;
    std::optional<std::string> message;
};

struct Config {
    std::optional<std::string> accountNumber;
    std::optional<std::string> nmi;
    std::optional<std::string> tokenPreview;
    std::optional<double> priceHighThresholdCentsPerKwh;
    std::optional<double> priceLowThresholdCentsPerKwh;
};

}
