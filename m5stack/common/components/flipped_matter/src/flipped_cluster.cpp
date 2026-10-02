#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>

#include "flipped/matter/ids.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

namespace Ids = flipped_energy;

const char *TAG = "flipped_cluster";
constexpr uint16_t TEXT_MAX_BYTES = static_cast<uint16_t>(core::FLIPPED_CLUSTER_TEXT_MAX_BYTES);
constexpr uint16_t CODE_MAX_BYTES = static_cast<uint16_t>(core::FLIPPED_CLUSTER_CODE_MAX_BYTES);
constexpr uint16_t NAME_MAX_BYTES = static_cast<uint16_t>(core::FLIPPED_CLUSTER_NAME_MAX_BYTES);

esp_matter::cluster_t *cluster = nullptr;

esp_matter_attr_val_t nullText(esp_matter_val_type_t type)
{
    esp_matter_attr_val_t value = esp_matter_invalid(nullptr);
    value.type = type;
    const uint16_t nullLength = type == ESP_MATTER_VAL_TYPE_CHAR_STRING ? UINT8_MAX : UINT16_MAX;
    value.val.a.b = nullptr;
    value.val.a.s = nullLength;
    value.val.a.t = nullLength;
    return value;
}

template <typename T>
nullable<T> maybe(const std::optional<T> &value)
{
    return value ? nullable<T>(*value) : nullable<T>();
}

void create(uint32_t id, esp_matter_attr_val_t initial, uint16_t maxBytes)
{
    if (esp_matter::attribute::create(cluster, id, esp_matter::ATTRIBUTE_FLAG_NULLABLE, initial, maxBytes) == nullptr) {
        ESP_LOGE(TAG, "attribute::create 0x%04" PRIX32 " of the Flipped Energy cluster returned null", id);
        abort();
    }
}

void createFault(uint32_t base)
{
    create(base + Ids::FAULT_CODE, nullText(ESP_MATTER_VAL_TYPE_CHAR_STRING), CODE_MAX_BYTES);
    create(base + Ids::FAULT_HTTP_STATUS, esp_matter_nullable_uint16(nullable<uint16_t>()), 0);
    create(base + Ids::FAULT_TEXT, nullText(ESP_MATTER_VAL_TYPE_LONG_CHAR_STRING), TEXT_MAX_BYTES);
    create(base + Ids::FAULT_BODY_BYTES, esp_matter_nullable_uint32(nullable<uint32_t>()), 0);
}

void put(uint32_t id, esp_matter_attr_val_t value)
{
    const esp_err_t err = esp_matter::attribute::report(ROOT_ENDPOINT, FLIPPED_ENERGY_CLUSTER_ID, id, &value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "attribute::report 0x%04" PRIX32 " of the Flipped Energy cluster: %s", id, esp_err_to_name(err));
        abort();
    }
}

void putText(uint32_t id, std::optional<std::string> text, esp_matter_val_type_t type, uint16_t maxBytes)
{
    if (!text) {
        put(id, nullText(type));
        return;
    }
    if (text->empty() || text->size() > maxBytes) {
        ESP_LOGE(TAG, "attribute 0x%04" PRIX32 " text of %u bytes does not fit 1 to %u bytes", id,
                 static_cast<unsigned>(text->size()), maxBytes);
        abort();
    }
    const uint16_t size = static_cast<uint16_t>(text->size());
    put(id, type == ESP_MATTER_VAL_TYPE_CHAR_STRING ? esp_matter_char_str(text->data(), size)
                                                    : esp_matter_long_char_str(text->data(), size));
}

void putFault(uint32_t base, const core::FaultValues &fault)
{
    putText(base + Ids::FAULT_CODE, fault.code, ESP_MATTER_VAL_TYPE_CHAR_STRING, CODE_MAX_BYTES);
    put(base + Ids::FAULT_HTTP_STATUS, esp_matter_nullable_uint16(maybe(fault.httpStatus)));
    putText(base + Ids::FAULT_TEXT, fault.text, ESP_MATTER_VAL_TYPE_LONG_CHAR_STRING, TEXT_MAX_BYTES);
    put(base + Ids::FAULT_BODY_BYTES, esp_matter_nullable_uint32(maybe(fault.bodyBytes)));
}

}

