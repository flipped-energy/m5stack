#include "flipped/core/parse.h"

#include "json_reader.h"

namespace flipped::core {

namespace {

void planFilter(JsonObject plan)
{
    plan["start"] = true;
    plan["end"] = true;
    JsonObject unit = plan["billingUnits"].add<JsonObject>();
    unit["billingUnitId"] = true;
    unit["name"] = true;
    unit["billingUnitType"] = true;
    unit["chargePerKwh"] = true;
    unit["chargePerKwhIncludingGst"] = true;
    unit["timeOfDayStartMinutes"] = true;
    unit["timeOfDayEndMinutes"] = true;
    unit["kwhStart"] = true;
    unit["kwhEnd"] = true;
    unit["capPerKwhIncludingGst"] = true;
}

BillingUnit toUnit(JsonVariantConst value)
{
    BillingUnit unit;
    if (!value.is<JsonObjectConst>()) {
        return unit;
    }
    JsonObjectConst object = value.as<JsonObjectConst>();
    unit.billingUnitId = detail::stringField(object["billingUnitId"]);
    unit.name = detail::stringField(object["name"]);
    unit.billingUnitType = detail::stringField(object["billingUnitType"]);
    unit.chargePerKwh = detail::numberField(object["chargePerKwh"]);
    unit.chargePerKwhIncludingGst = detail::numberField(object["chargePerKwhIncludingGst"]);
    unit.timeOfDayStartMinutes = detail::numberField(object["timeOfDayStartMinutes"]);
    unit.timeOfDayEndMinutes = detail::numberField(object["timeOfDayEndMinutes"]);
    unit.kwhStart = detail::numberField(object["kwhStart"]);
    unit.kwhEnd = detail::numberField(object["kwhEnd"]);
    unit.capPerKwhIncludingGst = detail::numberField(object["capPerKwhIncludingGst"]);
    return unit;
}

Plan toPlan(JsonObjectConst object)
{
    Plan plan;
    plan.start = detail::stringField(object["start"]);
    plan.end = detail::stringField(object["end"]);
    plan.billingUnits = detail::arrayField<BillingUnit>(object["billingUnits"], toUnit);
    return plan;
}

Product toProduct(JsonObjectConst object)
{
    Product product;
    product.gridType = detail::stringField(object["gridType"]);
    product.timeZone = detail::stringField(object["timeZone"]);
    product.currentPlan = detail::objectField<Plan>(object["currentPlan"], toPlan);
    product.upcomingPlan = detail::objectField<Plan>(object["upcomingPlan"], toPlan);
    return product;
}

Account toAccount(JsonVariantConst value)
{
    Account account;
    if (!value.is<JsonObjectConst>()) {
        return account;
    }
    JsonObjectConst object = value.as<JsonObjectConst>();
    account.accountNumber = detail::stringField(object["accountNumber"]);
    account.siteAddress = detail::stringField(object["siteAddress"]);
    account.accountState = detail::stringField(object["accountState"]);
    account.productName = detail::stringField(object["productName"]);
    account.product = detail::objectField<Product>(object["product"], toProduct);
    return account;
}

}

Parsed<AccountBody> parseAccount(ByteSource &source)
{
    JsonDocument filter;
    JsonObject account = filter["accounts"].add<JsonObject>();
    account["accountNumber"] = true;
    account["siteAddress"] = true;
    account["accountState"] = true;
    account["productName"] = true;
    JsonObject product = account["product"].to<JsonObject>();
    product["gridType"] = true;
    product["timeZone"] = true;
    planFilter(product["currentPlan"].to<JsonObject>());
    planFilter(product["upcomingPlan"].to<JsonObject>());

    detail::SourceReader reader(source);
    JsonDocument document;
    if (std::optional<std::string> error = detail::readObject(reader, document, filter)) {
        return Invalid{*error};
    }
    AccountBody body;
    body.accounts = detail::arrayField<Account>(document["accounts"], toAccount);
    return body;
}

}
