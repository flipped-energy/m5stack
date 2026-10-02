#include "flipped/net/sntp.h"

#include <cstdlib>

#include "esp_log.h"
#include "esp_netif_sntp.h"

namespace flipped::net {

namespace {

const char *TAG = "flipped_sntp";
void (*synced)() = nullptr;

void onSync(struct timeval *)
{
    synced();
}

}

void startSntp(void (*onSynced)())
{
    if (synced != nullptr) {
        ESP_LOGE(TAG, "SNTP started twice");
        abort();
    }
    synced = onSynced;
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    config.sync_cb = onSync;
    ESP_ERROR_CHECK(esp_netif_sntp_init(&config));
    ESP_LOGI(TAG, "SNTP started with pool.ntp.org");
}

}
