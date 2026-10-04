#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <cinttypes>
#include <cstdlib>
#include <mutex>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>
#include <nvs.h>
#include <sdkconfig.h>

#include <app-common/zap-generated/cluster-objects.h>
#include <app-common/zap-generated/ids/Attributes.h>
#include <app-common/zap-generated/ids/Clusters.h>
#include <app/EventLogging.h>
#include <app/reporting/reporting.h>
#include <lib/core/ErrorStr.h>

#include "flipped/core/identity.h"
#include "flipped/matter/ids.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

namespace Clusters = chip::app::Clusters;
namespace Bridged = chip::app::Clusters::BridgedDeviceBasicInformation;

const char *TAG = "flipped_switches";
constexpr uint8_t SWITCH_FLAGS = esp_matter::ENDPOINT_FLAG_DESTROYABLE | esp_matter::ENDPOINT_FLAG_BRIDGE;

#if CONFIG_FLIPPED_ENERGY_ENDPOINTS
constexpr size_t ENERGY_ENDPOINT_COUNT = 2;
#else
constexpr size_t ENERGY_ENDPOINT_COUNT = 0;
#endif
#if CONFIG_FLIPPED_EVE_HISTORY
constexpr size_t EVE_ENDPOINT_COUNT = 1;
#else
constexpr size_t EVE_ENDPOINT_COUNT = 0;
#endif
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
constexpr size_t GRID_ENERGY_ENDPOINT_COUNT = 1;
#else
constexpr size_t GRID_ENERGY_ENDPOINT_COUNT = 0;
#endif
static_assert(2 + ENERGY_ENDPOINT_COUNT + GRID_ENERGY_ENDPOINT_COUNT + EVE_ENDPOINT_COUNT + core::SWITCH_COUNT ==
                  CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT,
              "CONFIG_ESP_MATTER_MAX_DYNAMIC_ENDPOINT_COUNT differs from the endpoints this build creates");

struct Applied {
    std::string h;
    std::array<uint16_t, core::SWITCH_COUNT> endpointIds{};
};

std::mutex appliedMutex;
std::optional<Applied> applied;
std::atomic<bool> virtualEnabled{true};
std::atomic<int> spotPreference{-1};
std::atomic<bool> spotLinked{false};
std::map<uint16_t, bool> endpointVisibility;

void applyVisibility()
{
    esp_matter::lock::ScopedChipStackLock chipLock(portMAX_DELAY);
    const auto show = [](uint16_t id, bool enabled) {
        const auto prior = endpointVisibility.find(id);
        if ((prior == endpointVisibility.end() && enabled) || (prior != endpointVisibility.end() && prior->second == enabled)) {
            return;
        }
        esp_matter::endpoint_t *endpoint = esp_matter::endpoint::get(node(), id);
        require(endpoint != nullptr, "visibility endpoint does not exist");
        ESP_ERROR_CHECK(enabled ? esp_matter::endpoint::enable(endpoint) : esp_matter::endpoint::disable(endpoint));
        endpointVisibility[id] = enabled;
    };
    std::lock_guard<std::mutex> guard(appliedMutex);
    if (applied) {
        for (size_t i = 0; i < core::SWITCH_COUNT; ++i) {
            const bool price = std::string_view(core::SWITCH_IDENTITIES[i].key).find("wholesale") != std::string_view::npos;
            show(applied->endpointIds[i], virtualDevicesEnabled() && (!price || spotPricesEnabled()));
        }
    }
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
    show(gridEnergyEndpoint(), virtualDevicesEnabled());
#endif
#if CONFIG_FLIPPED_EVE_HISTORY
    if (const auto id = eveHistoryEndpoint()) {
        show(*id, virtualDevicesEnabled());
    }
#endif
}

void savePreference(const char *key, uint8_t value)
{
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_u8(handle, key, value));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}


void check(esp_err_t err, const char *what, uint16_t endpoint)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s on endpoint %u: %s", what, endpoint, esp_err_to_name(err));
        abort();
    }
}

