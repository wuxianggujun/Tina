#pragma once

#include <tina/core/base/Types.hpp>

#include <memory>
#include <ostream>
#include <string_view>
#include <type_traits>

namespace Tina::Core {

// Stream-oriented JSON report writer backed by Tina's vendored nlohmann/json implementation.
// The public API stays Tina-owned; the third-party DOM and serializer are private to the core
// library. Values are accumulated in an ordered DOM and emitted as one compact JSON value when a
// top-level scope closes.
class JsonWriter
{
public:
    static constexpr usize MaximumDepth = 16;

    explicit JsonWriter(std::ostream& output) noexcept;
    ~JsonWriter() noexcept;

    JsonWriter(const JsonWriter&) = delete;
    JsonWriter& operator=(const JsonWriter&) = delete;
    JsonWriter(JsonWriter&&) = delete;
    JsonWriter& operator=(JsonWriter&&) = delete;

    void beginObject() noexcept;
    void endObject() noexcept;
    void beginArray() noexcept;
    void endArray() noexcept;

    void member(std::string_view key, std::string_view value) noexcept;
    void member(std::string_view key, const char* value) noexcept;

    template <typename Value>
        requires std::is_same_v<std::remove_cv_t<Value>, bool>
    void member(std::string_view key, Value value) noexcept
    {
        memberBoolean(key, value);
    }

    template <typename Value>
        requires(std::is_arithmetic_v<Value> && !std::is_same_v<std::remove_cv_t<Value>, bool> &&
                 !std::is_same_v<std::remove_cv_t<Value>, char> &&
                 !std::is_same_v<std::remove_cv_t<Value>, signed char> &&
                 !std::is_same_v<std::remove_cv_t<Value>, unsigned char>)
    void member(std::string_view key, Value value) noexcept
    {
        if constexpr (std::is_floating_point_v<Value>)
        {
            memberFloating(key, static_cast<double>(value));
        }
        else if constexpr (std::is_signed_v<Value>)
        {
            memberSigned(key, static_cast<i64>(value));
        }
        else
        {
            memberUnsigned(key, static_cast<u64>(value));
        }
    }

    // Parses and inserts a complete JSON value. Unlike the old writer, malformed raw values are
    // rejected and leave the writer in a failed state instead of producing invalid output.
    void rawMember(std::string_view key, std::string_view jsonValueText) noexcept;

    void beginObjectMember(std::string_view key) noexcept;
    void beginArrayMember(std::string_view key) noexcept;

    void element(std::string_view value) noexcept;
    void element(const char* value) noexcept;

    template <typename Value>
        requires std::is_same_v<std::remove_cv_t<Value>, bool>
    void element(Value value) noexcept
    {
        elementBoolean(value);
    }

    template <typename Value>
        requires(std::is_arithmetic_v<Value> && !std::is_same_v<std::remove_cv_t<Value>, bool> &&
                 !std::is_same_v<std::remove_cv_t<Value>, char> &&
                 !std::is_same_v<std::remove_cv_t<Value>, signed char> &&
                 !std::is_same_v<std::remove_cv_t<Value>, unsigned char>)
    void element(Value value) noexcept
    {
        if constexpr (std::is_floating_point_v<Value>)
        {
            elementFloating(static_cast<double>(value));
        }
        else if constexpr (std::is_signed_v<Value>)
        {
            elementSigned(static_cast<i64>(value));
        }
        else
        {
            elementUnsigned(static_cast<u64>(value));
        }
    }

    void beginObjectElement() noexcept;
    void beginArrayElement() noexcept;

    [[nodiscard]] bool balanced() const noexcept;
    [[nodiscard]] usize depth() const noexcept;
    [[nodiscard]] bool failed() const noexcept;

    // Writes one standalone JSON string value. This does not alter the scope stack.
    void writeString(std::string_view value) noexcept;

private:
    struct State;

    void memberBoolean(std::string_view key, bool value) noexcept;
    void memberSigned(std::string_view key, i64 value) noexcept;
    void memberUnsigned(std::string_view key, u64 value) noexcept;
    void memberFloating(std::string_view key, double value) noexcept;
    void elementBoolean(bool value) noexcept;
    void elementSigned(i64 value) noexcept;
    void elementUnsigned(u64 value) noexcept;
    void elementFloating(double value) noexcept;

    std::ostream* output_ = nullptr;
    std::unique_ptr<State> state_;
};

} // namespace Tina::Core
