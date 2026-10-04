#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <sdkconfig.h>

#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/clusters/commodity-metering-server/commodity-metering-server.h>
#include <app/clusters/electrical-energy-measurement-server/ElectricalEnergyMeasurementCluster.h>
#include <app/clusters/electrical-energy-measurement-server/electrical-energy-measurement-server.h>
#include <app/clusters/electrical-power-measurement-server/electrical-power-measurement-server.h>
#include <app/clusters/meter-identification-server/meter-identification-server.h>
#include <app/clusters/power-topology-server/power-topology-server.h>
#include <app/reporting/reporting.h>
#include <lib/core/ErrorStr.h>
#include <platform/CHIPDeviceLayer.h>
#include <system/SystemClock.h>

#include "flipped/core/energy_publication.h"
#include "flipped/matter/ids.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

namespace Clusters = chip::app::Clusters;
namespace EEM = Clusters::ElectricalEnergyMeasurement;
namespace EPM = Clusters::ElectricalPowerMeasurement;
namespace Topology = Clusters::PowerTopology;
namespace Metering = Clusters::CommodityMetering;
namespace MeterId = Clusters::MeterIdentification;
namespace Globals = Clusters::Globals;
using chip::app::DataModel::List;
using chip::app::DataModel::Nullable;
using AccuracyRange = Clusters::detail::Structs::MeasurementAccuracyRangeStruct::Type;
using Accuracy = Clusters::detail::Structs::MeasurementAccuracyStruct::Type;

const char *TAG = "flipped_meter";

constexpr int64_t ACTIVE_POWER_MAX_MW = 100000000;
constexpr uint64_t ACTIVE_POWER_FIXED_MAX_MW = 1000;
constexpr uint64_t ENERGY_FIXED_MAX_MWH = 1000;

