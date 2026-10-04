#include <cinttypes>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>

#include <app-common/zap-generated/cluster-objects.h>
#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/reporting/reporting.h>
#include <protocols/interaction_model/StatusCode.h>

#include "flipped/core/eve_history.h"
#include "flipped/core/identity.h"
#include "flipped/matter/ids.h"
#include "flipped/store/eve_history_store.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

namespace Clusters = chip::app::Clusters;
using chip::app::AttributeValueDecoder;
using chip::app::AttributeValueEncoder;

const char *TAG = "flipped_eve";
constexpr double MWH_PER_KWH = 1000000.0;

struct Carried {
    std::optional<std::string> h;
    core::EveHistory history{core::EVE_MEMORY_SIZE};
    std::optional<float> totalKwh;
    std::optional<float> averageWatts;
};

Carried *carried = nullptr;
std::optional<uint16_t> endpointId;

void check(esp_err_t err, const char *what)
{
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s: %s", what, esp_err_to_name(err));
        abort();
    }
}

void createAttribute(esp_matter::cluster_t *cluster, uint32_t id, uint16_t flags, esp_matter_attr_val_t initial,
                     uint16_t maxBytes)
{
    if (esp_matter::attribute::create(cluster, id, flags, initial, maxBytes) == nullptr) {
        ESP_LOGE(TAG, "attribute::create 0x%08" PRIX32 " of the Eve history cluster returned null", id);
        abort();
    }
}

void reportChange(uint32_t attribute)
{
    MatterReportingAttributeChangeCallback(static_cast<chip::EndpointId>(*endpointId), EVE_HISTORY_CLUSTER_ID,
                                           attribute);
}

class EveServer : public chip::app::AttributeAccessInterface {
public:
    explicit EveServer(uint16_t endpoint)
        : AttributeAccessInterface(chip::MakeOptional(chip::EndpointId(endpoint)), EVE_HISTORY_CLUSTER_ID)
    {
    }

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath &path, AttributeValueEncoder &encoder) override
    {
        switch (path.mAttributeId) {
        case eve::HISTORY_STATUS: {
            const std::optional<std::vector<uint8_t>> status = carried->history.historyStatus();
            return status ? encoder.Encode(chip::ByteSpan(status->data(), status->size())) : encoder.EncodeNull();
        }
        case eve::HISTORY_ENTRIES: {
            const core::EveCursor cursor = carried->history.cursor();
            const std::vector<uint8_t> bytes = carried->history.historyEntries();
            const CHIP_ERROR err = encoder.Encode(chip::ByteSpan(bytes.data(), bytes.size()));
            if (err != CHIP_NO_ERROR) {
                carried->history.restore(cursor);
            }
            return err;
        }
        case eve::TOTAL_CONSUMPTION:
            return carried->totalKwh ? encoder.Encode(*carried->totalKwh) : encoder.EncodeNull();
        case eve::POWER_CONSUMPTION:
            return carried->averageWatts ? encoder.Encode(*carried->averageWatts) : encoder.EncodeNull();
        default:
            return CHIP_NO_ERROR;
        }
    }

    CHIP_ERROR Write(const chip::app::ConcreteDataAttributePath &path, AttributeValueDecoder &decoder) override
    {
        chip::ByteSpan bytes;
        switch (path.mAttributeId) {
        case eve::HISTORY_REQUEST: {
            ReturnErrorOnFailure(decoder.Decode(bytes));
            if (const std::optional<core::Invalid> invalid = carried->history.handleRequest(bytes.data(), bytes.size())) {
                ESP_LOGE(TAG, "%s", invalid->message.c_str());
                return CHIP_IM_GLOBAL_STATUS(ConstraintError);
            }
            return CHIP_NO_ERROR;
        }
        case eve::HISTORY_SET_TIME:
            return decoder.Decode(bytes);
        default:
            return CHIP_NO_ERROR;
        }
    }
};

EveServer *server = nullptr;