void createFlippedEnergyCluster(esp_matter::endpoint_t *root)
{
    require(cluster == nullptr, "the Flipped Energy cluster is created twice");
    cluster = esp_matter::cluster::create(root, FLIPPED_ENERGY_CLUSTER_ID, esp_matter::CLUSTER_FLAG_SERVER);
    require(cluster != nullptr, "cluster::create of the Flipped Energy cluster 0xFFF1FC01 returned null");
    require(esp_matter::cluster::global::attribute::create_feature_map(cluster, 0) != nullptr,
            "create_feature_map of the Flipped Energy cluster returned null");
    require(esp_matter::cluster::global::attribute::create_cluster_revision(cluster, FLIPPED_ENERGY_CLUSTER_REVISION) !=
                nullptr,
            "create_cluster_revision of the Flipped Energy cluster returned null");

    createFault(Ids::TARIFF_FAULT);
    create(Ids::STRUCTURE, esp_matter_nullable_enum8(nullable<uint8_t>()), 0);
    create(Ids::SPOT_LINKED, esp_matter_nullable_bool(nullable<bool>()), 0);
    create(Ids::RATE_PERIOD, esp_matter_nullable_enum8(nullable<uint8_t>()), 0);
    create(Ids::RATE_PERIOD_NAME, nullText(ESP_MATTER_VAL_TYPE_CHAR_STRING), NAME_MAX_BYTES);
    create(Ids::CURRENT_RATE, esp_matter_nullable_int64(nullable<int64_t>()), 0);
    create(Ids::RATE_ALLOWANCE_KWH, esp_matter_nullable_uint32(nullable<uint32_t>()), 0);
    create(Ids::RATE_AFTER_ALLOWANCE, esp_matter_nullable_int64(nullable<int64_t>()), 0);
    create(Ids::NEXT_RATE_CHANGE, esp_matter_nullable_uint32(nullable<uint32_t>()), 0);
    create(Ids::WHOLESALE_LINKED_RATE, esp_matter_nullable_bool(nullable<bool>()), 0);
    create(Ids::FIXED_RATE_COMPONENT, esp_matter_nullable_int64(nullable<int64_t>()), 0);
    create(Ids::WHOLESALE_RATE_CAP, esp_matter_nullable_int64(nullable<int64_t>()), 0);

    createFault(Ids::PRICE_FAULT);
    create(Ids::WHOLESALE_PRICE, esp_matter_nullable_int32(nullable<int32_t>()), 0);
    create(Ids::WHOLESALE_INTERVAL_START, esp_matter_nullable_uint32(nullable<uint32_t>()), 0);
    create(Ids::WHOLESALE_PRICE_LEVEL, esp_matter_nullable_enum8(nullable<uint8_t>()), 0);
    create(Ids::WHOLESALE_PRICE_NEGATIVE, esp_matter_nullable_bool(nullable<bool>()), 0);

    createFault(Ids::ENERGY_FAULT);
    create(Ids::LATEST_INTERVAL_END, esp_matter_nullable_uint32(nullable<uint32_t>()), 0);
    create(Ids::LAST_DAY_START, esp_matter_nullable_uint32(nullable<uint32_t>()), 0);
    create(Ids::LAST_DAY_GRID_IMPORT, esp_matter_nullable_int64(nullable<int64_t>()), 0);
    create(Ids::LAST_DAY_CONTROLLED_LOAD, esp_matter_nullable_int64(nullable<int64_t>()), 0);
    create(Ids::LAST_DAY_SOLAR_EXPORT, esp_matter_nullable_int64(nullable<int64_t>()), 0);
    create(Ids::LAST_DAY_USAGE_COST, esp_matter_nullable_int32(nullable<int32_t>()), 0);
    create(Ids::LAST_DAY_FEED_IN_CREDIT, esp_matter_nullable_int32(nullable<int32_t>()), 0);

    createFault(Ids::ACCOUNT_FAULT);
    create(Ids::TOKEN_EXPIRES_AT, esp_matter_nullable_uint32(nullable<uint32_t>()), 0);
    create(Ids::TOKEN_EXPIRING_SOON, esp_matter_nullable_bool(nullable<bool>()), 0);
    create(Ids::DAILY_LIMIT_REMAINING, esp_matter_nullable_uint16(nullable<uint16_t>()), 0);
}

