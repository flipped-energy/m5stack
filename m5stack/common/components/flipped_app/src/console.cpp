#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>

#include "esp_err.h"
#include "esp_matter_console.h"

#include "flipped/app/app.h"
#include "flipped/core/config_check.h"
#include "flipped/core/time.h"

namespace flipped::app {

namespace {

const char *USAGE = "usage: matter esp flipped status | token <value> | account [<number>] | nmi [<nmi>] | "
                    "thresholds <high|off> <low|off> | refresh | pair | unpair | erase";

const char *yesNo(bool value)
{
    return value ? "yes" : "no";
}

std::string text(const std::optional<std::string> &value)
{
    return value ? *value : std::string("none");
}

std::string instantText(const std::optional<core::Instant> &value)
{
    return value ? core::formatInstant(*value) : std::string("none");
}

void printFault(const char *group, const std::optional<core::Fault> &fault)
{
    if (!fault) {
        printf("%s group ok\n", group);
        return;
    }
    printf("%s group faulted: code %s", group, fault->code.c_str());
    if (fault->httpStatus) {
        printf(", http %d", *fault->httpStatus);
    }
    if (fault->bodyBytes) {
        printf(", body bytes %u", static_cast<unsigned>(*fault->bodyBytes));
    }
    if (fault->body) {
        printf(", kept bytes %u", static_cast<unsigned>(fault->body->size()));
    }
    printf("\n");
    if (fault->message) {
        printf("%s message: %s\n", group, fault->message->c_str());
    }
    if (fault->body) {
        printf("%s body: ", group);
        fwrite(fault->body->data(), 1, fault->body->size(), stdout);
        printf("\n");
    }
}

void printResult(const core::ConfigResult &result)
{
    printf("%s\n", result.text.c_str());
}

esp_err_t status()
{
    std::unique_ptr<UiModel> model(new UiModel(snapshot()));
    printf("fabrics %u, commissioning window %s, BLE %s\n", static_cast<unsigned>(model->fabricCount),
           model->commissioningWindowOpen ? "open" : "closed", model->bleAvailable ? "available" : "released");
    printf("wifi provisioned %s, connected %s, ssid %s, rssi %d, ipv4 %s\n", yesNo(model->wifiProvisioned),
           yesNo(model->wifiConnected), model->wifi.ssid.c_str(), static_cast<int>(model->wifi.rssi),
           model->wifi.ipv4.c_str());
    const std::optional<core::Instant> clock = now();
    printf("clock %s %s\n", clock ? "synced" : "unsynced", instantText(clock).c_str());
    const core::AccountGroup &account = model->signals.account;
    if (model->hasToken) {
        printf("token %s, expires %s, scope %s, rejected %s\n", model->tokenPreview.c_str(),
               instantText(account.tokenExpiresAt).c_str(), text(account.tokenScope).c_str(),
               yesNo(model->tokenRejected));
    } else {
        printf("token none\n");
    }
    printf("account %s, state %s, product %s, region %s, time zone %s\n", text(account.accountNumber).c_str(),
           text(account.accountState).c_str(), text(account.productName).c_str(), text(account.region).c_str(),
           text(account.timeZone).c_str());
    printFault("account", account.fault);
    printFault("tariff", model->signals.tariff.fault);
    printFault("price", model->signals.price.fault);
    printFault("energy", model->signals.energy.fault);
    if (model->dailyLimitRemaining) {
        printf("daily limit remaining %u\n", static_cast<unsigned>(*model->dailyLimitRemaining));
    }
    printf("firmware %s\n", model->firmwareVersion.c_str());
    return ESP_OK;
}

esp_err_t accounts()
{
    std::unique_ptr<UiModel> model(new UiModel(snapshot()));
    printf("eligible accounts %u\n", static_cast<unsigned>(model->eligibleAccounts.size()));
    for (const AccountChoice &choice : model->eligibleAccounts) {
        printf("%s  %s\n", choice.accountNumber.c_str(), choice.siteAddress.c_str());
    }
    return ESP_OK;
}

esp_err_t nmis()
{
    std::unique_ptr<UiModel> model(new UiModel(snapshot()));
    printf("candidate NMIs %u\n", static_cast<unsigned>(model->candidateNmis.size()));
    for (const std::string &nmi : model->candidateNmis) {
        printf("%s\n", nmi.c_str());
    }
    return ESP_OK;
}

bool thresholdArgument(const char *argument, std::optional<double> &value)
{
    if (std::strcmp(argument, "off") == 0) {
        value.reset();
        return true;
    }
    value = core::parseThreshold(argument);
    if (!value) {
        printf("%s is neither a finite number nor off\n", argument);
        return false;
    }
    return true;
}

esp_err_t pair()
{
    openCommissioningWindow();
    std::unique_ptr<UiModel> model(new UiModel(snapshot()));
    printf("QR %s\nmanual %s\n", model->qrPayload.c_str(), model->manualCode.c_str());
    return ESP_OK;
}

esp_err_t command(int argc, char **argv)
{
    if (argc == 1 && std::strcmp(argv[0], "status") == 0) {
        return status();
    }
    if (argc == 2 && std::strcmp(argv[0], "token") == 0) {
        printResult(setToken(argv[1]));
        return ESP_OK;
    }
    if (argc == 1 && std::strcmp(argv[0], "account") == 0) {
        return accounts();
    }
    if (argc == 2 && std::strcmp(argv[0], "account") == 0) {
        printResult(setAccountNumber(argv[1]));
        return ESP_OK;
    }
    if (argc == 1 && std::strcmp(argv[0], "nmi") == 0) {
        return nmis();
    }
    if (argc == 2 && std::strcmp(argv[0], "nmi") == 0) {
        printResult(setNmi(argv[1]));
        return ESP_OK;
    }
    if (argc == 3 && std::strcmp(argv[0], "thresholds") == 0) {
        std::optional<double> high;
        std::optional<double> low;
        if (!thresholdArgument(argv[1], high) || !thresholdArgument(argv[2], low)) {
            return ESP_ERR_INVALID_ARG;
        }
        printResult(setThresholds(high, low));
        return ESP_OK;
    }
    if (argc == 1 && std::strcmp(argv[0], "refresh") == 0) {
        requestUsageRefresh();
        return ESP_OK;
    }
    if (argc == 1 && std::strcmp(argv[0], "pair") == 0) {
        return pair();
    }
    if (argc == 1 && std::strcmp(argv[0], "unpair") == 0) {
        unpair();
        return ESP_OK;
    }
    if (argc == 1 && std::strcmp(argv[0], "erase") == 0) {
        eraseEverything();
        return ESP_OK;
    }
    printf("%s\n", USAGE);
    return ESP_ERR_INVALID_ARG;
}

}

void startConsole()
{
    static const esp_matter::console::command_t flipped = {
        "flipped", "Flipped Energy: status, token, account, nmi, thresholds, refresh, pair, unpair, erase", command};
    ESP_ERROR_CHECK(esp_matter::console::add_commands(&flipped, 1));
    ESP_ERROR_CHECK(esp_matter::console::wifi_register_commands());
    ESP_ERROR_CHECK(esp_matter::console::init());
}

}
