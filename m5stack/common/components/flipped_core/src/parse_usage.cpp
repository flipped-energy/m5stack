#include "flipped/core/parse.h"

#include <cstdio>

#include "flipped/core/time.h"
#include "json_reader.h"

namespace flipped::core {

namespace {

std::string numberText(double value)
{
    char text[32];
    std::snprintf(text, sizeof text, "%.17g", value);
    return text;
}

std::string costText(const KeyCost &cost)
{
    return cost.state == CostState::null ? std::string("null") : numberText(cost.value);
}

std::optional<std::string> takeCost(KeyCost &cost, const Field<double> &field, const std::string &time,
                                    const char *key)
{
    KeyCost incoming;
    incoming.state = field.present() ? CostState::value : CostState::null;
    incoming.value = field.present() ? field.value : 0;
    if (cost.state == CostState::noRows) {
        cost = incoming;
        return std::nullopt;
    }
    if (cost.state != incoming.state || cost.value != incoming.value) {
        return "usage rows at " + time + " with key " + key + " carry different cost values " + costText(cost) +
               " and " + costText(incoming);
    }
    return std::nullopt;
}

}

Parsed<UsageBody> parseUsage(ByteSource &source, std::string_view nmi)
{
    JsonDocument filter;
    filter["time"] = true;
    filter["value"] = true;
    filter["usageType"] = true;
    filter["controlledLoad"] = true;
    filter["nmi"] = true;
    filter["cost"] = true;

    UsageBody body;
    body.nmi = std::string(nmi);
    detail::SourceReader reader(source);
    std::optional<std::string> error = detail::readArray(reader, filter, [&](JsonObjectConst row) {
        if (body.invalid) {
            return;
        }
        const Field<std::string> rowNmi = detail::stringField(row["nmi"]);
        if (!rowNmi.present()) {
            body.invalid = "usage row nmi is missing or not a string";
            return;
        }
        if (rowNmi.value != nmi) {
            return;
        }
        const Field<std::string> usageType = detail::stringField(row["usageType"]);
        if (!usageType.present()) {
            body.invalid = "usage row usageType is missing or not a string";
            return;
        }
        const bool isExport = usageType.value == "Export";
        const bool isImport = usageType.value == "Import";
        if (!isExport && !isImport) {
            return;
        }
        const Field<std::string> time = detail::stringField(row["time"]);
        if (!time.present() || !isWallDateTime(time.value)) {
            body.invalid = "usage row time " + (time.present() ? time.value : std::string("(not a string)")) +
                           " is not a wall-clock date-time";
            return;
        }
        const std::string local = time.value.substr(0, 19);
        const Field<double> value = detail::numberField(row["value"]);
        if (!value.present()) {
            body.invalid = "usage row at " + local + " value is missing or not a number";
            return;
        }
        const Field<bool> controlledLoad = detail::boolField(row["controlledLoad"]);
        if (!controlledLoad.present()) {
            body.invalid = "usage row at " + local + " controlledLoad is missing or not a boolean";
            return;
        }
        const Field<double> cost = detail::numberField(row["cost"]);
        if (cost.presence == Presence::wrongType) {
            body.invalid = "usage row at " + local + " cost is not a number";
            return;
        }
        UsageBucket *bucket = nullptr;
        for (auto it = body.buckets.rbegin(); it != body.buckets.rend(); ++it) {
            if (it->time == local) {
                bucket = &*it;
                break;
            }
        }
        if (bucket == nullptr) {
            body.buckets.push_back(UsageBucket{});
            bucket = &body.buckets.back();
            bucket->time = local;
        }
        std::optional<std::string> mismatch;
        if (isExport && !controlledLoad.value) {
            bucket->gridImportKwh += value.value;
            mismatch = takeCost(bucket->exportGeneral, cost, local, "(Export, false)");
        } else if (isExport) {
            bucket->controlledLoadKwh += value.value;
            mismatch = takeCost(bucket->exportControlledLoad, cost, local, "(Export, true)");
        } else {
            bucket->solarExportKwh += value.value;
            if (!controlledLoad.value) {
                mismatch = takeCost(bucket->importGeneral, cost, local, "(Import, false)");
            }
        }
        if (mismatch) {
            body.invalid = *mismatch;
        }
    });
    if (error) {
        return Invalid{*error};
    }
    return body;
}

}