Carried load(const std::optional<std::string> &instanceKey)
{
    Carried loaded;
    if (instanceKey) {
        loaded.h = core::instanceHash(*instanceKey);
    }
    std::optional<std::variant<core::StoredEveHistory, core::Invalid>> stored =
        store::readEveHistory(core::EVE_MEMORY_SIZE);
    if (!stored) {
        return loaded;
    }
    if (const core::Invalid *invalid = std::get_if<core::Invalid>(&*stored)) {
        ESP_LOGE(TAG, "flipped/%s: %s", store::KEY_EVE_HISTORY, invalid->message.c_str());
        abort();
    }
    core::StoredEveHistory &history = std::get<core::StoredEveHistory>(*stored);
    if (!loaded.h || history.h != *loaded.h) {
        ESP_LOGW(TAG, "the stored Eve history belongs to instance %s, the current instance is %s: it is not served",
                 history.h.c_str(), loaded.h ? loaded.h->c_str() : "unknown");
        return loaded;
    }
    loaded.history = core::EveHistory(std::move(history.state));
    ESP_LOGI(TAG, "Eve history of instance %s restored: last entry %" PRIu32 ", %u slots used", loaded.h->c_str(),
             loaded.history.state().lastEntry, static_cast<unsigned>(loaded.history.state().usedMemory));
    return loaded;
}

uint16_t build(std::optional<uint16_t> stored)
{
    esp_matter::endpoint_t *endpoint =
        stored ? esp_matter::endpoint::resume(node(), esp_matter::ENDPOINT_FLAG_NONE, *stored, nullptr)
               : esp_matter::endpoint::create(node(), esp_matter::ENDPOINT_FLAG_NONE, nullptr);
    require(endpoint != nullptr, stored ? "endpoint::resume of the Energy History endpoint returned null"
                                        : "endpoint::create of the Energy History endpoint returned null");
    const uint16_t id = esp_matter::endpoint::get_id(endpoint);

    esp_matter::endpoint::on_off_plug_in_unit::config_t plug;
    plug.identify.identify_type = chip::to_underlying(Clusters::Identify::IdentifyTypeEnum::kDisplay);
    plug.on_off_lighting.start_up_on_off = nullable<uint8_t>();
    require(esp_matter::cluster::descriptor::create(endpoint, &plug.descriptor, esp_matter::CLUSTER_FLAG_SERVER) != nullptr,
            "descriptor::create on Energy History returned null");
    check(esp_matter::endpoint::on_off_plug_in_unit::add(endpoint, &plug), "on_off_plug_in_unit::add of Energy History");

    esp_matter::cluster_t *cluster =
        esp_matter::cluster::create(endpoint, EVE_HISTORY_CLUSTER_ID, esp_matter::CLUSTER_FLAG_SERVER);
    require(cluster != nullptr, "cluster::create of the Eve history cluster 0x130AFC01 returned null");
    require(esp_matter::cluster::global::attribute::create_feature_map(cluster, 0) != nullptr,
            "create_feature_map of the Eve history cluster returned null");
    require(esp_matter::cluster::global::attribute::create_cluster_revision(cluster, EVE_HISTORY_CLUSTER_REVISION) !=
                nullptr,
            "create_cluster_revision of the Eve history cluster returned null");
    createAttribute(cluster, eve::HISTORY_STATUS, esp_matter::ATTRIBUTE_FLAG_NULLABLE, esp_matter_octet_str(nullptr, 0),
                    static_cast<uint16_t>(core::EVE_STATUS_BYTES));
    createAttribute(cluster, eve::HISTORY_ENTRIES, esp_matter::ATTRIBUTE_FLAG_NONE, esp_matter_octet_str(nullptr, 0),
                    static_cast<uint16_t>(core::EVE_ENTRIES_MAX_BYTES));
    createAttribute(cluster, eve::HISTORY_REQUEST, esp_matter::ATTRIBUTE_FLAG_WRITABLE, esp_matter_octet_str(nullptr, 0),
                    eve::WRITE_MAX_BYTES);
    createAttribute(cluster, eve::HISTORY_SET_TIME, esp_matter::ATTRIBUTE_FLAG_WRITABLE, esp_matter_octet_str(nullptr, 0),
                    eve::WRITE_MAX_BYTES);
    createAttribute(cluster, eve::TOTAL_CONSUMPTION, esp_matter::ATTRIBUTE_FLAG_NULLABLE,
                    esp_matter_nullable_float(nullable<float>()), 0);
    createAttribute(cluster, eve::POWER_CONSUMPTION, esp_matter::ATTRIBUTE_FLAG_NULLABLE,
                    esp_matter_nullable_float(nullable<float>()), 0);

    for (const uint32_t required : {Clusters::Descriptor::Id, Clusters::Identify::Id, Clusters::OnOff::Id}) {
        if (esp_matter::cluster::get(endpoint, required) == nullptr) {
            ESP_LOGE(TAG, "the Energy History endpoint %u has no cluster 0x%04" PRIX32, id, required);
            abort();
        }
    }
    check(esp_matter::endpoint::enable(endpoint), "endpoint::enable of Energy History");
    return id;
}

}

