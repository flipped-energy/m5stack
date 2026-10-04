#include <cstdio>

#include "flipped/app/ui_model.h"

int runVectors(const char *directory);
int runSequences(const char *directory);
bool runRegionChangeTests();
bool runLedgerTests();
bool runIdentityTests();
bool runProjectionTests();
bool runConfigTests();
bool runSwitchPlanTests();
bool runTariffTablesTests();
bool runMeteringTests();

int main(int argc, char **argv)
{
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <vectors-dir> <sequences-dir>\n", argv[0]);
        return 2;
    }
    const bool vectors = runVectors(argv[1]) == 0;
    const bool sequences = runSequences(argv[2]) == 0;
    const bool regionChange = runRegionChangeTests();
    const bool ledger = runLedgerTests();
    const bool identity = runIdentityTests();
    const bool projection = runProjectionTests();
    const bool config = runConfigTests();
    const bool switchPlan = runSwitchPlanTests();
    const bool tariffTables = runTariffTablesTests();
    const bool metering = runMeteringTests();
    const bool passed = vectors && sequences && regionChange && ledger && identity && projection && config && switchPlan &&
                        tariffTables && metering;
    std::printf("core_tests: %s\n", passed ? "passed" : "FAILED");
    return passed ? 0 : 1;
}