std::optional<core::StoredSwitches> readBlob()
{
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    std::array<uint8_t, core::STORED_SWITCHES_BYTES + 1> buffer{};
    size_t size = buffer.size();
    const esp_err_t err = nvs_get_blob(handle, NVS_SWITCHES, buffer.data(), &size);
    nvs_close(handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return std::nullopt;
    }
    ESP_ERROR_CHECK(err);
    std::variant<core::StoredSwitches, core::Invalid> decoded = core::decodeStoredSwitches(buffer.data(), size);
    if (const core::Invalid *invalid = std::get_if<core::Invalid>(&decoded)) {
        ESP_LOGE(TAG, "%s/%s: %s", NVS_NAMESPACE, NVS_SWITCHES, invalid->message.c_str());
        abort();
    }
    return std::get<core::StoredSwitches>(decoded);
}

core::StoredSwitches asStored(const Applied &switches)
{
    core::StoredSwitches result;
    result.h = switches.h;
    std::copy(switches.endpointIds.begin(), switches.endpointIds.end(), result.endpointIds.begin());
    return result;
}

void writeBlob(const Applied &switches)
{
    const std::array<uint8_t, core::STORED_SWITCHES_BYTES> blob =
        core::encodeStoredSwitches(switches.h, switches.endpointIds);
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_blob(handle, NVS_SWITCHES, blob.data(), blob.size()));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

void setString(esp_matter::cluster_t *cluster, uint32_t attributeId, std::string value, uint16_t endpoint)
{
    esp_matter::attribute_t *attribute = esp_matter::attribute::get(cluster, attributeId);
    require(attribute != nullptr, "a Bridged Device Basic Information string attribute is missing");
    esp_matter_attr_val_t val = esp_matter_char_str(value.data(), static_cast<uint16_t>(value.size()));
    const esp_err_t err = esp_matter::attribute::set_val(attribute, &val, false);
    if (err != ESP_ERR_NOT_FINISHED) {
        check(err, "attribute::set_val of a Bridged Device Basic Information string", endpoint);
    }
}

uint16_t build(const core::SwitchEndpoint &plan)
{
    esp_matter::endpoint::bridged_node::config_t bridged;
    bridged.bridged_device_basic_information.reachable = false;
    esp_matter::endpoint_t *endpoint =
        plan.endpointId ? esp_matter::endpoint::bridged_node::resume(node(), &bridged, SWITCH_FLAGS, *plan.endpointId, nullptr)
                        : esp_matter::endpoint::bridged_node::create(node(), &bridged, SWITCH_FLAGS, nullptr);
    if (endpoint == nullptr) {
        ESP_LOGE(TAG, "%s of the %s switch endpoint returned null", plan.endpointId ? "bridged_node::resume" : "bridged_node::create",
                 plan.key.c_str());
        abort();
    }
    const uint16_t id = esp_matter::endpoint::get_id(endpoint);

    esp_matter::endpoint::on_off_plug_in_unit::config_t plug;
    plug.identify.identify_type = chip::to_underlying(Clusters::Identify::IdentifyTypeEnum::kDisplay);
    plug.on_off_lighting.start_up_on_off = nullable<uint8_t>();
    check(esp_matter::endpoint::on_off_plug_in_unit::add(endpoint, &plug), "on_off_plug_in_unit::add", id);
    check(esp_matter::endpoint::set_parent_endpoint(endpoint, aggregator()), "set_parent_endpoint", id);

    esp_matter::cluster_t *basic = esp_matter::cluster::get(endpoint, Bridged::Id);
    require(basic != nullptr, "a switch endpoint has no Bridged Device Basic Information cluster");
    std::string label = plan.nodeLabel;
    std::string vendor = VENDOR_NAME;
    require(esp_matter::cluster::bridged_device_basic_information::attribute::create_node_label(
                basic, label.data(), static_cast<uint16_t>(label.size())) != nullptr,
            "create_node_label returned null");
    require(esp_matter::cluster::bridged_device_basic_information::attribute::create_vendor_name(
                basic, vendor.data(), static_cast<uint16_t>(vendor.size())) != nullptr,
            "create_vendor_name returned null");
    setString(basic, Bridged::Attributes::UniqueID::Id, plan.uniqueId, id);

    for (const uint32_t cluster : {Clusters::Descriptor::Id, Bridged::Id, Clusters::Identify::Id, Clusters::Groups::Id,
                                   Clusters::OnOff::Id, Clusters::ScenesManagement::Id}) {
        if (esp_matter::cluster::get(endpoint, cluster) == nullptr) {
            ESP_LOGE(TAG, "switch endpoint %u has no cluster 0x%04" PRIX32, id, cluster);
            abort();
        }
    }
    esp_matter::attribute_t *onOff = esp_matter::attribute::get(esp_matter::cluster::get(endpoint, Clusters::OnOff::Id),
                                                                 Clusters::OnOff::Attributes::OnOff::Id);
    require(onOff != nullptr, "a switch endpoint has no OnOff attribute");
    check(esp_matter::attribute::set_deferred_persistence(onOff), "attribute::set_deferred_persistence", id);
    check(esp_matter::endpoint::enable(endpoint), "endpoint::enable", id);
    ESP_LOGI(TAG, "%s switch endpoint %u: %s, %s", plan.endpointId ? "resumed" : "created", id, plan.nodeLabel.c_str(),
             plan.uniqueId.c_str());
    return id;
}

