#include "flipped/store/config_store.h"

#include <array>
#include <cstdlib>
#include <vector>

#include "esp_log.h"

namespace flipped::store {

namespace {

const char *TAG = "flipped_store";

constexpr const char *KEY_TOKEN = "token";
constexpr const char *KEY_ACCOUNT = "account";
constexpr const char *KEY_NMI = "nmi";
constexpr const char *KEY_HIGH = "hi_thr";
constexpr const char *KEY_LOW = "lo_thr";
constexpr const char *KEY_LEDGER = "ledger";
constexpr const char *KEY_TARIFF_HASH = "tariff_h";
constexpr const char *KEY_TARIFF_TIME = "tariff_t";

std::string failure(const char *operation, const char *key, esp_err_t err)
{
    return std::string(operation) + " " + NAMESPACE + "/" + key + ": " + esp_err_to_name(err);
}

std::optional<double> storedThreshold(const char *key, const std::optional<std::string> &text)
{
    if (!text) {
        return std::nullopt;
    }
    const std::optional<double> value = core::parseThreshold(*text);
    if (!value) {
        ESP_LOGE(TAG, "%s/%s holds '%s', which is not a finite decimal number", NAMESPACE, key, text->c_str());
        abort();
    }
    return value;
}

std::optional<std::string> textOf(const std::optional<double> &value)
{
    if (!value) {
        return std::nullopt;
    }
    return core::thresholdText(*value);
}

}

ConfigStore::ConfigStore()
{
    ESP_ERROR_CHECK(nvs_open(NAMESPACE, NVS_READWRITE, &handle_));
    settings_.token = readString(KEY_TOKEN);
    settings_.accountNumber = readString(KEY_ACCOUNT);
    settings_.nmi = readString(KEY_NMI);
    settings_.priceHighThresholdCentsPerKwh = storedThreshold(KEY_HIGH, readString(KEY_HIGH));
    settings_.priceLowThresholdCentsPerKwh = storedThreshold(KEY_LOW, readString(KEY_LOW));
}

ConfigStore::~ConfigStore()
{
    nvs_close(handle_);
}

std::optional<std::string> ConfigStore::readString(const char *key) const
{
    size_t length = 0;
    const esp_err_t err = nvs_get_str(handle_, key, nullptr, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return std::nullopt;
    }
    ESP_ERROR_CHECK(err);
    std::vector<char> text(length);
    ESP_ERROR_CHECK(nvs_get_str(handle_, key, text.data(), &length));
    return std::string(text.data(), length - 1);
}

esp_err_t ConfigStore::writeString(const char *key, const std::optional<std::string> &value)
{
    if (value) {
        return nvs_set_str(handle_, key, value->c_str());
    }
    const esp_err_t err = nvs_erase_key(handle_, key);
    return err == ESP_ERR_NVS_NOT_FOUND ? ESP_OK : err;
}

core::ConfigResult ConfigStore::storeString(const char *key, const char *what, std::string_view value,
                                            std::optional<std::string> &field)
{
    const std::string text(value);
    esp_err_t err = nvs_set_str(handle_, key, text.c_str());
    if (err == ESP_OK) {
        err = nvs_commit(handle_);
        if (err != ESP_OK) {
            return core::ConfigResult{false, failure("nvs_commit", key, err)};
        }
    } else {
        return core::ConfigResult{false, failure("nvs_set_str", key, err)};
    }
    field = text;
    return core::ConfigResult{true, std::string(what) + " stored"};
}

core::ConfigResult ConfigStore::setToken(std::string_view token)
{
    if (std::optional<std::string> refusal = core::tokenRefusal(token)) {
        return core::ConfigResult{false, *refusal};
    }
    return storeString(KEY_TOKEN, "token", token, settings_.token);
}

core::ConfigResult ConfigStore::setAccountNumber(std::string_view accountNumber)
{
    if (std::optional<std::string> refusal = core::accountNumberRefusal(accountNumber)) {
        return core::ConfigResult{false, *refusal};
    }
    return storeString(KEY_ACCOUNT, "account number", accountNumber, settings_.accountNumber);
}

core::ConfigResult ConfigStore::setNmi(std::string_view nmi)
{
    if (std::optional<std::string> refusal = core::nmiRefusal(nmi)) {
        return core::ConfigResult{false, *refusal};
    }
    return storeString(KEY_NMI, "NMI", nmi, settings_.nmi);
}

core::ConfigResult ConfigStore::setThresholds(std::optional<double> high, std::optional<double> low)
{
    if (std::optional<std::string> refusal = core::thresholdsRefusal(high, low)) {
        return core::ConfigResult{false, *refusal};
    }
    const std::optional<std::string> previousHigh = textOf(settings_.priceHighThresholdCentsPerKwh);
    esp_err_t err = writeString(KEY_HIGH, textOf(high));
    if (err != ESP_OK) {
        return core::ConfigResult{false, failure(high ? "nvs_set_str" : "nvs_erase_key", KEY_HIGH, err)};
    }
    err = writeString(KEY_LOW, textOf(low));
    if (err != ESP_OK) {
        ESP_ERROR_CHECK(writeString(KEY_HIGH, previousHigh));
        ESP_ERROR_CHECK(nvs_commit(handle_));
        return core::ConfigResult{false, failure(low ? "nvs_set_str" : "nvs_erase_key", KEY_LOW, err)};
    }
    err = nvs_commit(handle_);
    if (err != ESP_OK) {
        return core::ConfigResult{false, failure("nvs_commit", KEY_LOW, err)};
    }
    settings_.priceHighThresholdCentsPerKwh = high;
    settings_.priceLowThresholdCentsPerKwh = low;
    return core::ConfigResult{true, "thresholds stored"};
}

std::optional<std::variant<core::Ledger, core::Invalid>> ConfigStore::ledger() const
{
    size_t length = 0;
    const esp_err_t err = nvs_get_blob(handle_, KEY_LEDGER, nullptr, &length);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return std::nullopt;
    }
    ESP_ERROR_CHECK(err);
    std::vector<uint8_t> blob(length);
    ESP_ERROR_CHECK(nvs_get_blob(handle_, KEY_LEDGER, blob.data(), &length));
    return core::decodeLedger(blob.data(), length);
}

