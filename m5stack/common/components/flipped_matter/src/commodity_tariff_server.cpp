#include <algorithm>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <esp_log.h>

#include <app-common/zap-generated/cluster-objects.h>
#include <app/AttributeAccessInterface.h>
#include <app/AttributeAccessInterfaceRegistry.h>
#include <app/CommandHandlerInterface.h>
#include <app/CommandHandlerInterfaceRegistry.h>
#include <app/reporting/reporting.h>
#include <protocols/interaction_model/StatusCode.h>

#include "flipped/core/flipped_cluster.h"
#include "flipped/matter/ids.h"
#include "matter_internal.h"

namespace flipped::matter {

namespace {

namespace Tariff = chip::app::Clusters::CommodityTariff;
namespace Globals = chip::app::Clusters::Globals;
using chip::app::AttributeValueEncoder;
using chip::app::DataModel::List;
using chip::app::DataModel::Nullable;
using Status = chip::Protocols::InteractionModel::Status;

chip::CharSpan span(const std::string &text)
{
    return chip::CharSpan(text.data(), text.size());
}

Nullable<chip::CharSpan> nullableText(const std::optional<std::string> &text)
{
    return text ? Nullable<chip::CharSpan>(span(*text)) : Nullable<chip::CharSpan>();
}

List<const uint32_t> ids(const std::vector<uint32_t> &values)
{
    return List<const uint32_t>(values.data(), values.size());
}

Tariff::Structs::DayEntryStruct::Type dayEntry(const core::DayEntryValue &value)
{
    Tariff::Structs::DayEntryStruct::Type entry;
    entry.dayEntryID = value.id;
    entry.startTime = value.startTime;
    return entry;
}

Tariff::Structs::DayStruct::Type day(const core::DayValue &value)
{
    Tariff::Structs::DayStruct::Type result;
    result.date = value.date;
    result.dayType = Tariff::DayTypeEnum::kStandard;
    result.dayEntryIDs = ids(value.dayEntryIds);
    return result;
}

Tariff::Structs::DayPatternStruct::Type dayPattern(const core::DayPatternValue &value)
{
    Tariff::Structs::DayPatternStruct::Type pattern;
    pattern.dayPatternID = value.id;
    pattern.daysOfWeek = chip::BitMask<Tariff::DayPatternDayOfWeekBitmap>(value.daysOfWeek);
    pattern.dayEntryIDs = ids(value.dayEntryIds);
    return pattern;
}

Tariff::Structs::CalendarPeriodStruct::Type calendarPeriod(const core::CalendarPeriodValue &value)
{
    Tariff::Structs::CalendarPeriodStruct::Type period;
    period.startDate.SetNonNull(value.startDate);
    period.dayPatternIDs = ids(value.dayPatternIds);
    return period;
}

Tariff::Structs::TariffComponentStruct::Type component(const core::TariffComponentValue &value)
{
    Tariff::Structs::TariffComponentStruct::Type result;
    result.tariffComponentID = value.id;
    Nullable<Tariff::Structs::TariffPriceStruct::Type> price;
    if (value.price) {
        Tariff::Structs::TariffPriceStruct::Type known;
        known.priceType = Globals::TariffPriceTypeEnum::kStandard;
        known.price.SetValue(value.price->price);
        known.priceLevel.SetValue(value.price->priceLevel);
        price.SetNonNull(known);
    }
    result.price.SetValue(price);
    if (value.peak) {
        Tariff::Structs::PeakPeriodStruct::Type peak;
        peak.severity = Tariff::PeakPeriodSeverityEnum::kHigh;
        peak.peakPeriod = 1;
        result.peakPeriod.SetValue(peak);
    }
    if (value.threshold) {
        result.threshold.SetNonNull(*value.threshold);
    }
    result.label.SetValue(nullableText(value.label));
    result.predicted.SetValue(false);
    return result;
}

Tariff::Structs::TariffPeriodStruct::Type tariffPeriod(const core::TariffPeriodValue &value)
{
    Tariff::Structs::TariffPeriodStruct::Type period;
    period.label = nullableText(value.label);
    period.dayEntryIDs = ids(value.dayEntryIds);
    period.tariffComponentIDs = ids(value.componentIds);
    return period;
}

template <typename Value, typename Make>
CHIP_ERROR encodeList(AttributeValueEncoder &encoder, const std::optional<std::vector<Value>> &values, Make make)
{
    if (!values) {
        return encoder.EncodeNull();
    }
    return encoder.EncodeList([&](const auto &items) -> CHIP_ERROR {
        for (const Value &value : *values) {
            ReturnErrorOnFailure(items.Encode(make(value)));
        }
        return CHIP_NO_ERROR;
    });
}

template <typename Value, typename Make>
CHIP_ERROR encodeOne(AttributeValueEncoder &encoder, const std::optional<Value> &value, Make make)
{
    if (!value) {
        return encoder.EncodeNull();
    }
    return encoder.Encode(make(*value));
}

CHIP_ERROR encodeNumber(AttributeValueEncoder &encoder, const std::optional<uint32_t> &value)
{
    if (!value) {
        return encoder.EncodeNull();
    }
    return encoder.Encode(*value);
}

class TariffServer : public chip::app::AttributeAccessInterface, public chip::app::CommandHandlerInterface {
public:
    TariffServer()
        : AttributeAccessInterface(chip::MakeOptional(chip::EndpointId(ELECTRICAL_METER_ENDPOINT)), Tariff::Id),
          CommandHandlerInterface(chip::MakeOptional(chip::EndpointId(ELECTRICAL_METER_ENDPOINT)), Tariff::Id)
    {
    }

