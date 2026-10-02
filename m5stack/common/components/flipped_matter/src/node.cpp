#include <cstdlib>
#include <cstring>
#include <optional>
#include <utility>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <esp_matter_providers.h>
#include <nvs.h>
#include <sdkconfig.h>

#include <app-common/zap-generated/ids/Clusters.h>
#include <lib/core/ErrorStr.h>

#include "flipped/matter/ids.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

const char *TAG = "flipped_matter";

Hooks installed;
bool hooksInstalled = false;
esp_matter::node_t *createdNode = nullptr;
esp_matter::endpoint_t *createdAggregator = nullptr;

esp_err_t onIdentify(esp_matter::identification::callback_type_t type, uint16_t endpoint, uint8_t effectId,
                     uint8_t effectVariant, void *)
{
    switch (type) {
    case esp_matter::identification::START:
        if (installed.identify) {
            installed.identify(endpoint, true);
        }
        return ESP_OK;
    case esp_matter::identification::STOP:
        if (installed.identify) {
            installed.identify(endpoint, false);
        }
        return ESP_OK;
    case esp_matter::identification::EFFECT:
        if (installed.identifyEffect) {
            installed.identifyEffect(endpoint, effectId, effectVariant);
        }
        return ESP_OK;
    }
    ESP_LOGE(TAG, "identify callback type %d on endpoint %u", static_cast<int>(type), endpoint);
    return ESP_ERR_INVALID_ARG;
}

}

const Hooks &hooks()
{
    return installed;
}

esp_matter::node_t *node()
{
    require(createdNode != nullptr, "the Matter node is not created");
    return createdNode;
}

esp_matter::endpoint_t *aggregator()
{
    require(createdAggregator != nullptr, "the aggregator endpoint is not created");
    return createdAggregator;
}

void require(bool condition, const char *what)
{
    if (!condition) {
        ESP_LOGE(TAG, "%s", what);
        abort();
    }
}

void scheduleWork(chip::DeviceLayer::AsyncWorkFunct work, intptr_t arg, const char *what)
{
    const CHIP_ERROR err = chip::DeviceLayer::PlatformMgr().ScheduleWork(work, arg);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "ScheduleWork %s: %s", what, chip::ErrorStr(err));
        abort();
    }
}

void checkChip(CHIP_ERROR err, const char *what)
{
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "%s: %s", what, chip::ErrorStr(err));
        abort();
    }
}

void eraseKey(const char *key)
{
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    const esp_err_t err = key == nullptr ? nvs_erase_all(handle) : nvs_erase_key(handle, key);
    if (err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_ERROR_CHECK(err);
    }
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

std::optional<uint16_t> storedEndpointId(const char *key)
{
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    uint16_t id = 0;
    const esp_err_t err = nvs_get_u16(handle, key, &id);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return std::nullopt;
    }
    ESP_ERROR_CHECK(err);
    return id;
}

void storeEndpointId(const char *key, uint16_t id)
{
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_u16(handle, key, id));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

void createNode(Hooks hooks)
{
    require(!hooksInstalled, "createNode called twice");
    installed = std::move(hooks);
    hooksInstalled = true;
    loadPreferences();

    esp_matter::node::config_t config;
    std::strcpy(config.root_node.basic_information.node_label, VENDOR_NAME);
    createdNode = esp_matter::node::create(&config, onAttribute, onIdentify);
    require(createdNode != nullptr, "esp_matter::node::create returned null");

    esp_matter::endpoint_t *root = esp_matter::endpoint::get(createdNode, ROOT_ENDPOINT);
    require(root != nullptr, "endpoint 0 does not exist after esp_matter::node::create");
    esp_matter::cluster::time_synchronization::config_t timeSync;
    esp_matter::cluster::time_synchronization::create(root, &timeSync, esp_matter::CLUSTER_FLAG_SERVER);
    createFlippedEnergyCluster(root);

    esp_matter::endpoint::aggregator::config_t aggregatorConfig;
    createdAggregator =
        esp_matter::endpoint::aggregator::create(createdNode, &aggregatorConfig, esp_matter::ENDPOINT_FLAG_NONE, nullptr);
    require(createdAggregator != nullptr, "esp_matter::endpoint::aggregator::create returned null");
    require(esp_matter::endpoint::get_id(createdAggregator) == AGGREGATOR_ENDPOINT,
            "the aggregator endpoint did not get endpoint ID 1");

    namespace Clusters = chip::app::Clusters;
    require(esp_matter::cluster::get(root, Clusters::Descriptor::Id) != nullptr, "EP 0 has no Descriptor cluster");
    require(esp_matter::cluster::get(root, Clusters::BasicInformation::Id) != nullptr,
            "EP 0 has no Basic Information cluster");
    require(esp_matter::cluster::get(root, Clusters::TimeSynchronization::Id) != nullptr,
            "EP 0 has no Time Synchronization cluster");
    require(esp_matter::cluster::get(root, FLIPPED_ENERGY_CLUSTER_ID) != nullptr,
            "EP 0 has no Flipped Energy cluster 0xFFF1FC01");
    require(esp_matter::cluster::get(createdAggregator, Clusters::Descriptor::Id) != nullptr,
            "EP 1 has no Descriptor cluster");
#if CONFIG_FLIPPED_ENERGY_ENDPOINTS
    createEnergyEndpoints();
#endif
}

void start()
{
    require(createdNode != nullptr, "start called before createNode");
    esp_matter::set_custom_commissionable_data_provider(&commissionableDataProvider());
    esp_matter::set_custom_device_instance_info_provider(&deviceInstanceInfoProvider());
    ESP_ERROR_CHECK(esp_matter::start(onDeviceEvent));
    startCommissioningFacts();
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
    restoreGridEnergy();
#endif
#if CONFIG_FLIPPED_ENERGY_ENDPOINTS
    scheduleWork([](intptr_t) { startEnergyServers(); }, 0, "start the EP 2 / EP 3 cluster servers");
#endif
}

}