void destroyApplied()
{
    std::optional<Applied> old;
    {
        std::lock_guard<std::mutex> lock(appliedMutex);
        old.swap(applied);
    }
    if (!old) {
        return;
    }
    for (const uint16_t id : old->endpointIds) {
        esp_matter::endpoint_t *endpoint = esp_matter::endpoint::get(node(), id);
        require(endpoint != nullptr, "a switch endpoint to destroy does not exist");
        check(esp_matter::endpoint::destroy(node(), endpoint), "endpoint::destroy", id);
        endpointVisibility.erase(id);
        ESP_LOGI(TAG, "destroyed switch endpoint %u", id);
    }
}

void install(const core::SwitchPlan &plan)
{
    Applied next;
    next.h = plan.h;
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        for (size_t i = 0; i < core::SWITCH_COUNT; ++i) {
            next.endpointIds[i] = build(plan.endpoints[i]);
        }
    }
    if (plan.store) {
        writeBlob(next);
    }
    std::vector<EndpointName> labels;
#if CONFIG_FLIPPED_ENERGY_ENDPOINTS
    labels.push_back(EndpointName{UTILITY_METER_ENDPOINT, UTILITY_METER_LABEL});
#endif
#if CONFIG_FLIPPED_GRID_ENERGY_OUTLET
    labels.push_back(EndpointName{gridEnergyEndpoint(), GRID_ENERGY_LABEL});
#endif
#if CONFIG_FLIPPED_EVE_HISTORY
    if (const std::optional<uint16_t> eve = eveHistoryEndpoint()) {
        labels.push_back(EndpointName{*eve, EVE_HISTORY_LABEL});
    }
#endif
    for (size_t i = 0; i < core::SWITCH_COUNT; ++i) {
        labels.push_back(EndpointName{next.endpointIds[i], plan.endpoints[i].nodeLabel});
    }
    {
        std::lock_guard<std::mutex> lock(appliedMutex);
        applied = next;
    }
    if (hooks().labels) {
        hooks().labels(std::move(labels));
    }
    applyVisibility();
}

void reportOnOff(intptr_t endpoint)
{
    MatterReportingAttributeChangeCallback(static_cast<chip::EndpointId>(endpoint), Clusters::OnOff::Id,
                                           Clusters::OnOff::Attributes::OnOff::Id);
}

void report(uint16_t endpoint, uint32_t cluster, uint32_t attribute, esp_matter_attr_val_t value)
{
    check(esp_matter::attribute::report(endpoint, cluster, attribute, &value), "attribute::report", endpoint);
}

void setReachable(uint16_t endpoint, bool reachable)
{
    esp_matter_attr_val_t current = esp_matter_invalid(nullptr);
    esp_matter::attribute_t *attribute = esp_matter::attribute::get(endpoint, Bridged::Id, Bridged::Attributes::Reachable::Id);
    require(attribute != nullptr, "a switch endpoint has no Reachable attribute");
    check(esp_matter::attribute::get_val(attribute, &current), "attribute::get_val Reachable", endpoint);
    if (current.val.b == reachable) {
        return;
    }
    report(endpoint, Bridged::Id, Bridged::Attributes::Reachable::Id, esp_matter_bool(reachable));
    Bridged::Events::ReachableChanged::Type event;
    event.reachableNewValue = reachable;
    chip::EventNumber number = 0;
    const CHIP_ERROR err = chip::app::LogEvent(event, endpoint, number);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "LogEvent ReachableChanged(%s) on endpoint %u: %s", reachable ? "true" : "false", endpoint,
                 chip::ErrorStr(err));
    }
}

}

