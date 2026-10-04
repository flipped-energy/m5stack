#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>

#include "flipped/core/flipped_cluster.h"
#include "flipped/core/switch_plan.h"
#include "suite.h"

using namespace flipped::core;

namespace {

const std::string KEY = "36200000000001:4102000000";
const std::string H = "16561ad652a6616f";

std::optional<StoredSwitches> fromBlob(const std::string &h, std::array<uint16_t, SWITCH_COUNT> ids)
{
    const std::array<uint8_t, STORED_SWITCHES_BYTES> blob = encodeStoredSwitches(h, ids);
    std::variant<StoredSwitches, Invalid> decoded = decodeStoredSwitches(blob.data(), blob.size());
    if (std::holds_alternative<Invalid>(decoded)) {
        return std::nullopt;
    }
    return std::get<StoredSwitches>(decoded);
}

std::array<uint8_t, FOUR_SWITCH_BLOB_BYTES> fourSwitchBlob(const std::string &h, std::array<uint16_t, 4> ids)
{
    std::array<uint8_t, FOUR_SWITCH_BLOB_BYTES> blob{};
    std::copy(h.begin(), h.end(), blob.begin());
    for (size_t i = 0; i < ids.size(); ++i) {
        blob[INSTANCE_HASH_CHARS + 2 * i] = static_cast<uint8_t>(ids[i] & 0xFF);
        blob[INSTANCE_HASH_CHARS + 2 * i + 1] = static_cast<uint8_t>(ids[i] >> 8);
    }
    return blob;
}

std::optional<StoredSwitches> fromFourSwitchBlob(const std::string &h, std::array<uint16_t, 4> ids)
{
    const std::array<uint8_t, FOUR_SWITCH_BLOB_BYTES> blob = fourSwitchBlob(h, ids);
    std::variant<StoredSwitches, Invalid> decoded = decodeStoredSwitches(blob.data(), blob.size());
    if (std::holds_alternative<Invalid>(decoded)) {
        return std::nullopt;
    }
    return std::get<StoredSwitches>(decoded);
}

std::string expectIds(const SwitchPlan &plan, const std::array<std::optional<uint16_t>, SWITCH_COUNT> &expectedIds)
{
    for (size_t i = 0; i < SWITCH_COUNT; ++i) {
        const std::optional<uint16_t> expected = expectedIds[i];
        if (plan.endpoints[i].endpointId != expected) {
            return plan.endpoints[i].key + " endpoint " +
                   (plan.endpoints[i].endpointId ? std::to_string(*plan.endpoints[i].endpointId) : "none") +
                   ", expected " + (expected ? std::to_string(*expected) : "none");
        }
    }
    return "";
}

std::string expectIds(const SwitchPlan &plan, const std::optional<std::array<uint16_t, SWITCH_COUNT>> &ids)
{
    std::array<std::optional<uint16_t>, SWITCH_COUNT> expected{};
    if (ids) {
        std::copy(ids->begin(), ids->end(), expected.begin());
    }
    return expectIds(plan, expected);
}

std::string expectAction(const SwitchPlan &plan, SwitchAction action, bool store)
{
    if (plan.action != action || plan.store != store) {
        return "action " + std::to_string(static_cast<int>(plan.action)) + ", store " + (plan.store ? "true" : "false");
    }
    return "";
}

}

