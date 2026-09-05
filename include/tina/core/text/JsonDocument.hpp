#pragma once

#include <tina/core/base/Types.hpp>
#include <tina/core/error/Result.hpp>

#include <memory>
#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Tina::Core {

// The parser is backed by the vendored nlohmann/json implementation, but the
// public surface is Tina-owned so SDK users do not depend on its ABI or macros.
enum class JsonValueKind : u8 {
    Invalid = 0,
    Null,
    Boolean,
    Number,
    String,
    Array,
    Object,
};

enum class JsonNumberKind : u8 {
    SignedInteger = 0,
    UnsignedInteger,
    FloatingPoint,
};

namespace JsonErrorCode {

inline constexpr ErrorCode ParseFailed{ErrorDomain::Core, 11};
inline constexpr ErrorCode TypeMismatch{ErrorDomain::Core, 12};
inline constexpr ErrorCode MemberNotFound{ErrorDomain::Core, 13};
inline constexpr ErrorCode IndexOutOfRange{ErrorDomain::Core, 14};
inline constexpr ErrorCode LimitExceeded{ErrorDomain::Core, 15};
inline constexpr ErrorCode InvalidValue{ErrorDomain::Core, 16};

} // namespace JsonErrorCode

struct JsonParseOptions final {
    // maxInputBytes is checked before parsing. maxDepth/maxNodes currently bound
    // the Tina node conversion after the parser has built its intermediate DOM;
    // they do not bound that parser's peak memory or nesting work.
    usize maxInputBytes = 16U * 1024U * 1024U;
    usize maxDepth = 128U;
    usize maxNodes = 1'000'000U;
};

class JsonValue final {
public:
    struct Node;

    JsonValue() noexcept = default;
    JsonValue(const JsonValue&) noexcept = default;
    JsonValue& operator=(const JsonValue&) noexcept = default;
    JsonValue(JsonValue&&) noexcept = default;
    JsonValue& operator=(JsonValue&&) noexcept = default;
    ~JsonValue() = default;

    [[nodiscard]] JsonValueKind kind() const noexcept;
    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] bool isNull() const noexcept;
    [[nodiscard]] bool isBoolean() const noexcept;
    [[nodiscard]] bool isNumber() const noexcept;
    [[nodiscard]] bool isString() const noexcept;
    [[nodiscard]] bool isArray() const noexcept;
    [[nodiscard]] bool isObject() const noexcept;

    [[nodiscard]] std::optional<JsonNumberKind> numberKind() const noexcept;
    [[nodiscard]] Result<bool> asBoolean() const;
    [[nodiscard]] Result<i64> asSignedInteger() const;
    [[nodiscard]] Result<u64> asUnsignedInteger() const;
    [[nodiscard]] Result<double> asNumber() const;
    [[nodiscard]] Result<std::string_view> asString() const;

    [[nodiscard]] usize size() const noexcept;
    [[nodiscard]] bool contains(std::string_view key) const noexcept;
    [[nodiscard]] Result<JsonValue> member(std::string_view key) const;
    [[nodiscard]] Result<JsonValue> element(usize index) const;
    [[nodiscard]] Result<std::vector<JsonValue>> elements() const;
    [[nodiscard]] Result<std::vector<std::string>> memberNames() const;

private:
    explicit JsonValue(std::shared_ptr<const Node> node) noexcept : node_(std::move(node)) {}

    std::shared_ptr<const Node> node_;
    friend class JsonDocument;
};

class JsonDocument final {
public:
    JsonDocument() noexcept = default;
    JsonDocument(const JsonDocument&) noexcept = default;
    JsonDocument& operator=(const JsonDocument&) noexcept = default;
    JsonDocument(JsonDocument&&) noexcept = default;
    JsonDocument& operator=(JsonDocument&&) noexcept = default;
    ~JsonDocument() = default;

    [[nodiscard]] static Result<JsonDocument> parse(
        std::string_view text, JsonParseOptions options = {});

    [[nodiscard]] static Result<JsonDocument> parse(
        std::span<const std::byte> bytes, JsonParseOptions options = {});

    [[nodiscard]] bool isValid() const noexcept { return static_cast<bool>(root_); }
    [[nodiscard]] JsonValue root() const noexcept { return JsonValue(root_); }

private:
    explicit JsonDocument(std::shared_ptr<const JsonValue::Node> root) noexcept
        : root_(std::move(root))
    {
    }

    std::shared_ptr<const JsonValue::Node> root_;
};

} // namespace Tina::Core
