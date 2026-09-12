#pragma once

#include <tina/core/base/Types.hpp>

#include <charconv>
#include <limits>
#include <string_view>
#include <system_error>
#include <type_traits>

namespace Tina::Core {

// Strict decimal integer parses for machine-written text: option values, settings files, and
// cooked-recipe tokens. The whole span must be consumed and the result must fit the target,
// so trailing garbage, surrounding spaces, and out-of-range values are rejections rather than
// truncations. The output is left untouched on failure, which is what lets callers chain
// several parses behind one `if` and still report the first bad field.
//
// Parse into a local value before checking full consumption and the target range.
// from_chars may successfully consume a prefix of invalid input ("12tail"); the
// caller's output must not change in that case either. No temporary string or
// NUL-terminated copy is needed.
//
// from_chars accepts no sign for unsigned targets, so "+5" and "-5" both fail parseUnsigned.
// Use parseSigned where a negative value is legitimate.
template <typename Value>
[[nodiscard]] bool parseUnsigned(std::string_view text, Value& out) noexcept
{
    static_assert(std::is_integral_v<Value> && std::is_unsigned_v<Value> &&
                      !std::is_same_v<Value, bool> && sizeof(Value) <= sizeof(u64),
                  "parseUnsigned requires an unsigned integer of at most 64 bits, not bool");

    if (text.empty())
    {
        return false;
    }

    u64 parsed = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return false;
    }
    if (parsed > static_cast<u64>((std::numeric_limits<Value>::max)()))
    {
        return false;
    }
    out = static_cast<Value>(parsed);
    return true;
}

// Signed counterpart. A leading '-' is accepted; a leading '+' is not, because from_chars
// rejects it and staying consistent with parseUnsigned matters more than accepting it here.
template <typename Value>
[[nodiscard]] bool parseSigned(std::string_view text, Value& out) noexcept
{
    static_assert(std::is_integral_v<Value> && std::is_signed_v<Value> && sizeof(Value) <= sizeof(i64),
                  "parseSigned requires a signed integer of at most 64 bits");

    if (text.empty())
    {
        return false;
    }

    i64 parsed = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return false;
    }
    if (parsed < static_cast<i64>((std::numeric_limits<Value>::min)()) ||
        parsed > static_cast<i64>((std::numeric_limits<Value>::max)()))
    {
        return false;
    }
    out = static_cast<Value>(parsed);
    return true;
}

} // namespace Tina::Core