void ConfigStore::setLedger(const core::Ledger &ledger)
{
    const std::array<uint8_t, core::LEDGER_BLOB_BYTES> blob = core::encodeLedger(ledger);
    ESP_ERROR_CHECK(nvs_set_blob(handle_, KEY_LEDGER, blob.data(), blob.size()));
    ESP_ERROR_CHECK(nvs_commit(handle_));
}

std::optional<TariffMark> ConfigStore::tariffMark() const
{
    TariffMark mark;
    const esp_err_t hashErr = nvs_get_u32(handle_, KEY_TARIFF_HASH, &mark.hash);
    const esp_err_t timeErr = nvs_get_i64(handle_, KEY_TARIFF_TIME, &mark.publishedAt);
    if (hashErr == ESP_ERR_NVS_NOT_FOUND && timeErr == ESP_ERR_NVS_NOT_FOUND) {
        return std::nullopt;
    }
    if (hashErr == ESP_ERR_NVS_NOT_FOUND || timeErr == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "%s/%s is %s but %s/%s is %s", NAMESPACE, KEY_TARIFF_HASH, esp_err_to_name(hashErr), NAMESPACE,
                 KEY_TARIFF_TIME, esp_err_to_name(timeErr));
        abort();
    }
    ESP_ERROR_CHECK(hashErr);
    ESP_ERROR_CHECK(timeErr);
    return mark;
}

void ConfigStore::setTariffMark(const TariffMark &mark)
{
    ESP_ERROR_CHECK(nvs_set_u32(handle_, KEY_TARIFF_HASH, mark.hash));
    ESP_ERROR_CHECK(nvs_set_i64(handle_, KEY_TARIFF_TIME, mark.publishedAt));
    ESP_ERROR_CHECK(nvs_commit(handle_));
}

void ConfigStore::eraseAll()
{
    ESP_ERROR_CHECK(nvs_erase_all(handle_));
    ESP_ERROR_CHECK(nvs_commit(handle_));
    settings_ = Settings{};
}

}
