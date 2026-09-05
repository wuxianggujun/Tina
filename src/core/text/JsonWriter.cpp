#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/diagnostics/Assert.hpp>

#include <nlohmann/json.hpp>

#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace Tina::Core {

namespace {

using OrderedJson = nlohmann::ordered_json;

enum class Attachment : u8 {
    Root,
    ArrayElement,
    ObjectMember,
};

struct Frame final {
    OrderedJson value;
    char opening = '{';
    Attachment attachment = Attachment::Root;
    std::string key;
};

[[nodiscard]] std::string dumpCompact(const OrderedJson& value)
{
    return value.dump(
        -1,
        ' ',
        false,
        nlohmann::json::error_handler_t::replace);
}

} // namespace

struct JsonWriter::State final {
    std::vector<Frame> frames;
    bool failed = false;
};

JsonWriter::JsonWriter(std::ostream& output) noexcept : output_(&output)
{
    try
    {
        state_ = std::make_unique<State>();
    }
    catch (...)
    {
        state_.reset();
    }
}

JsonWriter::~JsonWriter() noexcept = default;

void JsonWriter::beginObject() noexcept
{
    if (!state_ || state_->failed)
    {
        return;
    }
    try
    {
        TINA_ASSERT(state_->frames.size() < MaximumDepth,
                    "JsonWriter nesting exceeded MaximumDepth");
        if (state_->frames.size() >= MaximumDepth)
        {
            state_->failed = true;
            return;
        }

        Attachment attachment = Attachment::Root;
        if (!state_->frames.empty())
        {
            TINA_ASSERT(state_->frames.back().value.is_array(),
                        "JsonWriter object element requires an array scope");
            if (!state_->frames.back().value.is_array())
            {
                state_->failed = true;
                return;
            }
            attachment = Attachment::ArrayElement;
        }

        Frame frame;
        frame.value = OrderedJson::object();
        frame.opening = '{';
        frame.attachment = attachment;
        state_->frames.push_back(std::move(frame));
    }
    catch (...)
    {
        state_->failed = true;
    }
}

void JsonWriter::endObject() noexcept
{
    if (!state_ || state_->failed)
    {
        return;
    }
    TINA_ASSERT(!state_->frames.empty(), "JsonWriter closed a scope that was never opened");
    if (state_->frames.empty() || state_->frames.back().opening != '{')
    {
        state_->failed = true;
        return;
    }

    try
    {
        Frame completed = std::move(state_->frames.back());
        state_->frames.pop_back();

        if (state_->frames.empty())
        {
            *output_ << dumpCompact(completed.value);
            if (!output_->good())
            {
                state_->failed = true;
            }
            return;
        }

        Frame& parent = state_->frames.back();
        if (completed.attachment == Attachment::ArrayElement && parent.value.is_array())
        {
            parent.value.push_back(std::move(completed.value));
        }
        else if (completed.attachment == Attachment::ObjectMember && parent.value.is_object())
        {
            parent.value[completed.key] = std::move(completed.value);
        }
        else
        {
            state_->failed = true;
        }
    }
    catch (...)
    {
        state_->failed = true;
    }
}

void JsonWriter::beginArray() noexcept
{
    if (!state_ || state_->failed)
    {
        return;
    }
    try
    {
        TINA_ASSERT(state_->frames.size() < MaximumDepth,
                    "JsonWriter nesting exceeded MaximumDepth");
        if (state_->frames.size() >= MaximumDepth)
        {
            state_->failed = true;
            return;
        }

        Attachment attachment = Attachment::Root;
        if (!state_->frames.empty())
        {
            TINA_ASSERT(state_->frames.back().value.is_array(),
                        "JsonWriter array element requires an array scope");
            if (!state_->frames.back().value.is_array())
            {
                state_->failed = true;
                return;
            }
            attachment = Attachment::ArrayElement;
        }

        Frame frame;
        frame.value = OrderedJson::array();
        frame.opening = '[';
        frame.attachment = attachment;
        state_->frames.push_back(std::move(frame));
    }
    catch (...)
    {
        state_->failed = true;
    }
}

void JsonWriter::endArray() noexcept
{
    if (!state_ || state_->failed)
    {
        return;
    }
    TINA_ASSERT(!state_->frames.empty(), "JsonWriter closed a scope that was never opened");
    if (state_->frames.empty() || state_->frames.back().opening != '[')
    {
        state_->failed = true;
        return;
    }

    try
    {
        Frame completed = std::move(state_->frames.back());
        state_->frames.pop_back();

        if (state_->frames.empty())
        {
            *output_ << dumpCompact(completed.value);
            if (!output_->good())
            {
                state_->failed = true;
            }
            return;
        }

        Frame& parent = state_->frames.back();
        if (completed.attachment == Attachment::ArrayElement && parent.value.is_array())
        {
            parent.value.push_back(std::move(completed.value));
        }
        else if (completed.attachment == Attachment::ObjectMember && parent.value.is_object())
        {
            parent.value[completed.key] = std::move(completed.value);
        }
        else
        {
            state_->failed = true;
        }
    }
    catch (...)
    {
        state_->failed = true;
    }
}

