#include "flipped/core/energy_publication.h"

#include <limits>

#include "flipped/core/flipped_cluster.h"

namespace flipped::core {

namespace {

class Converter {
public:
    explicit Converter(std::vector<std::string> &problems) : problems_(problems) {}

    std::optional<uint32_t> epoch(const char *what, Instant instant)
    {
        const Instant seconds = instant - MATTER_EPOCH_UNIX_S;
        if (seconds < 0 || seconds >= std::numeric_limits<uint32_t>::max()) {
            problems_.push_back(std::string(what) + " " + std::to_string(instant) +
                                " is outside the Matter epoch-s range");
            return std::nullopt;
        }
        return static_cast<uint32_t>(seconds);
    }

    std::optional<int64_t> energy(const char *what, uint64_t mwh)
    {
        if (mwh > ENERGY_MWH_MAX) {
            problems_.push_back(std::string(what) + " " + std::to_string(mwh) + " mWh exceeds 2^62");
            return std::nullopt;
        }
        return static_cast<int64_t>(mwh);
    }

private:
    std::vector<std::string> &problems_;
};

}

EnergyPublication energyPublication(const EnergyGroup &energy, const std::optional<Ledger> &ledger,
                                    const std::optional<LedgerStep> &step, const EnergyPublication &previous)
{
    EnergyPublication publication;
    if (energy.fault || !ledger || !ledger->startedAt) {
        return publication;
    }
    if (const std::optional<std::string> refusal = ledgerRefusal(*ledger, energy.intervals)) {
        publication.problems.push_back("ledger not advanced: " + *refusal);
        return publication;
    }
    Converter convert(publication.problems);
    const std::optional<uint32_t> through = convert.epoch("ledger through", ledger->through);
    const std::optional<uint32_t> startedAt = convert.epoch("ledger startedAt", *ledger->startedAt);
    const std::optional<int64_t> imported = convert.energy("ledger importedMwh", ledger->importedMwh);
    const std::optional<int64_t> exported = convert.energy("ledger exportedMwh", ledger->exportedMwh);
    if (!through || !startedAt || !imported || !exported) {
        return publication;
    }
    publication.cumulativeImported = EnergyMeasurementValue{*imported, std::nullopt, *through};
    publication.cumulativeExported = EnergyMeasurementValue{*exported, std::nullopt, *through};
    publication.cumulativeResetTimestamp = startedAt;

    if (step && step->took > 0) {
        publication.cumulativeEvent = true;
        if (step->newLedger) {
            return publication;
        }
        const std::optional<uint32_t> first = convert.epoch("ledger step firstStart", *step->firstStart);
        const std::optional<uint32_t> last = convert.epoch("ledger step lastEnd", *step->lastEnd);
        const std::optional<int64_t> deltaImported = convert.energy("ledger step deltaImportedMwh", step->deltaImportedMwh);
        const std::optional<int64_t> deltaExported = convert.energy("ledger step deltaExportedMwh", step->deltaExportedMwh);
        if (!first || !last || !deltaImported || !deltaExported) {
            return publication;
        }
        publication.periodicImported = EnergyMeasurementValue{*deltaImported, first, *last};
        publication.periodicExported = EnergyMeasurementValue{*deltaExported, first, *last};
        publication.periodicEvent = true;
        return publication;
    }
    publication.periodicImported = previous.periodicImported;
    publication.periodicExported = previous.periodicExported;
    publication.cumulativeEvent = !previous.cumulativeImported;
    return publication;
}

std::vector<EnergyAttribute> changedEnergyAttributes(const EnergyPublication &before, const EnergyPublication &after)
{
    std::vector<EnergyAttribute> changed;
    if (before.cumulativeImported != after.cumulativeImported) {
        changed.push_back(EnergyAttribute::cumulativeImported);
    }
    if (before.cumulativeExported != after.cumulativeExported) {
        changed.push_back(EnergyAttribute::cumulativeExported);
    }
    if (before.periodicImported != after.periodicImported) {
        changed.push_back(EnergyAttribute::periodicImported);
    }
    if (before.periodicExported != after.periodicExported) {
        changed.push_back(EnergyAttribute::periodicExported);
    }
    if (before.cumulativeResetTimestamp != after.cumulativeResetTimestamp) {
        changed.push_back(EnergyAttribute::cumulativeReset);
    }
    return changed;
}

std::optional<int64_t> deferredMarkAt(std::optional<int64_t> lastMarkMs, int64_t nowMs)
{
    if (!lastMarkMs || nowMs - *lastMarkMs >= ENERGY_MARK_SPACING_MS) {
        return std::nullopt;
    }
    return *lastMarkMs + ENERGY_MARK_SPACING_MS;
}

}