class PowerDelegate : public EPM::Delegate {
public:
    EPM::PowerModeEnum GetPowerMode() override { return EPM::PowerModeEnum::kAc; }
    uint8_t GetNumberOfMeasurementTypes() override { return 1; }
    CHIP_ERROR StartAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetAccuracyByIndex(uint8_t index, Accuracy &accuracy) override
    {
        if (index > 0) {
            return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
        }
        range_.rangeMin = 0;
        range_.rangeMax = ACTIVE_POWER_MAX_MW;
        range_.fixedMax.SetValue(ACTIVE_POWER_FIXED_MAX_MW);
        accuracy.measurementType = Clusters::detail::MeasurementTypeEnum::kActivePower;
        accuracy.measured = false;
        accuracy.minMeasuredValue = 0;
        accuracy.maxMeasuredValue = ACTIVE_POWER_MAX_MW;
        accuracy.accuracyRanges = List<const AccuracyRange>(&range_, 1);
        return CHIP_NO_ERROR;
    }
    CHIP_ERROR EndAccuracyRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetRangeByIndex(uint8_t, EPM::Structs::MeasurementRangeStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndRangesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicCurrentsByIndex(uint8_t, EPM::Structs::HarmonicMeasurementStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicCurrentsRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR StartHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
    CHIP_ERROR GetHarmonicPhasesByIndex(uint8_t, EPM::Structs::HarmonicMeasurementStruct::Type &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR EndHarmonicPhasesRead() override { return CHIP_NO_ERROR; }
    Nullable<int64_t> GetVoltage() override { return {}; }
    Nullable<int64_t> GetActiveCurrent() override { return {}; }
    Nullable<int64_t> GetReactiveCurrent() override { return {}; }
    Nullable<int64_t> GetApparentCurrent() override { return {}; }
    Nullable<int64_t> GetActivePower() override { return {}; }
    Nullable<int64_t> GetReactivePower() override { return {}; }
    Nullable<int64_t> GetApparentPower() override { return {}; }
    Nullable<int64_t> GetRMSVoltage() override { return {}; }
    Nullable<int64_t> GetRMSCurrent() override { return {}; }
    Nullable<int64_t> GetRMSPower() override { return {}; }
    Nullable<int64_t> GetFrequency() override { return {}; }
    Nullable<int64_t> GetPowerFactor() override { return {}; }
    Nullable<int64_t> GetNeutralCurrent() override { return {}; }

private:
    AccuracyRange range_;
};

class TopologyDelegate : public Topology::Delegate {
public:
    CHIP_ERROR GetAvailableEndpointAtIndex(size_t, chip::EndpointId &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
    CHIP_ERROR GetActiveEndpointAtIndex(size_t, chip::EndpointId &) override
    {
        return CHIP_ERROR_PROVIDER_LIST_EXHAUSTED;
    }
};

PowerDelegate powerDelegate;
TopologyDelegate topologyDelegate;
AccuracyRange energyRange;
Accuracy energyAccuracy;
std::array<chip::app::DataModel::Provider::SemanticTag, 4> meterTags;

#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
PowerDelegate gridPowerDelegate;
std::optional<uint16_t> gridEndpoint;
#endif

bool isGridEnergy(chip::EndpointId endpoint)
{
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
    return gridEndpoint && endpoint == *gridEndpoint;
#else
    (void)endpoint;
    return false;
#endif
}

class EnergyAccess : public EEM::ElectricalEnergyMeasurementAttrAccess {
public:
    using EEM::ElectricalEnergyMeasurementAttrAccess::ElectricalEnergyMeasurementAttrAccess;

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath &path, chip::app::AttributeValueEncoder &encoder) override
    {
        if (path.mAttributeId == EEM::Attributes::FeatureMap::Id && isGridEnergy(path.mEndpointId)) {
            return encoder.Encode(chip::BitMask<EEM::Feature, uint32_t>(
                EEM::Feature::kImportedEnergy, EEM::Feature::kCumulativeEnergy, EEM::Feature::kPeriodicEnergy));
        }
        return EEM::ElectricalEnergyMeasurementAttrAccess::Read(path, encoder);
    }
};

EnergyAccess *energyAccess = nullptr;
MeterId::Instance *meterIdentification = nullptr;
Metering::Instance *commodityMetering = nullptr;

std::optional<std::string> appliedNmi;
core::MeteringAttribution appliedMetering;
core::EnergyPublication appliedEnergy;
std::array<std::optional<int64_t>, core::ENERGY_ATTRIBUTE_COUNT> lastMarkMs;
std::array<bool, core::ENERGY_ATTRIBUTE_COUNT> pendingMark{};

void checkEsp(esp_err_t err, const char *what)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
        abort();
    }
}

void requireCluster(esp_matter::endpoint_t *endpoint, uint32_t cluster, const char *what)
{
    if (esp_matter::cluster::get(endpoint, cluster) == nullptr) {
        ESP_LOGE(TAG, "EP %u has no %s cluster 0x%04" PRIX32, esp_matter::endpoint::get_id(endpoint), what, cluster);
        abort();
    }
}

int64_t monotonicMs()
{
    return static_cast<int64_t>(chip::System::SystemClock().GetMonotonicMilliseconds64().count());
}

chip::AttributeId attributeId(core::EnergyAttribute attribute)
{
    switch (attribute) {
    case core::EnergyAttribute::cumulativeImported:
        return EEM::Attributes::CumulativeEnergyImported::Id;
    case core::EnergyAttribute::cumulativeExported:
        return EEM::Attributes::CumulativeEnergyExported::Id;
    case core::EnergyAttribute::periodicImported:
        return EEM::Attributes::PeriodicEnergyImported::Id;
    case core::EnergyAttribute::periodicExported:
        return EEM::Attributes::PeriodicEnergyExported::Id;
    case core::EnergyAttribute::cumulativeReset:
        return EEM::Attributes::CumulativeEnergyReset::Id;
    }
    ESP_LOGE(TAG, "energy attribute %u", static_cast<unsigned>(attribute));
    abort();
}

#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
bool importedAttribute(core::EnergyAttribute attribute)
{
    return attribute == core::EnergyAttribute::cumulativeImported || attribute == core::EnergyAttribute::periodicImported;
}
#endif

void markNow(size_t slot, int64_t nowMs)
{
    const auto attribute = static_cast<core::EnergyAttribute>(slot);
    MatterReportingAttributeChangeCallback(ELECTRICAL_METER_ENDPOINT, EEM::Id, attributeId(attribute));
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
    if (importedAttribute(attribute)) {
        MatterReportingAttributeChangeCallback(*gridEndpoint, EEM::Id, attributeId(attribute));
    }
#endif
    lastMarkMs[slot] = nowMs;
    pendingMark[slot] = false;
}

void armDeferredMarks(int64_t nowMs);

void onDeferredMarks(chip::System::Layer *, void *)
{
    const int64_t nowMs = monotonicMs();
    for (size_t slot = 0; slot < core::ENERGY_ATTRIBUTE_COUNT; ++slot) {
        if (pendingMark[slot] && !core::deferredMarkAt(lastMarkMs[slot], nowMs)) {
            markNow(slot, nowMs);
        }
    }
    armDeferredMarks(nowMs);
}

void armDeferredMarks(int64_t nowMs)
{
    std::optional<int64_t> earliest;
    for (size_t slot = 0; slot < core::ENERGY_ATTRIBUTE_COUNT; ++slot) {
        if (!pendingMark[slot]) {
            continue;
        }
        const std::optional<int64_t> target = core::deferredMarkAt(lastMarkMs[slot], nowMs);
        if (target && (!earliest || *target < *earliest)) {
            earliest = target;
        }
    }
    if (!earliest) {
        return;
    }
    const auto delay = static_cast<uint32_t>(std::max<int64_t>(*earliest - nowMs, 1));
    checkChip(chip::DeviceLayer::SystemLayer().StartTimer(chip::System::Clock::Milliseconds32(delay), onDeferredMarks,
                                                          nullptr),
              "SystemLayer StartTimer for a deferred energy mark");
}

void mark(core::EnergyAttribute attribute)
{
    const auto slot = static_cast<size_t>(attribute);
    const int64_t nowMs = monotonicMs();
    if (core::deferredMarkAt(lastMarkMs[slot], nowMs)) {
        pendingMark[slot] = true;
        armDeferredMarks(nowMs);
        return;
    }
    markNow(slot, nowMs);
}

chip::Optional<EEM::Structs::EnergyMeasurementStruct::Type> measurement(
    const std::optional<core::EnergyMeasurementValue> &value)
{
    if (!value) {
        return chip::NullOptional;
    }
    EEM::Structs::EnergyMeasurementStruct::Type result;
    result.energy = value->energyMwh;
    if (value->startTimestamp) {
        result.startTimestamp.SetValue(*value->startTimestamp);
    }
    result.endTimestamp.SetValue(value->endTimestamp);
    return chip::MakeOptional(result);
}

void publishEnergy(uint16_t endpoint, const core::EnergyPublication &next, bool exported)
{
    EEM::MeasurementData *data = EEM::MeasurementDataForEndpoint(endpoint);
    if (data == nullptr) {
        ESP_LOGE(TAG, "MeasurementDataForEndpoint(%u) returned null", endpoint);
        abort();
    }
    const std::optional<core::EnergyMeasurementValue> none;
    const chip::Optional<EEM::Structs::EnergyMeasurementStruct::Type> cumulativeExported =
        measurement(exported ? next.cumulativeExported : none);
    const chip::Optional<EEM::Structs::EnergyMeasurementStruct::Type> periodicExported =
        measurement(exported ? next.periodicExported : none);
    if (next.cumulativeEvent) {
        if (!EEM::NotifyCumulativeEnergyMeasured(endpoint, measurement(next.cumulativeImported), cumulativeExported)) {
            ESP_LOGE(TAG, "NotifyCumulativeEnergyMeasured on EP %u returned false", endpoint);
        }
    } else {
        data->cumulativeImported = measurement(next.cumulativeImported);
        data->cumulativeExported = cumulativeExported;
    }
    if (next.periodicEvent) {
        if (!EEM::NotifyPeriodicEnergyMeasured(endpoint, measurement(next.periodicImported), periodicExported)) {
            ESP_LOGE(TAG, "NotifyPeriodicEnergyMeasured on EP %u returned false", endpoint);
        }
    } else {
        data->periodicImported = measurement(next.periodicImported);
        data->periodicExported = periodicExported;
    }
}

void setCumulativeReset(uint16_t endpoint, std::optional<uint32_t> timestamp, bool exported)
{
    chip::Optional<EEM::Structs::CumulativeEnergyResetStruct::Type> reset;
    if (timestamp) {
        EEM::Structs::CumulativeEnergyResetStruct::Type value;
        value.importedResetTimestamp.SetValue(Nullable<uint32_t>(*timestamp));
        if (exported) {
            value.exportedResetTimestamp.SetValue(Nullable<uint32_t>(*timestamp));
        }
        reset.SetValue(value);
    }
    const CHIP_ERROR err = EEM::SetCumulativeReset(endpoint, reset);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "SetCumulativeReset on EP %u: %s", endpoint, chip::ErrorStr(err));
        abort();
    }
}

