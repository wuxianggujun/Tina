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
// Reading into the widest type and range-checking afterwards is deliberate: from_chars into a
// narrow target reports errc::result_out_of_range for an overflow, but a caller that only
// checks `ec == errc{}` would then read a partially-written value. Parsing wide and narrowing
// once keeps that impossible.
//
// from_chars accepts no sign for unsigned targets, so "+5" and "-5" both fail parseUnsigned.
// Use parseSigned where a negative value is legitimate.
template <typename Value>
[[nodiscard]] bool parseUnsigned(std::string_view text, Value& out) noexcept
{
    static_assert(std::is_unsigned_v<Value>, "parseUnsigned is for unsigned targets only");

    unsigned long long parsed = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return false;
    }
    if (parsed > static_cast<unsigned long long>((std::numeric_limits<Value>::max)()))
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
    static_assert(std::is_signed_v<Value> && std::is_integral_v<Value>,
                  "parseSigned is for signed integral targets only");

    long long parsed = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto result = std::from_chars(begin, end, parsed);
    if (result.ec != std::errc{} || result.ptr != end)
    {
        return false;
    }
    if (parsed < static_cast<long long>((std::numeric_limits<Value>::min)()) ||
        parsed > static_cast<long long>((std::numeric_limits<Value>::max)()))
    {
        return false;
    }
    out = static_cast<Value>(parsed);
    return true;
}

} // namespace Tina::Core
