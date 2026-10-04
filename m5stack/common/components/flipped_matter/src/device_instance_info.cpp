#include <cstdint>
#include <cstdio>

#include <esp_err.h>
#include <esp_log.h>
#include <esp_mac.h>

#include <platform/CHIPDeviceLayer.h>
#include <platform/ESP32/ESP32Config.h>
#include <platform/internal/GenericDeviceInstanceInfoProvider.ipp>

#include "matter_internal.h"
#include "flipped/matter/ids.h"

namespace flipped::matter {

namespace {

const char *TAG = "flipped_instance";

using Generic = chip::DeviceLayer::Internal::GenericDeviceInstanceInfoProvider<chip::DeviceLayer::Internal::ESP32Config>;

class InstanceInfo : public Generic {
public:
    InstanceInfo() : Generic(chip::DeviceLayer::ConfigurationManagerImpl::GetDefaultInstance()) {}

    CHIP_ERROR GetVendorName(char *buf, size_t bufSize) override
    {
        return name(buf, bufSize);
    }

    CHIP_ERROR GetProductName(char *buf, size_t bufSize) override
    {
        return name(buf, bufSize);
    }

    CHIP_ERROR GetSerialNumber(char *buf, size_t bufSize) override
    {
        uint8_t mac[6] = {};
        const esp_err_t err = esp_efuse_mac_get_default(mac);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_efuse_mac_get_default: %s", esp_err_to_name(err));
            return CHIP_ERROR_INTERNAL;
        }
        const int written = snprintf(buf, bufSize, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        if (written < 0 || static_cast<size_t>(written) >= bufSize) {
            return CHIP_ERROR_BUFFER_TOO_SMALL;
        }
        return CHIP_NO_ERROR;
    }

private:
    static CHIP_ERROR name(char *buf, size_t bufSize)
    {
        const int written = snprintf(buf, bufSize, "%s", VENDOR_NAME);
        return written < 0 || static_cast<size_t>(written) >= bufSize ? CHIP_ERROR_BUFFER_TOO_SMALL : CHIP_NO_ERROR;
    }
};

}

chip::DeviceLayer::DeviceInstanceInfoProvider &deviceInstanceInfoProvider()
{
    static InstanceInfo provider;
    return provider;
}

}