void applyEnergy(const core::EnergyPublication &next)
{
    const std::vector<core::EnergyAttribute> changed = core::changedEnergyAttributes(appliedEnergy, next);
    publishEnergy(ELECTRICAL_METER_ENDPOINT, next, true);
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
    publishEnergy(*gridEndpoint, next, false);
#endif
    for (const core::EnergyAttribute attribute : changed) {
        if (attribute != core::EnergyAttribute::cumulativeReset) {
            mark(attribute);
            continue;
        }
        setCumulativeReset(ELECTRICAL_METER_ENDPOINT, next.cumulativeResetTimestamp, true);
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
        setCumulativeReset(*gridEndpoint, next.cumulativeResetTimestamp, false);
#endif
    }
    appliedEnergy = next;
}

void applyMetering(const core::MeteringAttribution &next)
{
    if (next.meteredQuantity == appliedMetering.meteredQuantity &&
        next.meteredQuantityTimestamp == appliedMetering.meteredQuantityTimestamp) {
        return;
    }
    if (!next.meteredQuantity) {
        checkChip(commodityMetering->SetMeteredQuantity(Nullable<List<Metering::Structs::MeteredQuantityStruct::Type>>()),
                  "CommodityMetering SetMeteredQuantity");
        checkChip(commodityMetering->SetMeteredQuantityTimestamp(Nullable<uint32_t>()),
                  "CommodityMetering SetMeteredQuantityTimestamp");
        checkChip(commodityMetering->SetTariffUnit(Nullable<Globals::TariffUnitEnum>()), "CommodityMetering SetTariffUnit");
        checkChip(commodityMetering->SetMaximumMeteredQuantities(Nullable<uint16_t>()),
                  "CommodityMetering SetMaximumMeteredQuantities");
        appliedMetering = next;
        return;
    }
    checkChip(commodityMetering->SetMaximumMeteredQuantities(Nullable<uint16_t>(core::MAXIMUM_METERED_QUANTITIES)),
              "CommodityMetering SetMaximumMeteredQuantities");
    checkChip(commodityMetering->SetTariffUnit(Nullable<Globals::TariffUnitEnum>(Globals::TariffUnitEnum::kKWh)),
              "CommodityMetering SetTariffUnit");
    std::vector<Metering::Structs::MeteredQuantityStruct::Type> quantities;
    for (const core::MeteredQuantityValue &value : *next.meteredQuantity) {
        Metering::Structs::MeteredQuantityStruct::Type quantity;
        quantity.tariffComponentIDs = List<const uint32_t>(value.componentIds.data(), value.componentIds.size());
        quantity.quantity = value.quantity;
        quantities.push_back(quantity);
    }
    checkChip(commodityMetering->SetMeteredQuantity(Nullable<List<Metering::Structs::MeteredQuantityStruct::Type>>(
                  List<Metering::Structs::MeteredQuantityStruct::Type>(quantities.data(), quantities.size()))),
              "CommodityMetering SetMeteredQuantity");
    checkChip(commodityMetering->SetMeteredQuantityTimestamp(Nullable<uint32_t>(*next.meteredQuantityTimestamp)),
              "CommodityMetering SetMeteredQuantityTimestamp");
    appliedMetering = next;
}

