#pragma once

#include <tina/core/base/Types.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <optional>
#include <string_view>

namespace Tina::Core {

// Longest text this accepts. A float has at most 9 significant decimal digits, so anything
// past this is a malformed field rather than a precise one, and the bound keeps the
// NUL-terminated copy on the stack.
inline constexpr usize MaximumParsedFloatBytes = 63;

// Strict decimal float parse: the whole span must be consumed and the result must be
// finite. Rejects empty text, trailing characters, hex/infinity/nan spellings that strtof
// would otherwise accept, and anything longer than MaximumParsedFloatBytes.
//
// std::from_chars would be the natural choice and is what this replaces. Its floating-point
// overloads are absent from libc++ through NDK 28 (there is no
// `__charconv/from_chars_floating_point.h`; only the integral and to_chars halves ship), so
// a single call site using them makes the whole module uncompilable for Android. The
// integer overloads are present and should still be used directly.
//
// strtof is locale-sensitive in principle -- a locale whose decimal point is ',' would parse
// "1.5" as 1. Tina never calls setlocale and never imbues a stream, so the process stays in
// the "C" locale for its whole lifetime and the radix character is '.' everywhere. Cooked
// asset text and settings files are machine-written ASCII, so that is the correct reading
// regardless of the user's display language.
[[nodiscard]] inline std::optional<float> parseStrictFloat(std::string_view text) noexcept
{
    if (text.empty() || text.size() > MaximumParsedFloatBytes)
    {
        return std::nullopt;
    }
    // strtof skips leading whitespace and accepts "0x1p3", "inf" and "nan"; from_chars with
    // chars_format::general accepts none of those. Reject them up front so the two agree.
    const auto isDecimalFloatByte = [](const char byte) noexcept {
        return (byte >= '0' && byte <= '9') || byte == '+' || byte == '-' || byte == '.' ||
               byte == 'e' || byte == 'E';
    };
    if (!std::all_of(text.begin(), text.end(), isDecimalFloatByte))
    {
        return std::nullopt;
    }

    std::array<char, MaximumParsedFloatBytes + 1U> buffer{};
    std::copy(text.begin(), text.end(), buffer.begin());

    char* end = nullptr;
    errno = 0;
    const float parsed = std::strtof(buffer.data(), &end);
    // A partial parse is a rejection, not a value: "1.5abc" must fail rather than yield 1.5.
    if (errno != 0 || end != buffer.data() + text.size() || !std::isfinite(parsed))
    {
        return std::nullopt;
    }
    return parsed;
}

} // namespace Tina::Core
