#include "flipped/dev_wifi.h"

#include <cstring>
#include <string>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_wifi.h"

#if __has_include("dev_wifi_networks.h")
#include "dev_wifi_networks.h"
#define FLIPPED_HAS_DEV_WIFI 1
#endif

namespace flipped::devwifi {

#ifdef FLIPPED_HAS_DEV_WIFI

namespace {

constexpr const char* kTag = "dev_wifi";
constexpr int kMissesBeforeSwitch = 2;
constexpr size_t kCount = sizeof(kDevNetworks) / sizeof(kDevNetworks[0]);

int misses = 0;

std::string storedNetwork()
{
    wifi_config_t config = {};
    ESP_ERROR_CHECK(esp_wifi_get_config(WIFI_IF_STA, &config));
    return std::string(reinterpret_cast<const char*>(config.sta.ssid));
}

void use(const DevNetwork& network)
{
    wifi_config_t config = {};
    std::strncpy(reinterpret_cast<char*>(config.sta.ssid), network.ssid, sizeof(config.sta.ssid));
    std::strncpy(reinterpret_cast<char*>(config.sta.password), network.password, sizeof(config.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &config));
    ESP_LOGI(kTag, "switching to %s", network.ssid);
    esp_err_t rc = esp_wifi_connect();
    if (rc != ESP_OK) {
        ESP_LOGW(kTag, "esp_wifi_connect: %s", esp_err_to_name(rc));
    }
}

void next()
{
    const std::string current = storedNetwork();
    size_t index = 0;
    for (size_t i = 0; i < kCount; ++i) {
        if (current == kDevNetworks[i].ssid) {
            index = (i + 1) % kCount;
        }
    }
    use(kDevNetworks[index]);
}

void onEvent(void*, esp_event_base_t base, int32_t id, void* data)
{
    if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        misses = 0;
        return;
    }
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const auto* event = static_cast<const wifi_event_sta_disconnected_t*>(data);
        if (event->reason != WIFI_REASON_NO_AP_FOUND) {
            return;
        }
        ++misses;
        ESP_LOGI(kTag, "%s not found (%d)", storedNetwork().c_str(), misses);
        if (misses >= kMissesBeforeSwitch) {
            misses = 0;
            next();
        }
    }
}

}

void begin()
{
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, onEvent, nullptr));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onEvent, nullptr));
    if (storedNetwork().empty()) {
        use(kDevNetworks[0]);
    }
}

#else

void begin() {}

#endif

}