void loadPreferences()
{
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    uint8_t value = 0;
    for (const char *key : {"virtual", "spot"}) {
        const esp_err_t err = nvs_get_u8(handle, key, &value);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            continue;
        }
        ESP_ERROR_CHECK(err);
        if (std::string_view(key) == "virtual") {
            virtualEnabled = value != 0;
        } else {
            spotPreference = value != 0 ? 1 : 0;
        }
    }
    nvs_close(handle);
}

bool virtualDevicesEnabled() { return virtualEnabled.load(); }
void refreshDeviceVisibility() { applyVisibility(); }
bool spotPricesEnabled() { return spotPreference.load() < 0 ? spotLinked.load() : spotPreference.load() != 0; }

void setVirtualDevicesEnabled(bool enabled)
{
    savePreference("virtual", enabled ? 1 : 0);
    virtualEnabled = enabled;
    applyVisibility();
    hooks().refresh();
}

void setSpotPricesEnabled(bool enabled)
{
    savePreference("spot", enabled ? 1 : 0);
    spotPreference = enabled ? 1 : 0;
    applyVisibility();
    hooks().refresh();
}

void setSpotLinked(bool linked)
{
    if (spotLinked.exchange(linked) != linked) {
        applyVisibility();
    }
}

void restoreSwitches(const std::optional<std::string> &instanceKey)
{
    const std::optional<core::StoredSwitches> stored = readBlob();
    const core::SwitchPlan plan = core::switchPlan(instanceKey, stored);
    switch (plan.action) {
    case core::SwitchAction::none:
        ESP_LOGI(TAG, "no instance is known: no switch endpoint exists");
        applyVisibility();
        return;
    case core::SwitchAction::resume:
        install(plan);
        return;
    case core::SwitchAction::replace:
        if (stored) {
            ESP_LOGW(TAG, "the stored switch endpoints belong to instance %s, the current instance is %s: replacing them",
                     stored->h.c_str(), plan.h.c_str());
            eraseKey(NVS_SWITCHES);
        }
        install(plan);
        return;
    }
    abort();
}

void followInstance(const std::optional<std::string> &instanceKey, bool accountOk)
{
    if (!instanceKey || !accountOk) {
        return;
    }
    std::optional<core::StoredSwitches> current;
    {
        std::lock_guard<std::mutex> lock(appliedMutex);
        if (applied) {
            current = asStored(*applied);
        }
    }
    const core::SwitchPlan plan = core::switchPlan(instanceKey, current);
    if (plan.action != core::SwitchAction::replace) {
        return;
    }
    ESP_LOGW(TAG, "instance changed to %s: replacing the switch endpoints", plan.h.c_str());
    destroyApplied();
    install(plan);
}

void applySwitches(const std::array<std::optional<bool>, core::SWITCH_COUNT> &values)
{
    std::optional<Applied> current;
    {
        std::lock_guard<std::mutex> lock(appliedMutex);
        current = applied;
    }
    if (!current) {
        return;
    }
    for (size_t i = 0; i < core::SWITCH_COUNT; ++i) {
        if (!virtualDevicesEnabled() || (!spotPricesEnabled() && std::string_view(core::SWITCH_IDENTITIES[i].key).find("wholesale") != std::string_view::npos)) {
            continue;
        }
        const uint16_t endpoint = current->endpointIds[i];
        if (!values[i]) {
            setReachable(endpoint, false);
            continue;
        }
        setReachable(endpoint, true);
        report(endpoint, Clusters::OnOff::Id, Clusters::OnOff::Attributes::OnOff::Id, esp_matter_bool(*values[i]));
    }
}

esp_err_t onAttribute(esp_matter::attribute::callback_type_t type, uint16_t endpoint, uint32_t cluster,
                      uint32_t attribute, esp_matter_attr_val_t *, void *)
{
    if (type != esp_matter::attribute::PRE_UPDATE || cluster != Clusters::OnOff::Id) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "refused a change of On/Off attribute 0x%04" PRIX32 " on endpoint %u: the switch is read-only",
             attribute, endpoint);
    scheduleWork(reportOnOff, endpoint, "MatterReportingAttributeChangeCallback");
    return ESP_ERR_NOT_SUPPORTED;
}

}