void JsonWriter::member(std::string_view key, std::string_view value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_object())
        {
            TINA_ASSERT(false, "JsonWriter wrote a member outside an object");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value[std::string(key)] = std::string(value);
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::member(std::string_view key, const char* value) noexcept
{
    member(key, value != nullptr ? std::string_view(value) : std::string_view{});
}

void JsonWriter::memberBoolean(std::string_view key, const bool value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_object())
        {
            TINA_ASSERT(false, "JsonWriter wrote a member outside an object");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value[std::string(key)] = value;
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::memberSigned(std::string_view key, const i64 value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_object())
        {
            TINA_ASSERT(false, "JsonWriter wrote a member outside an object");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value[std::string(key)] = value;
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::memberUnsigned(std::string_view key, const u64 value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_object())
        {
            TINA_ASSERT(false, "JsonWriter wrote a member outside an object");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value[std::string(key)] = value;
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::memberFloating(std::string_view key, const double value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_object())
        {
            TINA_ASSERT(false, "JsonWriter wrote a member outside an object");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value[std::string(key)] = value;
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::rawMember(std::string_view key, std::string_view jsonValueText) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_object())
        {
            TINA_ASSERT(false, "JsonWriter wrote a member outside an object");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }

        const auto parsed = OrderedJson::parse(
            jsonValueText.begin(),
            jsonValueText.end(),
            nullptr,
            true,
            false);
        state_->frames.back().value[std::string(key)] = parsed;
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::beginObjectMember(std::string_view key) noexcept
{
    if (!state_ || state_->failed)
    {
        return;
    }
    try
    {
        TINA_ASSERT(!state_->frames.empty() && state_->frames.back().value.is_object(),
                    "JsonWriter opened an object member outside an object");
        if (state_->frames.empty() || !state_->frames.back().value.is_object())
        {
            state_->failed = true;
            return;
        }
        TINA_ASSERT(state_->frames.size() < MaximumDepth,
                    "JsonWriter nesting exceeded MaximumDepth");
        if (state_->frames.size() >= MaximumDepth)
        {
            state_->failed = true;
            return;
        }

        Frame frame;
        frame.value = OrderedJson::object();
        frame.opening = '{';
        frame.attachment = Attachment::ObjectMember;
        frame.key = std::string(key);
        state_->frames.push_back(std::move(frame));
    }
    catch (...)
    {
        state_->failed = true;
    }
}

void JsonWriter::beginArrayMember(std::string_view key) noexcept
{
    if (!state_ || state_->failed)
    {
        return;
    }
    try
    {
        TINA_ASSERT(!state_->frames.empty() && state_->frames.back().value.is_object(),
                    "JsonWriter opened an array member outside an object");
        if (state_->frames.empty() || !state_->frames.back().value.is_object())
        {
            state_->failed = true;
            return;
        }
        TINA_ASSERT(state_->frames.size() < MaximumDepth,
                    "JsonWriter nesting exceeded MaximumDepth");
        if (state_->frames.size() >= MaximumDepth)
        {
            state_->failed = true;
            return;
        }

        Frame frame;
        frame.value = OrderedJson::array();
        frame.opening = '[';
        frame.attachment = Attachment::ObjectMember;
        frame.key = std::string(key);
        state_->frames.push_back(std::move(frame));
    }
    catch (...)
    {
        state_->failed = true;
    }
}

void JsonWriter::element(std::string_view value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_array())
        {
            TINA_ASSERT(false, "JsonWriter wrote an element outside an array");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value.push_back(std::string(value));
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::element(const char* value) noexcept
{
    element(value != nullptr ? std::string_view(value) : std::string_view{});
}

void JsonWriter::elementBoolean(const bool value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_array())
        {
            TINA_ASSERT(false, "JsonWriter wrote an element outside an array");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value.push_back(value);
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::elementSigned(const i64 value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_array())
        {
            TINA_ASSERT(false, "JsonWriter wrote an element outside an array");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value.push_back(value);
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::elementUnsigned(const u64 value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_array())
        {
            TINA_ASSERT(false, "JsonWriter wrote an element outside an array");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value.push_back(value);
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::elementFloating(const double value) noexcept
{
    try
    {
        if (!state_ || state_->failed || state_->frames.empty() ||
            !state_->frames.back().value.is_array())
        {
            TINA_ASSERT(false, "JsonWriter wrote an element outside an array");
            if (state_)
            {
                state_->failed = true;
            }
            return;
        }
        state_->frames.back().value.push_back(value);
    }
    catch (...)
    {
        if (state_)
        {
            state_->failed = true;
        }
    }
}

void JsonWriter::beginObjectElement() noexcept
{
    beginObject();
}

void JsonWriter::beginArrayElement() noexcept
{
    beginArray();
}

bool JsonWriter::balanced() const noexcept
{
    return state_ != nullptr && !state_->failed && state_->frames.empty();
}

usize JsonWriter::depth() const noexcept
{
    return state_ != nullptr ? state_->frames.size() : 0U;
}

bool JsonWriter::failed() const noexcept
{
    return state_ == nullptr || state_->failed;
}

void JsonWriter::writeString(std::string_view value) noexcept
{
    if (!state_ || state_->failed)
    {
        return;
    }
    try
    {
        *output_ << dumpCompact(OrderedJson(std::string(value)));
        if (!output_->good())
        {
            state_->failed = true;
        }
    }
    catch (...)
    {
        state_->failed = true;
    }
}

} // namespace Tina::Core
