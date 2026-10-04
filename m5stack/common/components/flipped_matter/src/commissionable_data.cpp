#include <array>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include <bootloader_random.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_random.h>
#include <nvs.h>

#include <crypto/CHIPCryptoPAL.h>
#include <lib/support/Span.h>
#include <platform/CommissionableDataProvider.h>
#include <setup_payload/SetupPayload.h>

#include "flipped/matter/ids.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

const char *TAG = "flipped_setup";
constexpr size_t SALT_BYTES = chip::Crypto::kSpake2p_Max_PBKDF_Salt_Length;
constexpr uint16_t DISCRIMINATOR_MASK = 0x0FFF;

struct SetupCode {
    uint32_t passcode = 0;
    uint16_t discriminator = 0;
    std::array<uint8_t, SALT_BYTES> salt{};
};

std::optional<SetupCode> setupCode;

const SetupCode &code()
{
    require(setupCode.has_value(), "the setup code is read before loadSetupCode");
    return *setupCode;
}

esp_err_t present(esp_err_t err, const char *key)
{
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "%s/%s: %s", NVS_NAMESPACE, key, esp_err_to_name(err));
        abort();
    }
    return err;
}

SetupCode generate()
{
    SetupCode generated;
    bootloader_random_enable();
    do {
        uint32_t draw = 0;
        esp_fill_random(&draw, sizeof draw);
        generated.passcode = 1 + draw % chip::kSetupPINCodeMaximumValue;
    } while (!chip::PayloadContents::IsValidSetupPIN(generated.passcode));
    uint16_t discriminator = 0;
    esp_fill_random(&discriminator, sizeof discriminator);
    generated.discriminator = discriminator & DISCRIMINATOR_MASK;
    esp_fill_random(generated.salt.data(), generated.salt.size());
    bootloader_random_disable();
    return generated;
}

class Provider : public chip::DeviceLayer::CommissionableDataProvider {
public:
    CHIP_ERROR GetSetupDiscriminator(uint16_t &setupDiscriminator) override
    {
        setupDiscriminator = code().discriminator;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR SetSetupDiscriminator(uint16_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

    CHIP_ERROR GetSpake2pIterationCount(uint32_t &iterationCount) override
    {
        iterationCount = SPAKE2P_ITERATIONS;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR GetSpake2pSalt(chip::MutableByteSpan &saltBuf) override
    {
        return chip::CopySpanToMutableSpan(chip::ByteSpan(code().salt.data(), code().salt.size()), saltBuf);
    }

    CHIP_ERROR GetSpake2pVerifier(chip::MutableByteSpan &verifierBuf, size_t &outVerifierLen) override
    {
        if (!verifierReady_) {
            chip::Crypto::Spake2pVerifier verifier;
            ReturnErrorOnFailure(verifier.Generate(SPAKE2P_ITERATIONS, chip::ByteSpan(code().salt.data(), code().salt.size()),
                                                   code().passcode));
            chip::MutableByteSpan serialized(verifier_);
            ReturnErrorOnFailure(verifier.Serialize(serialized));
            verifierReady_ = true;
        }
        outVerifierLen = sizeof verifier_;
        return chip::CopySpanToMutableSpan(chip::ByteSpan(verifier_), verifierBuf);
    }

    CHIP_ERROR GetSetupPasscode(uint32_t &setupPasscode) override
    {
        setupPasscode = code().passcode;
        return CHIP_NO_ERROR;
    }

    CHIP_ERROR SetSetupPasscode(uint32_t) override { return CHIP_ERROR_NOT_IMPLEMENTED; }

private:
    chip::Crypto::Spake2pVerifierSerialized verifier_ = {};
    bool verifierReady_ = false;
};

}

void loadSetupCode()
{
    require(!setupCode.has_value(), "loadSetupCode called twice");
    nvs_handle_t handle = 0;
    ESP_ERROR_CHECK(nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle));
    SetupCode stored;
    size_t saltSize = stored.salt.size();
    const bool hasPasscode = present(nvs_get_u32(handle, NVS_PASSCODE, &stored.passcode), NVS_PASSCODE) == ESP_OK;
    const bool hasDiscriminator =
        present(nvs_get_u16(handle, NVS_DISCRIMINATOR, &stored.discriminator), NVS_DISCRIMINATOR) == ESP_OK;
    const bool hasSalt = present(nvs_get_blob(handle, NVS_SALT, stored.salt.data(), &saltSize), NVS_SALT) == ESP_OK;

    if (hasPasscode && hasDiscriminator && hasSalt) {
        nvs_close(handle);
        if (!chip::PayloadContents::IsValidSetupPIN(stored.passcode) || stored.discriminator > DISCRIMINATOR_MASK ||
            saltSize != stored.salt.size()) {
            ESP_LOGE(TAG, "%s holds an invalid setup code: passcode valid %d, discriminator %u, salt %u bytes",
                     NVS_NAMESPACE, chip::PayloadContents::IsValidSetupPIN(stored.passcode), stored.discriminator,
                     static_cast<unsigned>(saltSize));
            abort();
        }
        setupCode = stored;
        return;
    }
    if (hasPasscode || hasDiscriminator || hasSalt) {
        std::string missing;
        const std::array<std::pair<bool, const char *>, 3> keys{{
            {hasPasscode, NVS_PASSCODE},
            {hasDiscriminator, NVS_DISCRIMINATOR},
            {hasSalt, NVS_SALT},
        }};
        for (const auto &[has, key] : keys) {
            if (!has) {
                missing += missing.empty() ? key : std::string(", ") + key;
            }
        }
        ESP_LOGE(TAG, "%s holds part of a setup code; missing: %s", NVS_NAMESPACE, missing.c_str());
        abort();
    }

    const SetupCode generated = generate();
    ESP_ERROR_CHECK(nvs_set_u32(handle, NVS_PASSCODE, generated.passcode));
    ESP_ERROR_CHECK(nvs_set_u16(handle, NVS_DISCRIMINATOR, generated.discriminator));
    ESP_ERROR_CHECK(nvs_set_blob(handle, NVS_SALT, generated.salt.data(), generated.salt.size()));
    ESP_ERROR_CHECK(nvs_commit(handle));
    nvs_close(handle);
    setupCode = generated;
    ESP_LOGW(TAG, "new setup code generated");
}

chip::DeviceLayer::CommissionableDataProvider &commissionableDataProvider()
{
    static Provider provider;
    return provider;
}

}
