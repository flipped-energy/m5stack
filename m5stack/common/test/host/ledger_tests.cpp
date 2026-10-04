#include <cmath>
#include <limits>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "flipped/core/energy_ledger.h"
#include "flipped/core/identity.h"
#include "flipped/core/time.h"
#include "suite.h"

using namespace flipped::core;

namespace {

constexpr Instant DAY_START = 1790690400;

EnergyEntry interval(Instant start, int minutes, double importKwh, double controlledKwh, double exportKwh)
{
    EnergyEntry entry;
    entry.start = start;
    entry.durationMinutes = minutes;
    entry.gridImportKwh = importKwh;
    entry.controlledLoadKwh = controlledKwh;
    entry.solarExportKwh = exportKwh;
    return entry;
}

std::string totals(const Ledger &ledger)
{
    return "imported " + std::to_string(ledger.importedMwh) + " mWh, exported " + std::to_string(ledger.exportedMwh) +
           " mWh, through " + formatInstant(ledger.through);
}

std::vector<EnergyEntry> firstWindow()
{
    return {interval(DAY_START + 1800, 30, 0.25, 0.1, 0.0), interval(DAY_START, 30, 0.412, 0.3, 0.95)};
}

}

bool runLedgerTests()
{
    Suite suite("ledger_tests");
    const std::string h = instanceHash("36200000000001");

    suite.run("first step of a new ledger reports new and takes the whole window", [&]() -> std::string {
        Ledger ledger = ledgerFor(h, std::nullopt);
        const LedgerStep step = advance(ledger, firstWindow());
        if (!step.newLedger || step.took != 2) {
            return "took " + std::to_string(step.took) + ", new " + std::to_string(step.newLedger);
        }
        if (ledger.importedMwh != 1062000 || ledger.exportedMwh != 950000 || ledger.through != DAY_START + 3600) {
            return totals(ledger);
        }
        if (ledger.startedAt != std::optional<Instant>(DAY_START) || step.firstStart != std::optional<Instant>(DAY_START) ||
            step.lastEnd != std::optional<Instant>(DAY_START + 3600)) {
            return "startedAt, firstStart or lastEnd wrong";
        }
        if (step.deltaImportedMwh != 1062000 || step.deltaExportedMwh != 950000) {
            return "delta " + std::to_string(step.deltaImportedMwh) + " / " + std::to_string(step.deltaExportedMwh);
        }
        return "";
    });

    suite.run("nothing is taken twice", [&]() -> std::string {
        Ledger ledger = ledgerFor(h, std::nullopt);
        advance(ledger, firstWindow());
        const Ledger before = ledger;
        const LedgerStep again = advance(ledger, firstWindow());
        if (again.took != 0 || again.newLedger || again.firstStart || again.lastEnd || again.deltaImportedMwh != 0) {
            return "second step took " + std::to_string(again.took);
        }
        if (ledger.importedMwh != before.importedMwh || ledger.exportedMwh != before.exportedMwh ||
            ledger.through != before.through) {
            return totals(ledger);
        }
        return "";
    });

    suite.run("a revised interval behind through changes nothing", [&]() -> std::string {
        Ledger ledger = ledgerFor(h, std::nullopt);
        advance(ledger, firstWindow());
        std::vector<EnergyEntry> revised = firstWindow();
        revised[0].gridImportKwh = 9.0;
        revised[1].solarExportKwh = 7.0;
        revised.push_back(interval(DAY_START - 1800, 30, 4.0, 0.0, 0.0));
        revised.push_back(interval(DAY_START + 3600, 30, 0.1, 0.2, 0.3));
        const LedgerStep step = advance(ledger, revised);
        if (step.took != 1 || step.newLedger || step.firstStart != std::optional<Instant>(DAY_START + 3600)) {
            return "took " + std::to_string(step.took);
        }
        if (ledger.importedMwh != 1062000 + 300000 || ledger.exportedMwh != 950000 + 300000 ||
            ledger.through != DAY_START + 5400) {
            return totals(ledger);
        }
        return "";
    });

    suite.run("the merged 120-minute interval is one step", [&]() -> std::string {
        Ledger ledger = ledgerFor(h, std::nullopt);
        advance(ledger, {interval(DAY_START, 30, 0.2, 0.0, 0.0)});
        const LedgerStep step = advance(ledger, {interval(DAY_START + 1800, 120, 0.9, 0.0, 0.0)});
        if (step.took != 1 || step.firstStart != std::optional<Instant>(DAY_START + 1800) ||
            step.lastEnd != std::optional<Instant>(DAY_START + 1800 + 7200)) {
            return "took " + std::to_string(step.took);
        }
        if (ledger.importedMwh != 1100000 || ledger.through != DAY_START + 9000) {
            return totals(ledger);
        }
        const LedgerStep next = advance(ledger, {interval(DAY_START + 9000, 30, 0.1, 0.0, 0.0)});
        if (next.took != 1 || ledger.importedMwh != 1200000) {
            return "interval after the merged one: " + totals(ledger);
        }
        return "";
    });

    suite.run("a new instance starts a new ledger", [&]() -> std::string {
        Ledger stored = ledgerFor(h, std::nullopt);
        advance(stored, firstWindow());
        const Ledger same = ledgerFor(h, stored);
        if (same.importedMwh != stored.importedMwh || same.startedAt != stored.startedAt) {
            return "the same instance did not keep its ledger";
        }
        const std::string other = instanceHash("36200000000001:4102000000");
        Ledger fresh = ledgerFor(other, stored);
        if (fresh.h != other || fresh.startedAt || fresh.importedMwh != 0 || fresh.exportedMwh != 0 || fresh.through != 0) {
            return "another instance kept the stored ledger";
        }
        const LedgerStep step = advance(fresh, firstWindow());
        if (!step.newLedger || step.took != 2) {
            return "the new instance's first step is not new";
        }
        return "";
    });

    suite.run("an empty window keeps a new ledger new", [&]() -> std::string {
        Ledger ledger = ledgerFor(h, std::nullopt);
        const LedgerStep empty = advance(ledger, {});
        if (empty.took != 0 || empty.newLedger || ledger.startedAt) {
            return "an empty step changed the ledger";
        }
        const LedgerStep first = advance(ledger, firstWindow());
        if (!first.newLedger) {
            return "the first step that takes intervals is not new";
        }
        return "";
    });

    suite.run("a negative or non-finite kWh in a fresh interval refuses the step", [&]() -> std::string {
        Ledger ledger = ledgerFor(h, std::nullopt);
        advance(ledger, firstWindow());
        EnergyEntry negative = interval(DAY_START + 5400, 30, 0.2, 0.0, -0.05);
        negative.local = "2026-09-29T01:30:00";
        const std::optional<std::string> refusal =
            ledgerRefusal(ledger, {negative, interval(DAY_START + 3600, 30, 0.1, 0.0, 0.0)});
        const std::string want =
            "energy interval 2026-09-29T01:30:00 (" + std::to_string(DAY_START + 5400) + ") has solarExportKwh -0.05";
        if (refusal != std::optional<std::string>(want)) {
            return "refusal is " + refusal.value_or("(none)");
        }
        if (!ledgerRefusal(ledger, {interval(DAY_START + 3600, 30, std::nan(""), 0.0, 0.0)})) {
            return "a NaN kWh was not refused";
        }
        if (!ledgerRefusal(ledger, {interval(DAY_START + 3600, 30, 0.1, std::numeric_limits<double>::infinity(), 0.0)})) {
            return "an infinite kWh was not refused";
        }
        if (!ledgerRefusal(ledgerFor(h, std::nullopt), {interval(DAY_START, 30, -1.0, 0.0, 0.0)})) {
            return "a new ledger did not refuse a negative kWh";
        }
        return "";
    });

    suite.run("a negative kWh behind through or a clean window is not refused", [&]() -> std::string {
        Ledger ledger = ledgerFor(h, std::nullopt);
        advance(ledger, firstWindow());
        if (const std::optional<std::string> refusal = ledgerRefusal(ledger, {interval(DAY_START, 30, -1.0, 0.0, 0.0)})) {
            return "refused an interval already behind through: " + *refusal;
        }
        if (const std::optional<std::string> refusal = ledgerRefusal(ledgerFor(h, std::nullopt), firstWindow())) {
            return "refused a clean window: " + *refusal;
        }
        return "";
    });

    suite.run("blob round trip and refusals", [&]() -> std::string {
        Ledger ledger = ledgerFor(h, std::nullopt);
        advance(ledger, firstWindow());
        const auto blob = encodeLedger(ledger);
        const std::variant<Ledger, Invalid> decoded = decodeLedger(blob.data(), blob.size());
        const Ledger *back = std::get_if<Ledger>(&decoded);
        if (back == nullptr || back->h != ledger.h || back->startedAt != ledger.startedAt || back->through != ledger.through ||
            back->importedMwh != ledger.importedMwh || back->exportedMwh != ledger.exportedMwh) {
            return "the decoded ledger differs";
        }
        if (!std::holds_alternative<Invalid>(decodeLedger(blob.data(), blob.size() - 1))) {
            return "a short blob was accepted";
        }
        auto wrong = blob;
        wrong[0] = LEDGER_VERSION + 1;
        if (!std::holds_alternative<Invalid>(decodeLedger(wrong.data(), wrong.size()))) {
            return "another version was accepted";
        }
        return "";
    });

    return suite.finish();
}