    CHIP_ERROR Read(const chip::app::ConcreteReadAttributePath &path, AttributeValueEncoder &encoder) override
    {
        namespace A = Tariff::Attributes;
        switch (path.mAttributeId) {
        case A::TariffInfo::Id:
            return encodeOne(encoder, tables_.info, [](const core::TariffInfoValue &info) {
                Tariff::Structs::TariffInformationStruct::Type result;
                result.tariffLabel = nullableText(info.label);
                result.providerName.SetNonNull(span(info.providerName));
                Globals::Structs::CurrencyStruct::Type currency;
                currency.currency = core::CURRENCY_AUD;
                currency.decimalPoints = core::CURRENCY_DECIMAL_POINTS;
                result.currency.SetValue(Nullable<Globals::Structs::CurrencyStruct::Type>(currency));
                result.blockMode.SetNonNull(static_cast<Tariff::BlockModeEnum>(info.blockMode));
                return result;
            });
        case A::TariffUnit::Id:
            return tables_.info ? encoder.Encode(Globals::TariffUnitEnum::kKWh) : encoder.EncodeNull();
        case A::StartDate::Id:
            return encodeNumber(encoder, tables_.startDate);
        case A::DayEntries::Id:
            return encodeList(encoder, tables_.dayEntries, dayEntry);
        case A::DayPatterns::Id:
            return encodeList(encoder, tables_.dayPatterns, dayPattern);
        case A::CalendarPeriods::Id:
            return encodeList(encoder, tables_.calendarPeriods, calendarPeriod);
        case A::IndividualDays::Id:
            return encodeList(encoder, tables_.individualDays, day);
        case A::CurrentDay::Id:
            return encodeOne(encoder, tables_.currentDay, day);
        case A::NextDay::Id:
            return encodeOne(encoder, tables_.nextDay, day);
        case A::CurrentDayEntry::Id:
            return encodeOne(encoder, tables_.currentDayEntry, dayEntry);
        case A::CurrentDayEntryDate::Id:
            return encodeNumber(encoder, tables_.currentDayEntryDate);
        case A::NextDayEntry::Id:
            return encodeOne(encoder, tables_.nextDayEntry, dayEntry);
        case A::NextDayEntryDate::Id:
            return encodeNumber(encoder, tables_.nextDayEntryDate);
        case A::TariffComponents::Id:
            return encodeList(encoder, tables_.components, component);
        case A::TariffPeriods::Id:
            return encodeList(encoder, tables_.periods, tariffPeriod);
        case A::CurrentTariffComponents::Id:
            return encodeComponents(encoder, tables_.currentComponentIds);
        case A::NextTariffComponents::Id:
            return encodeComponents(encoder, tables_.nextComponentIds);
        default:
            return CHIP_NO_ERROR;
        }
    }

    void InvokeCommand(HandlerContext &context) override
    {
        namespace C = Tariff::Commands;
        switch (context.mRequestPath.mCommandId) {
        case C::GetTariffComponent::Id:
            HandleCommand<C::GetTariffComponent::DecodableType>(
                context, [this](HandlerContext &ctx, const auto &request) { getTariffComponent(ctx, request); });
            return;
        case C::GetDayEntry::Id:
            HandleCommand<C::GetDayEntry::DecodableType>(
                context, [this](HandlerContext &ctx, const auto &request) { getDayEntry(ctx, request); });
            return;
        default:
            return;
        }
    }