#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
uint16_t buildGridEnergy(std::optional<uint16_t> stored)
{
    esp_matter::endpoint_t *endpoint =
        stored ? esp_matter::endpoint::resume(node(), esp_matter::ENDPOINT_FLAG_NONE, *stored, nullptr)
               : esp_matter::endpoint::create(node(), esp_matter::ENDPOINT_FLAG_NONE, nullptr);
    require(endpoint != nullptr, stored ? "endpoint::resume of the Grid Energy endpoint returned null"
                                        : "endpoint::create of the Grid Energy endpoint returned null");

    esp_matter::endpoint::on_off_plug_in_unit::config_t plug;
    plug.identify.identify_type = chip::to_underlying(Clusters::Identify::IdentifyTypeEnum::kDisplay);
    plug.on_off_lighting.start_up_on_off = nullable<uint8_t>();
    require(esp_matter::cluster::descriptor::create(endpoint, &plug.descriptor, esp_matter::CLUSTER_FLAG_SERVER) != nullptr,
            "descriptor::create on Grid Energy returned null");
    checkEsp(esp_matter::endpoint::on_off_plug_in_unit::add(endpoint, &plug), "on_off_plug_in_unit::add of Grid Energy");
    checkEsp(esp_matter::endpoint::add_device_type(endpoint, esp_matter::endpoint::electrical_sensor::get_device_type_id(),
                                                   esp_matter::endpoint::electrical_sensor::get_device_type_version()),
             "add_device_type Electrical Sensor on Grid Energy");

    esp_matter::cluster::power_topology::config_t topology;
    topology.feature_flags = esp_matter::cluster::power_topology::feature::node_topology::get_id();
    require(esp_matter::cluster::power_topology::create(endpoint, &topology, esp_matter::CLUSTER_FLAG_SERVER) != nullptr,
            "power_topology::create on Grid Energy returned null");

    esp_matter::cluster::electrical_power_measurement::config_t power;
    power.feature_flags = esp_matter::cluster::electrical_power_measurement::feature::alternating_current::get_id();
    power.delegate = &gridPowerDelegate;
    require(esp_matter::cluster::electrical_power_measurement::create(endpoint, &power, esp_matter::CLUSTER_FLAG_SERVER) !=
                nullptr,
            "electrical_power_measurement::create on Grid Energy returned null");

    esp_matter::cluster::electrical_energy_measurement::config_t energy;
    energy.feature_flags = esp_matter::cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
                           esp_matter::cluster::electrical_energy_measurement::feature::cumulative_energy::get_id() |
                           esp_matter::cluster::electrical_energy_measurement::feature::periodic_energy::get_id();
    esp_matter::cluster_t *measurement =
        esp_matter::cluster::electrical_energy_measurement::create(endpoint, &energy, esp_matter::CLUSTER_FLAG_SERVER);
    require(measurement != nullptr, "electrical_energy_measurement::create on Grid Energy returned null");
    require(esp_matter::cluster::electrical_energy_measurement::attribute::create_cumulative_energy_reset(
                measurement, nullptr, 0, 0) != nullptr,
            "create_cumulative_energy_reset on Grid Energy returned null");

    requireCluster(endpoint, Clusters::Descriptor::Id, "Descriptor");
    requireCluster(endpoint, Clusters::Identify::Id, "Identify");
    requireCluster(endpoint, Clusters::OnOff::Id, "On/Off");
    requireCluster(endpoint, Topology::Id, "Power Topology");
    requireCluster(endpoint, EPM::Id, "Electrical Power Measurement");
    requireCluster(endpoint, EEM::Id, "Electrical Energy Measurement");
    for (const uint32_t attribute : {EEM::Attributes::CumulativeEnergyImported::Id, EEM::Attributes::PeriodicEnergyImported::Id,
                                     EEM::Attributes::CumulativeEnergyReset::Id}) {
        if (esp_matter::attribute::get(measurement, attribute) == nullptr) {
            ESP_LOGE(TAG, "the Grid Energy endpoint has no Electrical Energy Measurement attribute 0x%04" PRIX32, attribute);
            abort();
        }
    }
    checkEsp(esp_matter::endpoint::enable(endpoint), "endpoint::enable of Grid Energy");
    return esp_matter::endpoint::get_id(endpoint);
}
#endif

}

