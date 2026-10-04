#include <string>
#include <utility>
#include <vector>

#include "flipped/core/constants.h"
#include "flipped/core/time.h"
#include "groups.h"

namespace flipped::core::detail {

Fault makeFault(const char *code)
{
    Fault fault;
    fault.code = code;
    return fault;
}

Fault makeFault(const char *code, std::string message)
{
    Fault fault;
    fault.code = code;
    fault.message = std::move(message);
    return fault;
}

Fault errorFault(const SnapshotError &error)
{
    Fault fault;
    switch (error.kind) {
    case ErrorKind::http:
        fault.code = "http_error";
        fault.httpStatus = error.httpStatus;
        fault.body = error.body;
        fault.bodyBytes = error.bodyBytes;
        break;
    case ErrorKind::network:
        fault.code = "network_error";
        fault.message = error.message;
        break;
    case ErrorKind::invalid:
        fault.code = "invalid_response";
        fault.message = error.message;
        break;
    }
    return fault;
}

Selection selectAccount(const Config &config, const AccountBody &body)
{
    Selection selection;
    if (body.accounts.presence == Presence::wrongType) {
        selection.fault = makeFault("invalid_response", "accounts is not an array");
        return selection;
    }
    std::vector<const Account *> eligible;
    if (body.accounts.present()) {
        for (const Account &account : body.accounts.value) {
            if (account.accountNumber.present() && !account.accountNumber.value.empty() &&
                account.product.present()) {
                eligible.push_back(&account);
            }
        }
    }
    if (eligible.empty()) {
        selection.fault = makeFault("account_none");
        return selection;
    }
    if (config.accountNumber) {
        for (const Account *account : eligible) {
            if (account->accountNumber.value == *config.accountNumber) {
                selection.account = account;
                return selection;
            }
        }
        selection.fault = makeFault("account_not_found", "account " + *config.accountNumber + " is not in the response");
        return selection;
    }
    if (eligible.size() == 1) {
        selection.account = eligible.front();
        return selection;
    }
    std::string numbers;
    for (const Account *account : eligible) {
        numbers += numbers.empty() ? "" : ", ";
        numbers += account->accountNumber.value;
    }
    selection.fault = makeFault("account_selection_required", "eligible accounts: " + numbers);
    return selection;
}

NmiChoice chooseNmi(const Config &config, const Account &account, const MetersBody &meters)
{
    NmiChoice choice;
    if (!meters.meters.present()) {
        choice.fault = makeFault("invalid_response", "meters is missing or not an array");
        return choice;
    }
    for (const Meter &meter : meters.meters.value) {
        if (!meter.nmi.present() || !meter.address.present()) {
            choice.fault = makeFault("invalid_response", "meters[].nmi or meters[].address is missing or not a string");
            return choice;
        }
    }
    if (config.nmi) {
        for (const Meter &meter : meters.meters.value) {
            if (meter.nmi.value == *config.nmi) {
                choice.nmi = *config.nmi;
                return choice;
            }
        }
        choice.fault = makeFault("nmi_not_found", "nmi " + *config.nmi + " is not in the meters response");
        return choice;
    }
    if (!account.siteAddress.present()) {
        choice.fault = makeFault("invalid_response", "siteAddress is missing or not a string");
        return choice;
    }
    std::vector<std::string> candidates;
    for (const Meter &meter : meters.meters.value) {
        if (meter.address.value == account.siteAddress.value) {
            bool seen = false;
            for (const std::string &candidate : candidates) {
                seen = seen || candidate == meter.nmi.value;
            }
            if (!seen) {
                candidates.push_back(meter.nmi.value);
            }
        }
    }
    if (candidates.size() == 1) {
        choice.nmi = candidates.front();
        return choice;
    }
    std::vector<std::string> listed = candidates;
    if (listed.empty()) {
        for (const Meter &meter : meters.meters.value) {
            bool seen = false;
            for (const std::string &entry : listed) {
                seen = seen || entry == meter.nmi.value;
            }
            if (!seen) {
                listed.push_back(meter.nmi.value);
            }
        }
    }
    std::string text;
    for (const std::string &entry : listed) {
        text += text.empty() ? "" : ", ";
        text += entry;
    }
    choice.fault = makeFault("nmi_selection_required", "nmi candidates: " + text);
    return choice;
}

AccountGroup accountGroup(Instant instant, const Config &config, const Snapshot<AccountBody> &account,
                          const Selection &selection, const Snapshot<TokensBody> &tokens)
{
    AccountGroup group;
    if (!account.body) {
        group.fault = unloadedFault(account);
        return group;
    }
    if (selection.fault) {
        group.fault = selection.fault;
        return group;
    }
    const Account &selected = *selection.account;
    const Product &product = selected.product.value;
    std::optional<Instant> expiresAt;
    std::optional<std::string> scope;
    if (tokens.body && config.tokenPreview) {
        if (!tokens.body->tokens.present()) {
            group.fault = makeFault("invalid_response", "tokens is missing or not an array");
            return group;
        }
        for (const Token &token : tokens.body->tokens.value) {
            if (token.tokenPreview.present() && token.tokenPreview.value == *config.tokenPreview) {
                const InstantText parsed =
                    token.expiresAt.present() ? parseInstant(token.expiresAt.value) : InstantText{};
                if (!parsed.instant) {
                    group.fault = makeFault("invalid_response", "tokens[].expiresAt " +
                                                                    (token.expiresAt.present() ? token.expiresAt.value
                                                                                               : std::string("(absent)")) +
                                                                    " is not a date-time with a UTC offset");
                    return group;
                }
                expiresAt = parsed.instant;
                if (token.scope.present()) {
                    scope = token.scope.value;
                }
                break;
            }
        }
    }
    group.accountNumber = selected.accountNumber.value;
    if (selected.accountState.present()) {
        group.accountState = selected.accountState.value;
    }
    if (selected.productName.present()) {
        group.productName = selected.productName.value;
    }
    if (product.gridType.present()) {
        group.region = product.gridType.value;
    }
    if (product.timeZone.present()) {
        group.timeZone = product.timeZone.value;
    }
    if (expiresAt) {
        group.tokenExpiresAt = expiresAt;
        group.tokenScope = scope;
        group.tokenExpiringSoon = *expiresAt - instant < TOKEN_EXPIRY_WARNING_DAYS * 86400;
    }
    return group;
}

}
