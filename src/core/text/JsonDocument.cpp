#include <tina/core/text/JsonDocument.hpp>
#include <tina/core/base/Types.hpp>
#include <tina/core/text/Utf8.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
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

// Build Tina nodes directly from nlohmann's SAX callbacks. This keeps parser
// memory proportional to the Tina document and lets depth/node limits abort
// before a second intermediate DOM is allocated.
class JsonSaxBuilder final : public nlohmann::json_sax<nlohmann::ordered_json>
{
  public:
    explicit JsonSaxBuilder(const JsonParseOptions& options) noexcept : options_(options) {}

    [[nodiscard]] std::shared_ptr<const JsonValue::Node> root() && noexcept
    {
        return std::move(root_);
    }

    [[nodiscard]] bool complete() const noexcept
    {
        return root_ != nullptr && frames_.empty();
    }

    [[nodiscard]] bool hasFailure() const noexcept
    {
        return failure_.has_value();
    }

    [[nodiscard]] Error takeFailure() &&
    {
        return std::move(*failure_);
    }

    bool null() override
    {
        auto node = makeNode(JsonValueKind::Null);
        return node && appendValue(std::move(node));
    }

    bool boolean(const bool value) override
    {
        auto node = makeNode(JsonValueKind::Boolean);
        if (!node)
        {
            return false;
        }
        node->booleanValue = value;
        return appendValue(std::move(node));
    }

    bool number_integer(const number_integer_t value) override
    {
        auto node = makeNode(JsonValueKind::Number);
        if (!node)
        {
            return false;
        }
        node->numberKind = JsonNumberKind::SignedInteger;
        node->signedValue = value;
        return appendValue(std::move(node));
    }

    bool number_unsigned(const number_unsigned_t value) override
    {
        auto node = makeNode(JsonValueKind::Number);
        if (!node)
        {
            return false;
        }
        node->numberKind = JsonNumberKind::UnsignedInteger;
        node->unsignedValue = value;
        return appendValue(std::move(node));
    }

    bool number_float(const number_float_t value, const string_t&) override
    {
        if (!std::isfinite(value))
        {
            setFailure(JsonErrorCode::InvalidValue, "JSON number is not finite");
            return false;
        }
        auto node = makeNode(JsonValueKind::Number);
        if (!node)
        {
            return false;
        }
        node->numberKind = JsonNumberKind::FloatingPoint;
        node->floatingValue = value;
        return appendValue(std::move(node));
    }

    bool string(string_t& value) override
    {
        auto node = makeNode(JsonValueKind::String);
        if (!node)
        {
            return false;
        }
        node->stringValue = std::move(value);
        return appendValue(std::move(node));
    }

    bool binary(binary_t&) override
    {
        setFailure(JsonErrorCode::InvalidValue, "JSON binary value is unsupported");
        return false;
    }

    bool start_object(Tina::Core::usize) override
    {
        auto node = makeNode(JsonValueKind::Object);
        if (!node || !appendValue(node))
        {
            return false;
        }
        frames_.push_back(Frame{std::move(node), true, {}, false});
        return true;
    }

    bool key(string_t& value) override
    {
        if (frames_.empty() || !frames_.back().object || frames_.back().hasKey)
        {
            setFailure(JsonErrorCode::ParseFailed, "JSON object key is out of order");
            return false;
        }
        frames_.back().key = std::move(value);
        frames_.back().hasKey = true;
        return true;
    }

    bool end_object() override
    {
        if (frames_.empty() || !frames_.back().object || frames_.back().hasKey)
        {
            setFailure(JsonErrorCode::ParseFailed, "JSON object ended before its value");
            return false;
        }
        frames_.pop_back();
        return true;
    }

    bool start_array(Tina::Core::usize) override
    {
        auto node = makeNode(JsonValueKind::Array);
        if (!node || !appendValue(node))
        {
            return false;
        }
        frames_.push_back(Frame{std::move(node), false, {}, false});
        return true;
    }

    bool end_array() override
    {
        if (frames_.empty() || frames_.back().object)
        {
            setFailure(JsonErrorCode::ParseFailed, "JSON array ended out of order");
            return false;
        }
        frames_.pop_back();
        return true;
    }

    bool parse_error(const Tina::Core::usize position, const std::string&, const nlohmann::detail::exception& exception) override
    {
        setFailure(JsonErrorCode::ParseFailed, exception.what());
        if (failure_)
        {
            failure_->setNativeCode(static_cast<i64>(position));
        }
        return false;
    }

