#include <string>
#include <utility>

#include <esp_log.h>

#include <lib/core/ErrorStr.h>
#include <setup_payload/OnboardingCodesUtil.h>
#include <setup_payload/QRCodeSetupPayloadGenerator.h>
#include <setup_payload/SetupPayload.h>

#include "matter_internal.h"

namespace flipped::matter {

namespace {

const char *TAG = "flipped_onboarding";

}

std::pair<std::string, std::string> onboardingCodes(bool ble, bool onNetwork)
{
    chip::RendezvousInformationFlags rendezvous;
    if (ble) {
        rendezvous.Set(chip::RendezvousInformationFlag::kBLE);
    }
    if (onNetwork) {
        rendezvous.Set(chip::RendezvousInformationFlag::kOnNetwork);
    }
    if (!rendezvous.HasAny()) {
        ESP_LOGI(TAG, "no onboarding code: BLE is released and no Wi-Fi network is stored");
        return {};
    }
    std::pair<std::string, std::string> codes;
    char qr[chip::QRCodeBasicSetupPayloadGenerator::kMaxQRCodeBase38RepresentationLength + 1] = {};
    chip::MutableCharSpan qrSpan(qr);
    CHIP_ERROR err = GetQRCode(qrSpan, rendezvous);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "GetQRCode: %s", chip::ErrorStr(err));
    } else {
        codes.first.assign(qrSpan.data(), qrSpan.size());
    }
    char manual[chip::kManualSetupLongCodeCharLength + 1] = {};
    chip::MutableCharSpan manualSpan(manual);
    err = GetManualPairingCode(manualSpan, rendezvous);
    if (err != CHIP_NO_ERROR) {
        ESP_LOGE(TAG, "GetManualPairingCode: %s", chip::ErrorStr(err));
    } else {
        codes.second.assign(manualSpan.data(), manualSpan.size());
    }
    ESP_LOGI(TAG, "onboarding codes (%s%s): QR %s, manual %s", ble ? "BLE" : "",
             ble && onNetwork ? " and on-network" : (onNetwork ? "on-network" : ""), codes.first.c_str(),
             codes.second.c_str());
    return codes;
}

}