void restoreEveHistory(const std::optional<std::string> &instanceKey)
{
    require(carried == nullptr, "restoreEveHistory called twice");
    carried = new Carried(load(instanceKey));
    const std::optional<uint16_t> stored = storedEndpointId(NVS_EVE_ENDPOINT);
    {
        esp_matter::lock::ScopedChipStackLock lock(portMAX_DELAY);
        endpointId = build(stored);
        server = new EveServer(*endpointId);
        require(chip::app::AttributeAccessInterfaceRegistry::Instance().Register(server),
                "AttributeAccessInterfaceRegistry::Register for the Eve history cluster returned false");
    }
    if (!stored) {
        storeEndpointId(NVS_EVE_ENDPOINT, *endpointId);
    }
    ESP_LOGI(TAG, "%s Energy History endpoint %u with the Eve history cluster", stored ? "resumed" : "created",
             *endpointId);
    refreshDeviceVisibility();
}

std::optional<uint16_t> eveHistoryEndpoint()
{
    return endpointId;
}

EvePayload evePayload(const Update &update)
{
    EvePayload payload;
    if (!update.instanceKey) {
        return payload;
    }
    payload.h = core::instanceHash(*update.instanceKey);
    if (update.ledger && update.ledger->h == *payload.h) {
        payload.importedMwh = update.ledger->importedMwh;
    }
    if (!update.signals.energy.fault && !update.signals.energy.intervals.empty()) {
        const auto &latest = update.signals.energy.intervals.back();
        payload.averageWatts = static_cast<float>(latest.gridImportKwh * 60000.0 / latest.durationMinutes);
    }
    if (update.ledgerStep && update.ledgerStep->took > 0) {
        require(update.ledgerStep->firstStart.has_value(), "a ledger step that took intervals has no firstStart");
        for (const core::EnergyEntry &interval : update.signals.energy.intervals) {
            if (interval.start >= *update.ledgerStep->firstStart) {
                payload.fresh.push_back(interval);
            }
        }
    }
    return payload;
}

void applyEveHistory(EvePayload payload)
{
    require(carried != nullptr, "an Eve history update arrived before restoreEveHistory");
    if (!payload.h) {
        return;
    }
    if (carried->h != payload.h) {
        ESP_LOGW(TAG, "instance %s replaces %s: the Eve history starts empty", payload.h->c_str(),
                 carried->h ? carried->h->c_str() : "none");
        carried->h = payload.h;
        carried->history = core::EveHistory(core::EVE_MEMORY_SIZE);
        carried->totalKwh.reset();
        reportChange(eve::HISTORY_STATUS);
        reportChange(eve::TOTAL_CONSUMPTION);
    }
    if (!payload.fresh.empty()) {
        const core::EveEntries added = carried->history.addIntervals(payload.fresh, core::importedKwh);
        if (!added.overflow.empty()) {
            double kwh = 0;
            for (const core::EveOverflow &interval : added.overflow) {
                kwh += interval.kwh;
            }
            ESP_LOGW(TAG, "%u intervals above 6553.5 W average left out of the Eve history, %.3f kWh",
                     static_cast<unsigned>(added.overflow.size()), kwh);
        }
        if (!added.entries.empty()) {
            store::writeEveHistory(*carried->h, carried->history.state());
            reportChange(eve::HISTORY_STATUS);
        }
    }
    const std::optional<float> totalKwh =
        payload.importedMwh ? std::optional<float>(static_cast<float>(*payload.importedMwh / MWH_PER_KWH)) : std::nullopt;
    if (totalKwh != carried->totalKwh) {
        carried->totalKwh = totalKwh;
        reportChange(eve::TOTAL_CONSUMPTION);
    }
    if (payload.averageWatts != carried->averageWatts) {
        carried->averageWatts = payload.averageWatts;
        reportChange(eve::POWER_CONSUMPTION);
    }
}

}
