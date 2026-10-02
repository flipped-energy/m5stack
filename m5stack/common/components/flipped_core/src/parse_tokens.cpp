#include "flipped/core/parse.h"

#include "json_reader.h"

namespace flipped::core {

namespace {

Token toToken(JsonVariantConst value)
{
    Token token;
    if (!value.is<JsonObjectConst>()) {
        return token;
    }
    JsonObjectConst object = value.as<JsonObjectConst>();
    token.tokenPreview = detail::stringField(object["tokenPreview"]);
    token.scope = detail::stringField(object["scope"]);
    token.expiresAt = detail::stringField(object["expiresAt"]);
    token.revokedAt = detail::stringField(object["revokedAt"]);
    return token;
}

}

Parsed<TokensBody> parseTokens(ByteSource &source)
{
    JsonDocument filter;
    JsonObject token = filter["tokens"].add<JsonObject>();
    token["tokenPreview"] = true;
    token["scope"] = true;
    token["expiresAt"] = true;
    token["revokedAt"] = true;

    detail::SourceReader reader(source);
    JsonDocument document;
    if (std::optional<std::string> error = detail::readObject(reader, document, filter)) {
        return Invalid{*error};
    }
    TokensBody body;
    body.tokens = detail::arrayField<Token>(document["tokens"], toToken);
    return body;
}

}