  private:
    struct Frame final
    {
        std::shared_ptr<JsonValue::Node> node;
        bool object = false;
        std::string key;
        bool hasKey = false;
    };

    [[nodiscard]] std::shared_ptr<JsonValue::Node> makeNode(const JsonValueKind kind)
    {
        const usize depth = frames_.size();
        if (depth > options_.maxDepth)
        {
            setFailure(JsonErrorCode::LimitExceeded, "JSON nesting or node limit exceeded");
            return {};
        }
        if (nodeCount_ >= options_.maxNodes)
        {
            setFailure(JsonErrorCode::LimitExceeded, "JSON nesting or node limit exceeded");
            return {};
        }
        try
        {
            auto node = std::make_shared<JsonValue::Node>();
            node->kind = kind;
            ++nodeCount_;
            return node;
        }
        catch (const std::bad_alloc&)
        {
            setFailure(CoreErrorCode::OutOfMemory, "JSON DOM allocation failed");
            return {};
        }
    }

    [[nodiscard]] bool appendValue(const std::shared_ptr<JsonValue::Node>& node)
    {
        if (node == nullptr)
        {
            return false;
        }
        try
        {
            if (frames_.empty())
            {
                if (root_ != nullptr)
                {
                    setFailure(JsonErrorCode::ParseFailed, "JSON document contains multiple root values");
                    return false;
                }
                root_ = node;
                return true;
            }

            Frame& parent = frames_.back();
            if (parent.object)
            {
                if (!parent.hasKey)
                {
                    setFailure(JsonErrorCode::ParseFailed, "JSON object value has no key");
                    return false;
                }
                const auto existing = std::find_if(
                    parent.node->objectValues.begin(), parent.node->objectValues.end(),
                    [&parent](const auto& entry) { return entry.first == parent.key; });
                if (existing != parent.node->objectValues.end())
                {
                    existing->second = node;
                }
                else
                {
                    parent.node->objectValues.emplace_back(parent.key, node);
                }
                parent.key.clear();
                parent.hasKey = false;
            }
            else
            {
                parent.node->arrayValues.push_back(node);
            }
            return true;
        }
        catch (const std::bad_alloc&)
        {
            setFailure(CoreErrorCode::OutOfMemory, "JSON DOM allocation failed");
            return false;
        }
    }

    void setFailure(const ErrorCode code, std::string_view message)
    {
        if (!failure_)
        {
            failure_.emplace(code, message);
        }
    }

    const JsonParseOptions& options_;
    usize nodeCount_ = 0U;
    std::shared_ptr<JsonValue::Node> root_;
    std::vector<Frame> frames_;
    std::optional<Error> failure_;
};

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
    if (isNumber() && node_->numberKind == JsonNumberKind::UnsignedInteger)
    {
        if (node_->unsignedValue > static_cast<u64>((std::numeric_limits<i64>::max)()))
        { return failure(JsonErrorCode::InvalidValue, "JSON integer exceeds the signed 64-bit range"); }
        return static_cast<i64>(node_->unsignedValue);
    }
    if (!isNumber() || node_->numberKind != JsonNumberKind::SignedInteger)
    {
        return failure(typeMismatch("signed integer"));
    }
    return node_->signedValue;
}

Result<u64> JsonValue::asUnsignedInteger() const
{
    if (isNumber() && node_->numberKind == JsonNumberKind::SignedInteger)
    {
        if (node_->signedValue < 0)
        { return failure(JsonErrorCode::InvalidValue, "Negative JSON integer cannot be read as unsigned"); }
        return static_cast<u64>(node_->signedValue);
    }
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
        JsonSaxBuilder builder{options};
        const bool parsed = nlohmann::ordered_json::sax_parse(
            text.begin(), text.end(), &builder, nlohmann::json::input_format_t::json, true);
        if (!parsed || builder.hasFailure())
        {
            if (builder.hasFailure())
            {
                return failure(std::move(builder).takeFailure());
            }
            return failure(JsonErrorCode::ParseFailed, "JSON SAX parser rejected input");
        }
        if (!builder.complete())
        {
            return failure(JsonErrorCode::ParseFailed, "JSON document is incomplete");
        }
        return JsonDocument{std::move(builder).root()};
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
