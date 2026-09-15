#pragma once

#include <tina/core/error/Result.hpp>
#include <tina/core/text/JsonDocument.hpp>
#include <tina/core/text/JsonWriter.hpp>
#include <tina/core/text/Utf8.hpp>

#include <array>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace Tina::Serialization {

namespace ErrorCode {
inline constexpr Core::ErrorCode InvalidData{Core::ErrorDomain::Serialization, 1};
inline constexpr Core::ErrorCode LimitExceeded{Core::ErrorDomain::Serialization, 2};
inline constexpr Core::ErrorCode UnknownType{Core::ErrorDomain::Serialization, 3};
inline constexpr Core::ErrorCode DuplicateIdentity{Core::ErrorDomain::Serialization, 4};
inline constexpr Core::ErrorCode CodecFailed{Core::ErrorDomain::Serialization, 5};
inline constexpr Core::ErrorCode UnresolvedReference{Core::ErrorDomain::Serialization, 6};
}

struct ArchiveLimits final {
    // Bounds the complete UTF-8 envelope. This is not a peak-allocation budget:
    // JSON DOMs, codec DTOs and the final byte buffer have separate storage.
    Core::usize maxBytes = 16U * 1024U * 1024U;
    // Same as JsonParseOptions: root depth is zero; values include scalar leaves.
    Core::usize maxDepth = 128;
    Core::usize maxNodes = 1'000'000;
};

template<class T, class Enable = void> struct Codec;
class ObjectWriter;
class ArrayWriter;

class Reader final {
  public:
    explicit Reader(Core::JsonValue value, std::string path = "$", ArchiveLimits limits = {})
        : value_(std::move(value)), path_(std::move(path)), limits_(limits) {}
    [[nodiscard]] const Core::JsonValue& value() const noexcept { return value_; }
    [[nodiscard]] Core::usize size() const noexcept { return value_.size(); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] Core::Error error(Core::ErrorCode code, std::string_view message) const
    { return Core::Error{code, message}.addContext("decode", path_); }
    [[nodiscard]] Core::Result<Reader> member(std::string_view key) const;
    // Linear enumeration avoids repeated name scans for large object maps.
    [[nodiscard]] Core::Result<std::vector<std::pair<std::string, Reader>>> members() const;
    [[nodiscard]] Core::Result<Reader> element(Core::usize index) const;
    [[nodiscard]] Core::Status fieldsOnly(std::initializer_list<std::string_view> fields) const;

    template<class T> [[nodiscard]] Core::Result<T> as() const
    {
        try { return Codec<T>::decode(*this); }
        catch (const std::bad_alloc&) { return Core::failure(error(Core::CoreErrorCode::OutOfMemory, "Decode allocation failed")); }
        catch (const std::exception& exception) { return Core::failure(error(ErrorCode::CodecFailed, exception.what())); }
        catch (...) { return Core::failure(error(ErrorCode::CodecFailed, "Decode callback threw")); }
    }
    template<class T> [[nodiscard]] Core::Result<T> field(std::string_view key) const
    {
        auto child = member(key);
        if (!child) return Core::failure(child.error());
        return child->template as<T>();
    }
    template<class T> [[nodiscard]] Core::Result<std::optional<T>> optionalField(std::string_view key) const
    {
        if (!value_.isObject()) return Core::failure(error(ErrorCode::InvalidData, "Expected object"));
        if (!value_.contains(key)) return std::optional<T>{};
        return field<std::optional<T>>(key);
    }
  private:
    Core::JsonValue value_;
    std::string path_;
    ArchiveLimits limits_;
};

namespace Detail {
struct WriteState final {
    Core::JsonWriter& output;
    ArchiveLimits limits;
    Core::usize nodes = 0;
    Core::usize stringBytes = 0;
    std::optional<Core::Error> failure;
};
}