bool runSwitchPlanTests()
{
    Suite suite("switch_plan_tests");

    suite.run("labels and UniqueIDs of the five switches for a fixed instanceKey", []() -> std::string {
        const SwitchPlan plan = switchPlan(KEY, std::nullopt);
        const std::array<std::array<const char *, 3>, SWITCH_COUNT> expected{{
            {"peak_rate", "Peak Rate", "FE-16561ad652a6616f-PK"},
            {"off_peak_rate", "Off-Peak Rate", "FE-16561ad652a6616f-OP"},
            {"shoulder_rate", "Shoulder Rate", "FE-16561ad652a6616f-SH"},
            {"wholesale_price_high", "Wholesale Price High", "FE-16561ad652a6616f-WH"},
            {"wholesale_price_low", "Wholesale Price Low", "FE-16561ad652a6616f-WL"},
        }};
        if (plan.h != H) {
            return "h " + plan.h;
        }
        for (size_t i = 0; i < SWITCH_COUNT; ++i) {
            const SwitchEndpoint &endpoint = plan.endpoints[i];
            if (endpoint.key != expected[i][0] || endpoint.nodeLabel != expected[i][1] ||
                endpoint.uniqueId != expected[i][2]) {
                return endpoint.key + " / " + endpoint.nodeLabel + " / " + endpoint.uniqueId;
            }
            if (endpoint.uniqueId.size() != 22 || endpoint.uniqueId.size() > UNIQUE_ID_MAX_CHARS) {
                return endpoint.uniqueId + " is " + std::to_string(endpoint.uniqueId.size()) + " characters";
            }
        }
        return "";
    });

    suite.run("no stored blob: none without an instance, replace once the instance is known", []() -> std::string {
        const SwitchPlan unknown = switchPlan(std::nullopt, std::nullopt);
        if (unknown.action != SwitchAction::none || !unknown.h.empty() || !unknown.endpoints[0].uniqueId.empty()) {
            return "without an instance: action " + std::to_string(static_cast<int>(unknown.action));
        }
        const SwitchPlan known = switchPlan(KEY, std::nullopt);
        if (const std::string why = expectAction(known, SwitchAction::replace, true); !why.empty()) {
            return "with an instance: " + why;
        }
        return expectIds(known, std::nullopt);
    });

    suite.run("a stored blob without an instance: none", []() -> std::string {
        const SwitchPlan plan = switchPlan(std::nullopt, fromBlob(H, {4, 5, 6, 7, 8}));
        return expectAction(plan, SwitchAction::none, false);
    });

    suite.run("a stored blob with the same h: resume under the stored IDs, nothing to store", []() -> std::string {
        const std::optional<StoredSwitches> stored = fromBlob(H, {8, 9, 12, 10, 11});
        if (!stored) {
            return "the blob did not decode";
        }
        const SwitchPlan plan = switchPlan(KEY, stored);
        if (const std::string why = expectAction(plan, SwitchAction::resume, false); !why.empty()) {
            return why;
        }
        return expectIds(plan, std::array<uint16_t, SWITCH_COUNT>{8, 9, 12, 10, 11});
    });

    suite.run("a stored blob with another h: replace", []() -> std::string {
        const std::optional<StoredSwitches> stored = fromBlob("b016d710ad0e9769", {4, 5, 6, 7, 8});
        if (!stored) {
            return "the blob did not decode";
        }
        const SwitchPlan plan = switchPlan(KEY, stored);
        if (const std::string why = expectAction(plan, SwitchAction::replace, true); !why.empty() || plan.h != H) {
            return why + ", h " + plan.h;
        }
        return expectIds(plan, std::nullopt);
    });

    suite.run("a four-switch blob of the same h: resume its four by key, create Shoulder Rate, store", []() -> std::string {
        const std::optional<StoredSwitches> stored = fromFourSwitchBlob(H, {4, 5, 6, 7});
        if (!stored) {
            return "the four-switch blob did not decode";
        }
        const std::array<std::optional<uint16_t>, SWITCH_COUNT> decoded{4, 5, std::nullopt, 6, 7};
        if (stored->h != H || stored->endpointIds != decoded) {
            return "decoded h " + stored->h;
        }
        const SwitchPlan plan = switchPlan(KEY, stored);
        if (const std::string why = expectAction(plan, SwitchAction::resume, true); !why.empty()) {
            return why;
        }
        if (const std::string why = expectIds(plan, decoded); !why.empty()) {
            return why;
        }
        if (plan.endpoints[2].key != "shoulder_rate" || plan.endpoints[2].uniqueId != "FE-16561ad652a6616f-SH") {
            return plan.endpoints[2].key + " / " + plan.endpoints[2].uniqueId;
        }
        const std::array<uint8_t, STORED_SWITCHES_BYTES> next = encodeStoredSwitches(plan.h, {4, 5, 9, 6, 7});
        std::variant<StoredSwitches, Invalid> reread = decodeStoredSwitches(next.data(), next.size());
        if (!std::holds_alternative<StoredSwitches>(reread)) {
            return "the five-switch blob written after the upgrade did not decode";
        }
        const SwitchPlan after = switchPlan(KEY, std::get<StoredSwitches>(reread));
        if (const std::string why = expectAction(after, SwitchAction::resume, false); !why.empty()) {
            return "next boot: " + why;
        }
        return expectIds(after, std::array<uint16_t, SWITCH_COUNT>{4, 5, 9, 6, 7});
    });

    suite.run("a four-switch blob of another h: replace all five", []() -> std::string {
        const SwitchPlan plan = switchPlan(KEY, fromFourSwitchBlob("b016d710ad0e9769", {4, 5, 6, 7}));
        if (const std::string why = expectAction(plan, SwitchAction::replace, true); !why.empty()) {
            return why;
        }
        return expectIds(plan, std::nullopt);
    });

    suite.run("the blob is h then five little-endian endpoint IDs, and a damaged blob is invalid", []() -> std::string {
        const std::array<uint8_t, STORED_SWITCHES_BYTES> blob = encodeStoredSwitches(H, {4, 0x0105, 6, 7, 0x0208});
        if (std::string(blob.begin(), blob.begin() + INSTANCE_HASH_CHARS) != H || blob[16] != 4 || blob[17] != 0 ||
            blob[18] != 0x05 || blob[19] != 0x01 || blob[24] != 0x08 || blob[25] != 0x02 || blob.size() != 26) {
            return "unexpected layout";
        }
        for (const size_t size : {blob.size() - 1, blob.size() - 3, blob.size() + 2}) {
            std::array<uint8_t, STORED_SWITCHES_BYTES + 2> padded{};
            std::copy(blob.begin(), blob.end(), padded.begin());
            if (!std::holds_alternative<Invalid>(decodeStoredSwitches(padded.data(), size))) {
                return "a " + std::to_string(size) + "-byte blob decoded";
            }
        }
        std::array<uint8_t, STORED_SWITCHES_BYTES> damaged = blob;
        damaged[3] = 'G';
        if (!std::holds_alternative<Invalid>(decodeStoredSwitches(damaged.data(), damaged.size()))) {
            return "a blob with a non-hexadecimal h decoded";
        }
        std::array<uint8_t, FOUR_SWITCH_BLOB_BYTES> damagedFour = fourSwitchBlob(H, {4, 5, 6, 7});
        damagedFour[0] = 'A';
        if (!std::holds_alternative<Invalid>(decodeStoredSwitches(damagedFour.data(), damagedFour.size()))) {
            return "a four-switch blob with a non-hexadecimal h decoded";
        }
        return "";
    });

    suite.run("a faulted group leaves its switches unknown, an ok group gives their values", []() -> std::string {
        Signals signals;
        signals.tariff.fault = Fault{"tariff-unavailable", 503, std::string("down"), 4, std::nullopt};
        signals.tariff.period.segment.band = Band::shoulder;
        signals.price.priceHigh = true;
        const std::array<std::optional<bool>, SWITCH_COUNT> values = switchValues(signals);
        if (values[0] || values[1] || values[2] || values[3] != true || values[4] != false) {
            return "unexpected switch values";
        }
        return "";
    });

    suite.run("Shoulder Rate is on exactly while the current period's band is shoulder", []() -> std::string {
        Signals signals;
        signals.price.fault = Fault{"price-unavailable", 503, std::string("down"), 4, std::nullopt};
        const std::array<std::pair<Band, std::array<bool, 3>>, 4> cases{{
            {Band::peak, {true, false, false}},
            {Band::offPeak, {false, true, false}},
            {Band::shoulder, {false, false, true}},
            {Band::anytime, {false, false, false}},
        }};
        for (const auto &[band, expected] : cases) {
            signals.tariff.period.segment.band = band;
            signals.tariff.peak = band == Band::peak;
            signals.tariff.offPeak = band == Band::offPeak;
            const std::array<std::optional<bool>, SWITCH_COUNT> values = switchValues(signals);
            for (size_t i = 0; i < expected.size(); ++i) {
                if (values[i] != expected[i]) {
                    return "band " + std::to_string(static_cast<int>(band)) + " switch " + std::to_string(i);
                }
            }
            if (values[3] || values[4]) {
                return "a faulted price group gave a wholesale switch a value";
            }
        }
        return "";
    });

    suite.run("Flipped Energy cluster: money, epoch, nulls of a faulted group, text cut on UTF-8", []() -> std::string {
        if (matterMoney(253000000) != 2530000 || matterMoney(-149) != -1 || matterMoney(-150) != -2 ||
            matterMoney(149) != 1 || matterMoney(150) != 2) {
            return "matterMoney rounding";
        }
        Signals signals;
        signals.tariff.period.segment.band = Band::shoulder;
        signals.tariff.period.segment.name = "Shoulder";
        signals.tariff.period.segment.rateKey = 253000000;
        signals.tariff.nextChange = MATTER_EPOCH_UNIX_S + 7200;
        std::string body(510, 'a');
        body += "\xE2\x82\xAC";
        signals.price.fault = Fault{"http-error", 500, body, body.size(), std::nullopt};
        signals.price.centsPerKwh = 12.3456;
        signals.energy.fault = Fault{"usage-unavailable", std::nullopt, std::nullopt, std::nullopt, std::string("timeout")};
        signals.account.tokenExpiresAt = MATTER_EPOCH_UNIX_S - 1;
        const FlippedClusterValues values = flippedClusterValues(signals, 4321);
        if (values.ratePeriod != 2 || values.ratePeriodName != "Shoulder" || values.currentRate != 2530000 ||
            values.nextRateChange != 7200u || values.fixedRateComponent || values.tariffFault.code) {
            return "tariff values";
        }
        if (values.wholesalePrice || values.priceFault.code != "http-error" || values.priceFault.httpStatus != 500 ||
            !values.priceFault.text || values.priceFault.text->size() != 510 || values.priceFault.bodyBytes != 513u) {
            return "price fault values";
        }
        if (values.energyFault.text != "timeout" || values.energyFault.httpStatus || values.lastDayStart) {
            return "energy fault values";
        }
        if (values.tokenExpiresAt || values.problems.size() != 1 || values.dailyLimitRemaining != 4321) {
            return std::to_string(values.problems.size()) + " problems";
        }
        return "";
    });

    return suite.finish();
}
