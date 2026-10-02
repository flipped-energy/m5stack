#pragma once

#include <optional>
#include <string>

#include "flipped/core/signals.h"
#include "flipped/core/types.h"

namespace flipped::core::detail {

Fault makeFault(const char *code);
Fault makeFault(const char *code, std::string message);
Fault errorFault(const SnapshotError &error);

template <typename Body>
Fault unloadedFault(const Snapshot<Body> &snapshot)
{
    return snapshot.error ? errorFault(*snapshot.error) : makeFault("not_loaded");
}

struct Selection {
    const Account *account = nullptr;
    std::optional<Fault> fault;
};

Selection selectAccount(const Config &config, const AccountBody &body);

struct NmiChoice {
    std::optional<std::string> nmi;
    std::optional<Fault> fault;
};

NmiChoice chooseNmi(const Config &config, const Account &account, const MetersBody &meters);

AccountGroup accountGroup(Instant instant, const Config &config, const Snapshot<AccountBody> &account,
                          const Selection &selection, const Snapshot<TokensBody> &tokens);

struct TariffResult {
    TariffGroup group;
    std::optional<Instant> planChangeInstant;
};

TariffResult tariffGroup(Instant instant, const Snapshot<AccountBody> &account, const Selection &selection);

PriceGroup priceGroup(Instant instant, const Config &config, const Snapshot<AccountBody> &account,
                      const Selection &selection, const Snapshot<OutlookBody> &outlook);

EnergyGroup energyGroup(const Config &config, const Snapshot<AccountBody> &account, const Selection &selection,
                        const Snapshot<MetersBody> &meters, const Snapshot<UsageBody> &halfHourly,
                        const Snapshot<UsageBody> &daily);

}