// A single value position. It is borrowed only for the codec callback.
class Writer final {
  public:
    Writer(Detail::WriteState& state, std::optional<std::string_view> key, std::string path)
        : state_(state), key_(key), path_(std::move(path)) {}
    Writer(const Writer&) = delete;
    Writer& operator=(const Writer&) = delete;
    [[nodiscard]] Core::Status writeWith(const std::function<Core::Status(Writer&)>& write);
    [[nodiscard]] Core::Status null();
    [[nodiscard]] Core::Status string(std::string_view value);
    [[nodiscard]] Core::Status object(const std::function<Core::Status(ObjectWriter&)>& write);
    [[nodiscard]] Core::Status array(const std::function<Core::Status(ArrayWriter&)>& write);
    template<class T> requires std::is_arithmetic_v<T>
    [[nodiscard]] Core::Status number(T value)
    {
        if constexpr (std::is_floating_point_v<T>) {
            // Check the source range before narrowing an extended long double.
            if (!std::isfinite(value) || std::abs(value) > (std::numeric_limits<double>::max)())
                return fail("Non-finite or unrepresentable JSON number");
        }
        if (auto status = countValue(); !status) return status;
        if constexpr (std::is_same_v<T, bool>) {
            if (key_) state_.output.member(*key_, value); else state_.output.element(value);
        } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            if (key_) state_.output.member(*key_, static_cast<Core::i64>(value));
            else state_.output.element(static_cast<Core::i64>(value));
        } else if constexpr (std::is_integral_v<T>) {
            if (key_) state_.output.member(*key_, static_cast<Core::u64>(value));
            else state_.output.element(static_cast<Core::u64>(value));
        } else {
            if (key_) state_.output.member(*key_, static_cast<double>(value));
            else state_.output.element(static_cast<double>(value));
        }
        return status();
    }
    [[nodiscard]] Core::Status fail(std::string_view message, Core::ErrorCode code = ErrorCode::InvalidData) const
    {
        if (!state_.failure) state_.failure = Core::Error{code, message}.addContext("encode", path_);
        return Core::failure(*state_.failure);
    }
  private:
    [[nodiscard]] Core::Status countValue();
    [[nodiscard]] Core::Status status() const;
    Detail::WriteState& state_;
    std::optional<std::string_view> key_;
    std::string path_;
    bool written_ = false;
};

class ObjectWriter final {
  public:
    ObjectWriter(Detail::WriteState& state, std::string path) : state_(state), path_(std::move(path)) {}
    template<class T> [[nodiscard]] Core::Status field(std::string_view key, const T& value)
    { return fieldWith(key, [&](Writer& writer) { return Codec<T>::encode(writer, value); }); }
    [[nodiscard]] Core::Status fieldWith(std::string_view key, const std::function<Core::Status(Writer&)>& write);
  private:
    Detail::WriteState& state_;
    std::string path_;
    std::set<std::string, std::less<>> keys_;
};

class ArrayWriter final {
  public:
    ArrayWriter(Detail::WriteState& state, std::string path) : state_(state), path_(std::move(path)) {}
    template<class T> [[nodiscard]] Core::Status element(const T& value)
    { return elementWith([&](Writer& writer) { return Codec<T>::encode(writer, value); }); }
    [[nodiscard]] Core::Status elementWith(const std::function<Core::Status(Writer&)>& write);
  private:
    Detail::WriteState& state_;
    std::string path_;
    Core::usize index_ = 0;
};

// Explicit archive envelope v1. Only this current schema is accepted. No fallback.
[[nodiscard]] Core::Result<std::vector<std::byte>> encodeJsonWith(
    const std::function<Core::Status(Writer&)>& write, ArchiveLimits limits = {});
[[nodiscard]] Core::Result<Reader> decodeJson(std::span<const std::byte> bytes, ArchiveLimits limits = {});
template<class T> [[nodiscard]] Core::Result<std::vector<std::byte>> encodeJson(const T& value, ArchiveLimits limits = {})
{ return encodeJsonWith([&](Writer& writer) { return Codec<T>::encode(writer, value); }, limits); }
template<class T> [[nodiscard]] Core::Result<T> decodeJson(std::span<const std::byte> bytes, ArchiveLimits limits = {})
{
    auto reader = decodeJson(bytes, limits);
    if (!reader) return Core::failure(reader.error());
    return reader->template as<T>();
}

template<class T> struct Codec<T, std::enable_if_t<std::is_integral_v<T>>> {
    static Core::Status encode(Writer& writer, T value) { return writer.number(value); }
    static Core::Result<T> decode(const Reader& reader)
    {
        if constexpr (std::is_same_v<T, bool>) {
            auto value = reader.value().asBoolean();
            if (!value) return Core::failure(reader.error(ErrorCode::InvalidData, "Expected boolean"));
            return *value;
        }
        else if constexpr (std::is_signed_v<T>) {
            auto value = reader.value().asSignedInteger();
            if (!value) return Core::failure(reader.error(ErrorCode::InvalidData, "Expected signed integer"));
            if (*value < (std::numeric_limits<T>::min)() || *value > (std::numeric_limits<T>::max)())
                return Core::failure(reader.error(ErrorCode::InvalidData, "Signed integer out of range"));
            return static_cast<T>(*value);
        } else {
            auto value = reader.value().asUnsignedInteger();
            if (!value) return Core::failure(reader.error(ErrorCode::InvalidData, "Expected unsigned integer"));
            if (*value > (std::numeric_limits<T>::max)())
                return Core::failure(reader.error(ErrorCode::InvalidData, "Unsigned integer out of range"));
            return static_cast<T>(*value);
        }
    }
};

