#include <vector>

#include <esp_log.h>

#include <app-common/zap-generated/cluster-objects.h>
#include <app/clusters/commodity-price-server/commodity-price-server.h>

#include "flipped/matter/ids.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

namespace Price = chip::app::Clusters::CommodityPrice;
namespace Globals = chip::app::Clusters::Globals;
using chip::app::DataModel::List;
using chip::app::DataModel::Nullable;
using PriceStruct = Price::Structs::CommodityPriceStruct::Type;

const char *TAG = "flipped_price";

class PriceDelegate : public Price::Delegate {};

PriceDelegate delegate;
Price::Instance *instance = nullptr;
core::CommodityPriceValues applied;

PriceStruct priceStruct(const core::PriceValue &value)
{
    PriceStruct result;
    result.periodStart = value.periodStart;
    if (value.periodEnd) {
        result.periodEnd.SetNonNull(*value.periodEnd);
    }
    result.price.SetValue(value.price);
    result.priceLevel.SetValue(value.priceLevel);
    if (!value.description.empty()) {
        result.description.SetValue(chip::CharSpan(value.description.data(), value.description.size()));
    }
    return result;
}

}

void initCommodityPrice()
{
    require(instance == nullptr, "the Commodity Price server is initialised twice");
    instance = new Price::Instance(ELECTRICAL_METER_ENDPOINT, delegate, Price::Feature::kForecasting);
    checkChip(instance->Init(), "CommodityPrice::Instance::Init");
    checkChip(instance->SetTariffUnit(Globals::TariffUnitEnum::kKWh), "CommodityPrice SetTariffUnit");
    Globals::Structs::CurrencyStruct::Type currency;
    currency.currency = core::CURRENCY_AUD;
    currency.decimalPoints = core::CURRENCY_DECIMAL_POINTS;
    checkChip(instance->SetCurrency(currency), "CommodityPrice SetCurrency");
    checkChip(instance->SetCurrentPrice(Nullable<PriceStruct>()), "CommodityPrice SetCurrentPrice");
    checkChip(instance->SetForecast(List<const PriceStruct>()), "CommodityPrice SetForecast");
}

void applyCommodityPrice(const core::CommodityPriceValues &values)
{
    require(instance != nullptr, "Commodity Price published before its server was initialised");
    if (values.current != applied.current) {
        Nullable<PriceStruct> current;
        if (values.current) {
            current.SetNonNull(priceStruct(*values.current));
        }
        checkChip(instance->SetCurrentPrice(current), "CommodityPrice SetCurrentPrice");
        const chip::Protocols::InteractionModel::Status status = instance->GeneratePriceChangeEvent();
        if (status != chip::Protocols::InteractionModel::Status::Success) {
            ESP_LOGE(TAG, "GeneratePriceChangeEvent: status 0x%02x", static_cast<unsigned>(status));
        }
        applied.current = values.current;
    }
    if (values.forecast != applied.forecast) {
        std::vector<PriceStruct> entries;
        for (const core::PriceValue &value : values.forecast) {
            entries.push_back(priceStruct(value));
        }
        checkChip(instance->SetForecast(List<const PriceStruct>(entries.data(), entries.size())),
                  "CommodityPrice SetForecast");
        applied.forecast = values.forecast;
    }
}

}