    void update(core::TariffTables next)
    {
        namespace A = Tariff::Attributes;
        std::vector<chip::AttributeId> changed;
        const auto mark = [&](bool differs, chip::AttributeId id) {
            if (differs) {
                changed.push_back(id);
            }
        };
        mark(next.info != tables_.info, A::TariffInfo::Id);
        mark(next.info.has_value() != tables_.info.has_value(), A::TariffUnit::Id);
        mark(next.startDate != tables_.startDate, A::StartDate::Id);
        mark(next.dayEntries != tables_.dayEntries, A::DayEntries::Id);
        mark(next.dayPatterns != tables_.dayPatterns, A::DayPatterns::Id);
        mark(next.calendarPeriods != tables_.calendarPeriods, A::CalendarPeriods::Id);
        mark(next.individualDays != tables_.individualDays, A::IndividualDays::Id);
        mark(next.currentDay != tables_.currentDay, A::CurrentDay::Id);
        mark(next.nextDay != tables_.nextDay, A::NextDay::Id);
        mark(next.currentDayEntry != tables_.currentDayEntry, A::CurrentDayEntry::Id);
        mark(next.currentDayEntryDate != tables_.currentDayEntryDate, A::CurrentDayEntryDate::Id);
        mark(next.nextDayEntry != tables_.nextDayEntry, A::NextDayEntry::Id);
        mark(next.nextDayEntryDate != tables_.nextDayEntryDate, A::NextDayEntryDate::Id);
        mark(next.components != tables_.components, A::TariffComponents::Id);
        mark(next.periods != tables_.periods, A::TariffPeriods::Id);
        mark(next.currentComponentIds != tables_.currentComponentIds, A::CurrentTariffComponents::Id);
        mark(next.nextComponentIds != tables_.nextComponentIds, A::NextTariffComponents::Id);
        tables_ = std::move(next);
        for (const chip::AttributeId id : changed) {
            MatterReportingAttributeChangeCallback(ELECTRICAL_METER_ENDPOINT, Tariff::Id, id);
        }
    }

private:
    CHIP_ERROR encodeComponents(AttributeValueEncoder &encoder, const std::optional<std::vector<uint32_t>> &componentIds)
    {
        if (!componentIds) {
            return encoder.EncodeNull();
        }
        return encoder.EncodeList([&](const auto &items) -> CHIP_ERROR {
            for (const uint32_t id : *componentIds) {
                const core::TariffComponentValue *found = core::findComponent(tables_, id);
                require(found != nullptr, "a current or next TariffComponentID is not in TariffComponents");
                ReturnErrorOnFailure(items.Encode(component(*found)));
            }
            return CHIP_NO_ERROR;
        });
    }

    void getTariffComponent(HandlerContext &ctx, const Tariff::Commands::GetTariffComponent::DecodableType &request)
    {
        const core::TariffComponentValue *found = core::findComponent(tables_, request.tariffComponentID);
        if (found == nullptr) {
            ctx.mCommandHandler.AddStatus(ctx.mRequestPath, Status::NotFound);
            return;
        }
        std::string label;
        bool labelled = false;
        std::vector<uint32_t> entries;
        for (const core::TariffPeriodValue *period : core::periodsWithComponent(tables_, request.tariffComponentID)) {
            if (period->label) {
                label += labelled ? "; " + *period->label : *period->label;
                labelled = true;
            }
            entries.insert(entries.end(), period->dayEntryIds.begin(), period->dayEntryIds.end());
        }
        std::sort(entries.begin(), entries.end());
        entries.erase(std::unique(entries.begin(), entries.end()), entries.end());
        label = core::utf8Prefix(label, core::TARIFF_TEXT_MAX_BYTES);
        Tariff::Commands::GetTariffComponentResponse::Type response;
        response.label = labelled ? Nullable<chip::CharSpan>(span(label)) : Nullable<chip::CharSpan>();
        response.dayEntryIDs = ids(entries);
        response.tariffComponent = component(*found);
        ctx.mCommandHandler.AddResponse(ctx.mRequestPath, response);
    }

    void getDayEntry(HandlerContext &ctx, const Tariff::Commands::GetDayEntry::DecodableType &request)
    {
        if (tables_.dayEntries) {
            for (const core::DayEntryValue &entry : *tables_.dayEntries) {
                if (entry.id == request.dayEntryID) {
                    Tariff::Commands::GetDayEntryResponse::Type response;
                    response.dayEntry = dayEntry(entry);
                    ctx.mCommandHandler.AddResponse(ctx.mRequestPath, response);
                    return;
                }
            }
        }
        ctx.mCommandHandler.AddStatus(ctx.mRequestPath, Status::NotFound);
    }

    core::TariffTables tables_;
};

TariffServer *server = nullptr;

}

void initCommodityTariff()
{
    require(server == nullptr, "the Commodity Tariff server is initialised twice");
    server = new TariffServer();
    checkChip(chip::app::CommandHandlerInterfaceRegistry::Instance().RegisterCommandHandler(server),
              "RegisterCommandHandler for Commodity Tariff");
    require(chip::app::AttributeAccessInterfaceRegistry::Instance().Register(server),
            "AttributeAccessInterfaceRegistry::Register for Commodity Tariff returned false");
}

void applyCommodityTariff(core::TariffTables tables)
{
    require(server != nullptr, "Commodity Tariff published before its server was initialised");
    server->update(std::move(tables));
}

}
