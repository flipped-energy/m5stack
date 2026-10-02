#include "flipped/core/compute_signals.h"

#include <cstdio>
#include <cstdlib>

#include "flipped/core/constants.h"
#include "groups.h"

namespace flipped::core {

namespace {

template <typename Body>
void requireFetchedAt(const Snapshot<Body> &snapshot, const char *name)
{
    if (snapshot.body && !snapshot.fetchedAt) {
        std::fprintf(stderr, "%s snapshot has a body and no fetchedAt\n", name);
        std::abort();
    }
}

}

Signals computeSignals(std::optional<Instant> instant, const Config &config, const Snapshot<AccountBody> &account,
                       const Snapshot<MetersBody> &meters, const Snapshot<TokensBody> &tokens,
                       const Snapshot<OutlookBody> &outlook, const Snapshot<UsageBody> &halfHourly,
                       const Snapshot<UsageBody> &daily)
{
    requireFetchedAt(account, "account");
    Signals signals;
    if (!instant) {
        const Fault unsynced = detail::makeFault("clock_unsynced");
        signals.account.fault = unsynced;
        signals.tariff.fault = unsynced;
        signals.price.fault = unsynced;
        signals.energy.fault = unsynced;
        return signals;
    }
    detail::Selection selection;
    if (account.body) {
        selection = detail::selectAccount(config, *account.body);
    }
    signals.account = detail::accountGroup(*instant, config, account, selection, tokens);
    const detail::TariffResult tariff = detail::tariffGroup(*instant, account, selection);
    signals.tariff = tariff.group;
    signals.tariff.planChangeInstant = tariff.planChangeInstant;
    signals.price = detail::priceGroup(*instant, config, account, selection, outlook);
    signals.energy = detail::energyGroup(config, account, selection, meters, halfHourly, daily);

    std::optional<Instant> earliest;
    const auto consider = [&](std::optional<Instant> candidate) {
        if (candidate && *candidate > *instant && (!earliest || *candidate < *earliest)) {
            earliest = candidate;
        }
    };
    if (!signals.tariff.fault) {
        consider(signals.tariff.nextChange);
    }
    consider(tariff.planChangeInstant);
    if (account.body) {
        consider(*account.fetchedAt + ACCOUNT_MAX_AGE_S);
    }
    if (!signals.price.fault) {
        consider(signals.price.intervalStart + PRICE_STALE_AFTER_S);
    }
    if (signals.account.tokenExpiresAt) {
        consider(*signals.account.tokenExpiresAt - TOKEN_EXPIRY_WARNING_DAYS * 86400);
    }
    signals.nextEvaluation = earliest;
    return signals;
}

std::optional<std::string> selectedNmi(const Config &config, const Snapshot<AccountBody> &account,
                                       const Snapshot<MetersBody> &meters)
{
    if (!account.body || !meters.body) {
        return std::nullopt;
    }
    const detail::Selection selection = detail::selectAccount(config, *account.body);
    if (selection.fault) {
        return std::nullopt;
    }
    return detail::chooseNmi(config, *selection.account, *meters.body).nmi;
}

}
