#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <esp_matter.h>
#include <platform/CHIPDeviceLayer.h>
#include <platform/CommissionableDataProvider.h>
#include <platform/DeviceInstanceInfoProvider.h>

#include "flipped/core/energy_publication.h"
#include "flipped/core/eve_history.h"
#include "flipped/core/flipped_cluster.h"
#include "flipped/core/metering_attribution.h"
#include "flipped/core/switch_plan.h"
#include "flipped/core/tariff_tables.h"
#include "flipped/matter/matter_service.h"

namespace flipped::matter {

const Hooks &hooks();
esp_matter::node_t *node();
esp_matter::endpoint_t *aggregator();
void require(bool condition, const char *what);
void scheduleWork(chip::DeviceLayer::AsyncWorkFunct work, intptr_t arg, const char *what);
void eraseKey(const char *key);
std::optional<uint16_t> storedEndpointId(const char *key);
void storeEndpointId(const char *key, uint16_t id);

void createFlippedEnergyCluster(esp_matter::endpoint_t *root);
void applyFlippedEnergy(const core::FlippedClusterValues &values);

void followInstance(const std::optional<std::string> &instanceKey, bool accountOk);
void applySwitches(const std::array<std::optional<bool>, core::SWITCH_COUNT> &values);
esp_err_t onAttribute(esp_matter::attribute::callback_type_t type, uint16_t endpoint, uint32_t cluster,
                      uint32_t attribute, esp_matter_attr_val_t *value, void *priv);

struct EnergyPayload {
    std::optional<std::string> nmi;
    core::TariffTables tables;
    core::CommodityPriceValues price;
    core::MeteringAttribution metering;
    core::EnergyPublication energy;
};

void createEnergyEndpoints();
void restoreGridEnergy();
uint16_t gridEnergyEndpoint();
void startEnergyServers();
void initCommodityPrice();
void applyCommodityPrice(const core::CommodityPriceValues &values);
void initCommodityTariff();
void applyCommodityTariff(core::TariffTables tables);
void applyMeter(const std::optional<std::string> &nmi, const core::MeteringAttribution &metering,
                const core::EnergyPublication &energy);
void checkChip(CHIP_ERROR err, const char *what);

struct EvePayload {
    std::optional<std::string> h;
    std::vector<core::EnergyEntry> fresh;
    std::optional<uint64_t> importedMwh;
    std::optional<float> averageWatts;
};

EvePayload evePayload(const Update &update);
void applyEveHistory(EvePayload payload);
std::optional<uint16_t> eveHistoryEndpoint();
void refreshDeviceVisibility();

void onDeviceEvent(const chip::DeviceLayer::ChipDeviceEvent *event, intptr_t arg);
void startCommissioningFacts();
std::pair<std::string, std::string> onboardingCodes(bool ble, bool onNetwork);

chip::DeviceLayer::CommissionableDataProvider &commissionableDataProvider();
chip::DeviceLayer::DeviceInstanceInfoProvider &deviceInstanceInfoProvider();

}
