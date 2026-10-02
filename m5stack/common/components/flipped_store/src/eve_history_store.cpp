#include "flipped/store/eve_history_store.h"

#include <vector>

#include "esp_err.h"
#include "nvs.h"

#include "flipped/store/config_store.h"

namespace flipped::store {

std::optional<std::variant<core::StoredEveHistory, core::Invalid>> readEveHistory(uint16_t memorySize)
{
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &handle));
    size_t length = 0;
    const esp_err_t err = nvs_get_blob(handle, KEY_EVE_HISTORY, nullptr, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(handle);
        return std::nullopt;
    }
    ESP_ERROR_CHECK(err);
    std::vector<uint8_t> blob(length);
    ESP_ERROR_CHECK(nvs_get_blob(handle, KEY_EVE_HISTORY, blob.data(), &length));
    nvs_close(handle);
    return core::decodeEveHistory(blob.data(), length, memorySize);
}

void writeEveHistory(const std::string &h, const core::EveHistoryState &state)
{
    const std::vector<uint8_t> blob = core::encodeEveHistory(h, state);
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &handle));
    ESP_ERROR_CHECK(nvs_set_blob(handle, KEY_EVE_HISTORY, blob.data(), blob.size()));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
}

}