template<class T> struct Codec<T, std::enable_if_t<std::is_floating_point_v<T>>> {
    static Core::Status encode(Writer& writer, T value) { return writer.number(value); }
    static Core::Result<T> decode(const Reader& reader)
    {
        auto value = reader.value().asNumber();
        if (!value || !std::isfinite(*value) || std::abs(*value) > (std::numeric_limits<T>::max)())
            return Core::failure(reader.error(ErrorCode::InvalidData, "Expected finite representable number"));
        return static_cast<T>(*value);
    }
};

template<> struct Codec<std::string> {
    static Core::Status encode(Writer& writer, const std::string& value) { return writer.string(value); }
    static Core::Result<std::string> decode(const Reader& reader)
    {
        auto value = reader.value().asString();
        if (!value) return Core::failure(reader.error(ErrorCode::InvalidData, "Expected string"));
        return std::string(*value);
    }
};
template<> struct Codec<std::string_view> {
    static Core::Status encode(Writer& writer, std::string_view value) { return writer.string(value); }
    // Intentionally no decoder returning a dangling borrowed string.
};

template<class T> struct Codec<std::optional<T>> {
    static Core::Status encode(Writer& writer, const std::optional<T>& value)
    { return value ? Codec<T>::encode(writer, *value) : writer.null(); }
    static Core::Result<std::optional<T>> decode(const Reader& reader)
    {
        if (reader.value().isNull()) return std::optional<T>{};
        auto result = reader.template as<T>();
        if (!result) return Core::failure(result.error());
        return std::optional<T>{std::move(*result)};
    }
};

template<class T> struct Codec<std::vector<T>> {
    static Core::Status encode(Writer& writer, const std::vector<T>& values)
    {
        return writer.array([&](ArrayWriter& array) {
            for (const auto& value : values) if (auto status = array.element(value); !status) return status;
            return Core::success();
        });
    }
    static Core::Result<std::vector<T>> decode(const Reader& reader)
    {
        if (!reader.value().isArray()) return Core::failure(reader.error(ErrorCode::InvalidData, "Expected array"));
        std::vector<T> values;
        values.reserve(reader.size());
        for (Core::usize index = 0; index < reader.size(); ++index) {
            auto child = reader.element(index);
            if (!child) return Core::failure(child.error());
            auto value = child->template as<T>();
            if (!value) return Core::failure(value.error());
            values.push_back(std::move(*value));
        }
        return values;
    }
};

template<class T, Core::usize N> struct Codec<std::array<T, N>> {
    static Core::Status encode(Writer& writer, const std::array<T, N>& values)
    { return writer.array([&](ArrayWriter& array) {
        for (const auto& value : values) if (auto status = array.element(value); !status) return status;
        return Core::success();
    }); }
    static Core::Result<std::array<T, N>> decode(const Reader& reader)
    {
        if (!reader.value().isArray() || reader.size() != N)
            return Core::failure(reader.error(ErrorCode::InvalidData, "Array extent mismatch"));
        std::array<T, N> result{};
        for (Core::usize index = 0; index < N; ++index) {
            auto child = reader.element(index);
            if (!child) return Core::failure(child.error());
            auto value = child->template as<T>();
            if (!value) return Core::failure(value.error());
            result[index] = std::move(*value);
        }
        return result;
    }
};

template<class T> struct Codec<std::map<std::string, T>> {
    static Core::Status encode(Writer& writer, const std::map<std::string, T>& values)
    { return writer.object([&](ObjectWriter& object) {
        for (const auto& [key, value] : values) if (auto status = object.field(key, value); !status) return status;
        return Core::success();
    }); }
    static Core::Result<std::map<std::string, T>> decode(const Reader& reader)
    {
        auto members = reader.members();
        if (!members) return Core::failure(members.error());
        std::map<std::string, T> result;
        for (auto& [name, child] : *members) {
            auto value = child.template as<T>();
            if (!value) return Core::failure(value.error());
            result.emplace(std::move(name), std::move(*value));
        }
        return result;
    }
};

} // namespace Tina::Serialization