void createEnergyEndpoints()
{
    esp_matter::endpoint::electrical_utility_meter::config_t utilityConfig;
    esp_matter::endpoint_t *utility = esp_matter::endpoint::electrical_utility_meter::create(
        node(), &utilityConfig, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
    require(utility != nullptr, "electrical_utility_meter::create returned null");
    require(esp_matter::endpoint::get_id(utility) == UTILITY_METER_ENDPOINT,
            "the electrical utility meter endpoint did not get endpoint ID 2");
    esp_matter::cluster::identify::config_t identify;
    identify.identify_type = chip::to_underlying(Clusters::Identify::IdentifyTypeEnum::kDisplay);
    require(esp_matter::cluster::identify::create(utility, &identify, esp_matter::CLUSTER_FLAG_SERVER) != nullptr,
            "identify::create on EP 2 returned null");

    esp_matter::endpoint::electrical_meter::config_t meterConfig;
    meterConfig.electrical_energy_measurement.feature_flags =
        esp_matter::cluster::electrical_energy_measurement::feature::imported_energy::get_id() |
        esp_matter::cluster::electrical_energy_measurement::feature::exported_energy::get_id() |
        esp_matter::cluster::electrical_energy_measurement::feature::cumulative_energy::get_id() |
        esp_matter::cluster::electrical_energy_measurement::feature::periodic_energy::get_id();
    meterConfig.electrical_power_measurement.feature_flags =
        esp_matter::cluster::electrical_power_measurement::feature::alternating_current::get_id();
    meterConfig.electrical_power_measurement.delegate = &powerDelegate;
    esp_matter::endpoint_t *meter =
        esp_matter::endpoint::electrical_meter::create(node(), &meterConfig, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
    require(meter != nullptr, "electrical_meter::create returned null");
    require(esp_matter::endpoint::get_id(meter) == ELECTRICAL_METER_ENDPOINT,
            "the electrical meter endpoint did not get endpoint ID 3");
    checkEsp(esp_matter::endpoint::add_device_type(meter, esp_matter::endpoint::electrical_sensor::get_device_type_id(),
                                                   esp_matter::endpoint::electrical_sensor::get_device_type_version()),
             "add_device_type Electrical Sensor on EP 3");

    esp_matter::cluster::power_topology::config_t topology;
    topology.feature_flags = esp_matter::cluster::power_topology::feature::set_topology::get_id();
    topology.delegate = &topologyDelegate;
    require(esp_matter::cluster::power_topology::create(meter, &topology, esp_matter::CLUSTER_FLAG_SERVER) != nullptr,
            "power_topology::create on EP 3 returned null");

    esp_matter::cluster::commodity_price::config_t priceConfig;
    esp_matter::cluster_t *price =
        esp_matter::cluster::commodity_price::create(meter, &priceConfig, esp_matter::CLUSTER_FLAG_SERVER);
    require(price != nullptr, "commodity_price::create on EP 3 returned null");
    checkEsp(esp_matter::cluster::commodity_price::feature::forecasting::add(price),
             "commodity_price forecasting::add on EP 3");
    require(esp_matter::cluster::commodity_price::command::create_get_detailed_price_request(price) != nullptr &&
                esp_matter::cluster::commodity_price::command::create_get_detailed_price_response(price) != nullptr &&
                esp_matter::cluster::commodity_price::command::create_get_detailed_forecast_request(price) != nullptr &&
                esp_matter::cluster::commodity_price::command::create_get_detailed_forecast_response(price) != nullptr,
            "a Commodity Price command on EP 3 was not created");
    require(esp_matter::cluster::commodity_price::event::create_price_change(price) != nullptr,
            "the Commodity Price PriceChange event on EP 3 was not created");

    esp_matter::cluster::commodity_tariff::config_t tariffConfig;
    tariffConfig.feature_flags = esp_matter::cluster::commodity_tariff::feature::pricing::get_id() |
                                 esp_matter::cluster::commodity_tariff::feature::peak_period::get_id();
    require(esp_matter::cluster::commodity_tariff::create(meter, &tariffConfig, esp_matter::CLUSTER_FLAG_SERVER) !=
                nullptr,
            "commodity_tariff::create on EP 3 returned null");

    esp_matter::cluster::commodity_metering::config_t meteringConfig;
    require(esp_matter::cluster::commodity_metering::create(meter, &meteringConfig, esp_matter::CLUSTER_FLAG_SERVER) !=
                nullptr,
            "commodity_metering::create on EP 3 returned null");

    require(esp_matter::cluster::electrical_energy_measurement::attribute::create_cumulative_energy_reset(
                esp_matter::cluster::get(meter, EEM::Id), nullptr, 0, 0) != nullptr,
            "create_cumulative_energy_reset on EP 3 returned null");
    checkEsp(esp_matter::endpoint::set_parent_endpoint(meter, utility), "set_parent_endpoint EP 3 -> EP 2");

    esp_matter::cluster_t *descriptor = esp_matter::cluster::get(meter, Clusters::Descriptor::Id);
    require(descriptor != nullptr, "EP 3 has no Descriptor cluster");
    checkEsp(esp_matter::cluster::descriptor::feature::tag_list::add(descriptor), "descriptor tag_list::add on EP 3");
    const std::array<std::pair<uint8_t, uint8_t>, 4> tags = {{{0x0F, 0x01}, {0x13, 0x00}, {0x0A, 0x01}, {0x0B, 0x00}}};
    for (size_t i = 0; i < tags.size(); ++i) {
        meterTags[i].namespaceID = tags[i].first;
        meterTags[i].tag = tags[i].second;
    }
    checkEsp(esp_matter::endpoint::set_semantic_tags(meter, meterTags.data(), meterTags.size()),
             "set_semantic_tags on EP 3");
    require(esp_matter::endpoint::get_semantic_tag_count(meter) == 4, "EP 3 does not hold 4 semantic tags");

    requireCluster(utility, Clusters::Descriptor::Id, "Descriptor");
    requireCluster(utility, Clusters::Identify::Id, "Identify");
    requireCluster(utility, MeterId::Id, "Meter Identification");
    requireCluster(meter, Clusters::CommodityPrice::Id, "Commodity Price");
    requireCluster(meter, Clusters::CommodityTariff::Id, "Commodity Tariff");
    requireCluster(meter, Metering::Id, "Commodity Metering");
    requireCluster(meter, EPM::Id, "Electrical Power Measurement");
    requireCluster(meter, EEM::Id, "Electrical Energy Measurement");
    requireCluster(meter, Topology::Id, "Power Topology");
}

#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
void restoreGridEnergy()
{
    require(!gridEndpoint, "restoreGridEnergy called twice");
    const std::optional<uint16_t> stored = storedEndpointId(NVS_GRID_ENERGY_ENDPOINT);
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        gridEndpoint = buildGridEnergy(stored);
    }
    if (!stored) {
        storeEndpointId(NVS_GRID_ENERGY_ENDPOINT, *gridEndpoint);
    }
    ESP_LOGI(TAG, "%s Grid Energy endpoint %u", stored ? "resumed" : "created", *gridEndpoint);
}

uint16_t gridEnergyEndpoint()
{
    require(gridEndpoint.has_value(), "the Grid Energy endpoint is not restored");
    return *gridEndpoint;
}
#endif

void startEnergyServers()
{
    require(energyAccess == nullptr, "the energy servers are started twice");
    energyAccess = new EnergyAccess(
        chip::BitMask<EEM::Feature, uint32_t>(EEM::Feature::kImportedEnergy, EEM::Feature::kExportedEnergy,
                                              EEM::Feature::kCumulativeEnergy, EEM::Feature::kPeriodicEnergy),
        chip::BitMask<EEM::OptionalAttributes, uint32_t>(EEM::OptionalAttributes::kOptionalAttributeCumulativeEnergyReset));
    checkChip(energyAccess->Init(), "ElectricalEnergyMeasurementAttrAccess::Init");
    require(EEM::MeasurementDataForEndpoint(ELECTRICAL_METER_ENDPOINT) != nullptr,
            "MeasurementDataForEndpoint(3) returned null");
    energyRange.rangeMin = 0;
    energyRange.rangeMax = static_cast<int64_t>(core::ENERGY_MWH_MAX);
    energyRange.fixedMax.SetValue(ENERGY_FIXED_MAX_MWH);
    energyAccuracy.measurementType = Clusters::detail::MeasurementTypeEnum::kElectricalEnergy;
    energyAccuracy.measured = true;
    energyAccuracy.minMeasuredValue = 0;
    energyAccuracy.maxMeasuredValue = static_cast<int64_t>(core::ENERGY_MWH_MAX);
    energyAccuracy.accuracyRanges = List<const AccuracyRange>(&energyRange, 1);
    checkChip(EEM::SetMeasurementAccuracy(ELECTRICAL_METER_ENDPOINT, energyAccuracy), "SetMeasurementAccuracy on EP 3");
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
    require(gridEndpoint.has_value(), "the energy servers started before the Grid Energy endpoint was restored");
    checkChip(EEM::SetMeasurementAccuracy(*gridEndpoint, energyAccuracy), "SetMeasurementAccuracy on Grid Energy");
#endif

    meterIdentification = new MeterId::Instance(UTILITY_METER_ENDPOINT, chip::BitMask<MeterId::Feature>());
    checkChip(meterIdentification->Init(), "MeterIdentification::Instance::Init");
    checkChip(meterIdentification->SetMeterType(Nullable<MeterId::MeterTypeEnum>(MeterId::MeterTypeEnum::kUtility)),
              "MeterIdentification SetMeterType");

    commodityMetering = new Metering::Instance(ELECTRICAL_METER_ENDPOINT);
    checkChip(commodityMetering->Init(), "CommodityMetering::Instance::Init");

    initCommodityPrice();
    initCommodityTariff();
    ESP_LOGI(TAG, "EP 2 and EP 3 cluster servers started");
}

void applyMeter(const std::optional<std::string> &nmi, const core::MeteringAttribution &metering,
                const core::EnergyPublication &energy)
{
    require(meterIdentification != nullptr && commodityMetering != nullptr,
            "EP 2 / EP 3 published before their servers were started");
    if (nmi != appliedNmi) {
        checkChip(meterIdentification->SetPointOfDelivery(nmi ? Nullable<chip::CharSpan>(chip::CharSpan(nmi->data(), nmi->size()))
                                                              : Nullable<chip::CharSpan>()),
                  "MeterIdentification SetPointOfDelivery");
        appliedNmi = nmi;
    }
    applyMetering(metering);
    applyEnergy(energy);
}

}