void applyFlippedEnergy(const core::FlippedClusterValues &values)
{
    putFault(Ids::TARIFF_FAULT, values.tariffFault);
    put(Ids::STRUCTURE, esp_matter_nullable_enum8(maybe(values.structure)));
    put(Ids::SPOT_LINKED, esp_matter_nullable_bool(maybe(values.spotLinked)));
    put(Ids::RATE_PERIOD, esp_matter_nullable_enum8(maybe(values.ratePeriod)));
    putText(Ids::RATE_PERIOD_NAME, values.ratePeriodName, ESP_MATTER_VAL_TYPE_CHAR_STRING,
            NAME_MAX_BYTES);
    put(Ids::CURRENT_RATE, esp_matter_nullable_int64(maybe(values.currentRate)));
    put(Ids::RATE_ALLOWANCE_KWH, esp_matter_nullable_uint32(maybe(values.rateAllowanceKwh)));
    put(Ids::RATE_AFTER_ALLOWANCE, esp_matter_nullable_int64(maybe(values.rateAfterAllowance)));
    put(Ids::NEXT_RATE_CHANGE, esp_matter_nullable_uint32(maybe(values.nextRateChange)));
    put(Ids::WHOLESALE_LINKED_RATE, esp_matter_nullable_bool(maybe(values.wholesaleLinkedRate)));
    put(Ids::FIXED_RATE_COMPONENT, esp_matter_nullable_int64(maybe(values.fixedRateComponent)));
    put(Ids::WHOLESALE_RATE_CAP, esp_matter_nullable_int64(maybe(values.wholesaleRateCap)));

    putFault(Ids::PRICE_FAULT, values.priceFault);
    put(Ids::WHOLESALE_PRICE, esp_matter_nullable_int32(maybe(values.wholesalePrice)));
    put(Ids::WHOLESALE_INTERVAL_START, esp_matter_nullable_uint32(maybe(values.wholesaleIntervalStart)));
    put(Ids::WHOLESALE_PRICE_LEVEL, esp_matter_nullable_enum8(maybe(values.wholesalePriceLevel)));
    put(Ids::WHOLESALE_PRICE_NEGATIVE, esp_matter_nullable_bool(maybe(values.wholesalePriceNegative)));

    putFault(Ids::ENERGY_FAULT, values.energyFault);
    put(Ids::LATEST_INTERVAL_END, esp_matter_nullable_uint32(maybe(values.latestIntervalEnd)));
    put(Ids::LAST_DAY_START, esp_matter_nullable_uint32(maybe(values.lastDayStart)));
    put(Ids::LAST_DAY_GRID_IMPORT, esp_matter_nullable_int64(maybe(values.lastDayGridImportMwh)));
    put(Ids::LAST_DAY_CONTROLLED_LOAD, esp_matter_nullable_int64(maybe(values.lastDayControlledLoadMwh)));
    put(Ids::LAST_DAY_SOLAR_EXPORT, esp_matter_nullable_int64(maybe(values.lastDaySolarExportMwh)));
    put(Ids::LAST_DAY_USAGE_COST, esp_matter_nullable_int32(maybe(values.lastDayUsageCostCents)));
    put(Ids::LAST_DAY_FEED_IN_CREDIT, esp_matter_nullable_int32(maybe(values.lastDayFeedInCreditCents)));

    putFault(Ids::ACCOUNT_FAULT, values.accountFault);
    put(Ids::TOKEN_EXPIRES_AT, esp_matter_nullable_uint32(maybe(values.tokenExpiresAt)));
    put(Ids::TOKEN_EXPIRING_SOON, esp_matter_nullable_bool(maybe(values.tokenExpiringSoon)));
    put(Ids::DAILY_LIMIT_REMAINING, esp_matter_nullable_uint16(maybe(values.dailyLimitRemaining)));
}

}
