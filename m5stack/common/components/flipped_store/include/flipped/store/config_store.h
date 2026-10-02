#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include "nvs.h"

#include "flipped/core/config_check.h"
#include "flipped/core/energy_ledger.h"
#include "flipped/core/parse.h"

namespace flipped::store {

inline constexpr const char *NAMESPACE = "flipped";

struct Settings {
    std::optional<std::string> token;
    std::optional<std::string> accountNumber;
    std::optional<std::string> nmi;
    std::optional<double> priceHighThresholdCentsPerKwh;
    std::optional<double> priceLowThresholdCentsPerKwh;
};

struct TariffMark {
    uint32_t hash = 0;
    int64_t publishedAt = 0;
};

class ConfigStore {
public:
    ConfigStore();
    ~ConfigStore();
    ConfigStore(const ConfigStore &) = delete;
    ConfigStore &operator=(const ConfigStore &) = delete;

    const Settings &settings() const { return settings_; }
    core::ConfigResult setToken(std::string_view token);
    core::ConfigResult setAccountNumber(std::string_view accountNumber);
    core::ConfigResult setNmi(std::string_view nmi);
    core::ConfigResult setThresholds(std::optional<double> high, std::optional<double> low);

    std::optional<std::variant<core::Ledger, core::Invalid>> ledger() const;
    void setLedger(const core::Ledger &ledger);
    std::optional<TariffMark> tariffMark() const;
    void setTariffMark(const TariffMark &mark);
    void eraseAll();

private:
    std::optional<std::string> readString(const char *key) const;
    esp_err_t writeString(const char *key, const std::optional<std::string> &value);
    core::ConfigResult storeString(const char *key, const char *what, std::string_view value,
                                   std::optional<std::string> &field);

    nvs_handle_t handle_ = 0;
    Settings settings_;
};

}
