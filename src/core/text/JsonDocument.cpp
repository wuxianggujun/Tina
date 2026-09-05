#include <tina/core/text/JsonDocument.hpp>
#include <tina/core/text/Utf8.hpp>

#include <nlohmann/json.hpp>

#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <string>
#include <utility>

namespace Tina::Core {

struct JsonValue::Node final {
    JsonValueKind kind = JsonValueKind::Invalid;
    JsonNumberKind numberKind = JsonNumberKind::SignedInteger;
    bool booleanValue = false;
    i64 signedValue = 0;
    u64 unsignedValue = 0;
    double floatingValue = 0.0;
    std::string stringValue;
    std::vector<std::shared_ptr<const Node>> arrayValues;
    std::vector<std::pair<std::string, std::shared_ptr<const Node>>> objectValues;
};

namespace {

[[nodiscard]] Error typeMismatch(std::string_view expected)
{
    std::string message = "JSON value is not a ";
    message.append(expected);
    return Error{JsonErrorCode::TypeMismatch, message};
}

[[nodiscard]] Error allocationFailure(std::string_view operation)
{
    return Error{CoreErrorCode::OutOfMemory, operation};
}

[[nodiscard]] Result<std::shared_ptr<const JsonValue::Node>> makeNode(
    const nlohmann::ordered_json& value,
    const usize depth,
    const JsonParseOptions& options,
    usize& nodeCount)
{
    if (depth > options.maxDepth || nodeCount >= options.maxNodes)
    {
        return failure(JsonErrorCode::LimitExceeded, "JSON nesting or node limit exceeded");
    }
    ++nodeCount;

    try
    {
        auto node = std::make_shared<JsonValue::Node>();
        if (value.is_null())
        {
            node->kind = JsonValueKind::Null;
            return std::shared_ptr<const JsonValue::Node>{std::move(node)};
        }
        if (value.is_boolean())
        {
            node->kind = JsonValueKind::Boolean;
            node->booleanValue = value.get<bool>();
            return std::shared_ptr<const JsonValue::Node>{std::move(node)};
        }
        if (value.is_number_unsigned())
        {
            node->kind = JsonValueKind::Number;
            node->numberKind = JsonNumberKind::UnsignedInteger;
            node->unsignedValue = value.get<u64>();
            return std::shared_ptr<const JsonValue::Node>{std::move(node)};
        }
        if (value.is_number_integer())
        {
            node->kind = JsonValueKind::Number;
            node->numberKind = JsonNumberKind::SignedInteger;
            node->signedValue = value.get<i64>();
            return std::shared_ptr<const JsonValue::Node>{std::move(node)};
        }
        if (value.is_number_float())
        {
            node->kind = JsonValueKind::Number;
            node->numberKind = JsonNumberKind::FloatingPoint;
            node->floatingValue = value.get<double>();
            if (!std::isfinite(node->floatingValue))
            {
                return failure(JsonErrorCode::InvalidValue, "JSON number is not finite");
            }
            return std::shared_ptr<const JsonValue::Node>{std::move(node)};
        }
        if (value.is_string())
        {
            node->kind = JsonValueKind::String;
            node->stringValue = value.get<std::string>();
            return std::shared_ptr<const JsonValue::Node>{std::move(node)};
        }
        if (value.is_array())
        {
            node->kind = JsonValueKind::Array;
            node->arrayValues.reserve(value.size());
            for (const auto& child : value)
            {
                auto converted = makeNode(child, depth + 1U, options, nodeCount);
                if (!converted)
                {
                    return failure(std::move(converted.error()));
                }
                node->arrayValues.push_back(std::move(*converted));
            }
            return std::shared_ptr<const JsonValue::Node>{std::move(node)};
        }
        if (value.is_object())
        {
            node->kind = JsonValueKind::Object;
            node->objectValues.reserve(value.size());
            for (const auto& [key, child] : value.items())
            {
                auto converted = makeNode(child, depth + 1U, options, nodeCount);
                if (!converted)
                {
                    return failure(std::move(converted.error()));
                }
                node->objectValues.emplace_back(key, std::move(*converted));
            }
            return std::shared_ptr<const JsonValue::Node>{std::move(node)};
        }
        return failure(JsonErrorCode::InvalidValue, "JSON value kind is unsupported");
    }
    catch (const std::bad_alloc&)
    {
        return failure(allocationFailure("JSON DOM allocation failed"));
    }
}

} // namespace

JsonValueKind JsonValue::kind() const noexcept
{
    return node_ ? node_->kind : JsonValueKind::Invalid;
}

bool JsonValue::isValid() const noexcept
{
    return static_cast<bool>(node_);
}

bool JsonValue::isNull() const noexcept
{
    return node_ && node_->kind == JsonValueKind::Null;
}

bool JsonValue::isBoolean() const noexcept
{
    return node_ && node_->kind == JsonValueKind::Boolean;
}

bool JsonValue::isNumber() const noexcept
{
    return node_ && node_->kind == JsonValueKind::Number;
}

bool JsonValue::isString() const noexcept
{
    return node_ && node_->kind == JsonValueKind::String;
}

bool JsonValue::isArray() const noexcept
{
    return node_ && node_->kind == JsonValueKind::Array;
}

bool JsonValue::isObject() const noexcept
{
    return node_ && node_->kind == JsonValueKind::Object;
}

std::optional<JsonNumberKind> JsonValue::numberKind() const noexcept
{
    if (!isNumber())
    {
        return std::nullopt;
    }
    return node_->numberKind;
}

Result<bool> JsonValue::asBoolean() const
{
    if (!isBoolean())
    {
        return failure(typeMismatch("boolean"));
    }
    return node_->booleanValue;
}

Result<i64> JsonValue::asSignedInteger() const
{
    if (!isNumber() || node_->numberKind != JsonNumberKind::SignedInteger)
    {
        return failure(typeMismatch("signed integer"));
    }
    return node_->signedValue;
}

Result<u64> JsonValue::asUnsignedInteger() const
{
    if (!isNumber() || node_->numberKind != JsonNumberKind::UnsignedInteger)
    {
        return failure(typeMismatch("unsigned integer"));
    }
    return node_->unsignedValue;
}

Result<double> JsonValue::asNumber() const
{
    if (!isNumber())
    {
        return failure(typeMismatch("number"));
    }
    switch (node_->numberKind)
    {
    case JsonNumberKind::SignedInteger:
        return static_cast<double>(node_->signedValue);
    case JsonNumberKind::UnsignedInteger:
        return static_cast<double>(node_->unsignedValue);
    case JsonNumberKind::FloatingPoint:
        return node_->floatingValue;
    }
    return failure(JsonErrorCode::InvalidValue, "JSON number kind is invalid");
}

Result<std::string_view> JsonValue::asString() const
{
    if (!isString())
    {
        return failure(typeMismatch("string"));
    }
    return std::string_view{node_->stringValue};
}

usize JsonValue::size() const noexcept
{
    if (!node_)
    {
        return 0U;
    }
    if (node_->kind == JsonValueKind::Array)
    {
        return node_->arrayValues.size();
    }
    if (node_->kind == JsonValueKind::Object)
    {
        return node_->objectValues.size();
    }
    return 0U;
}

bool JsonValue::contains(const std::string_view key) const noexcept
{
    if (!isObject())
    {
        return false;
    }
    for (const auto& [memberName, memberValue] : node_->objectValues)
    {
        static_cast<void>(memberValue);
        if (memberName == key)
        {
            return true;
        }
    }
    return false;
}

Result<JsonValue> JsonValue::member(const std::string_view key) const
{
    if (!isObject())
    {
        return failure(typeMismatch("object"));
    }
    for (const auto& [memberName, memberValue] : node_->objectValues)
    {
        if (memberName == key)
        {
            return JsonValue{memberValue};
        }
    }
    return failure(JsonErrorCode::MemberNotFound, "JSON object member was not found");
}

Result<JsonValue> JsonValue::element(const usize index) const
{
    if (!isArray())
    {
        return failure(typeMismatch("array"));
    }
    if (index >= node_->arrayValues.size())
    {
        return failure(JsonErrorCode::IndexOutOfRange, "JSON array index is out of range");
    }
    return JsonValue{node_->arrayValues[index]};
}

Result<std::vector<JsonValue>> JsonValue::elements() const
{
    if (!isArray())
    {
        return failure(typeMismatch("array"));
    }
    try
    {
        std::vector<JsonValue> values;
        values.reserve(node_->arrayValues.size());
        for (const auto& value : node_->arrayValues)
        {
            values.push_back(JsonValue{value});
        }
        return values;
    }
    catch (const std::bad_alloc&)
    {
        return failure(allocationFailure("JSON array view allocation failed"));
    }
}

Result<std::vector<std::string>> JsonValue::memberNames() const
{
    if (!isObject())
    {
        return failure(typeMismatch("object"));
    }
    try
    {
        std::vector<std::string> names;
        names.reserve(node_->objectValues.size());
        for (const auto& [name, value] : node_->objectValues)
        {
            static_cast<void>(value);
            names.push_back(name);
        }
        return names;
    }
    catch (const std::bad_alloc&)
    {
        return failure(allocationFailure("JSON member name allocation failed"));
    }
}

Result<JsonDocument> JsonDocument::parse(
    const std::string_view text, const JsonParseOptions options)
{
    if (text.size() > options.maxInputBytes)
    {
        return failure(JsonErrorCode::LimitExceeded, "JSON input exceeds maxInputBytes");
    }
    if (!isStrictUtf8(text))
    {
        return failure(JsonErrorCode::ParseFailed, "JSON input is not strict UTF-8");
    }
    try
    {
        const auto parsed = nlohmann::ordered_json::parse(
            text.begin(),
            text.end(),
            nullptr,
            true,
            false);
        usize nodeCount = 0U;
        auto root = makeNode(parsed, 0U, options, nodeCount);
        if (!root)
        {
            return failure(std::move(root.error()));
        }
        return JsonDocument{std::move(*root)};
    }
    catch (const nlohmann::json::parse_error& exception)
    {
        Error error{JsonErrorCode::ParseFailed, exception.what()};
        error.setNativeCode(static_cast<i64>(exception.byte));
        return failure(std::move(error));
    }
    catch (const nlohmann::json::exception& exception)
    {
        return failure(JsonErrorCode::ParseFailed, exception.what());
    }
    catch (const std::bad_alloc&)
    {
        return failure(allocationFailure("JSON parser allocation failed"));
    }
    catch (const std::exception& exception)
    {
        return failure(JsonErrorCode::ParseFailed, exception.what());
    }
}

Result<JsonDocument> JsonDocument::parse(
    const std::span<const std::byte> bytes, const JsonParseOptions options)
{
    if (bytes.size() > options.maxInputBytes)
    {
        return failure(JsonErrorCode::LimitExceeded, "JSON input exceeds maxInputBytes");
    }
    if (bytes.empty())
    {
        return parse(std::string_view{}, options);
    }
    const auto chars = std::string_view{
        reinterpret_cast<const char*>(bytes.data()),
        bytes.size(),
    };
    return parse(chars, options);
}

} // namespace Tina::Core
