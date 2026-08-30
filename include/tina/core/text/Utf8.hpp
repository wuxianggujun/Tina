#pragma once

#include <tina/core/base/Types.hpp>

#include <limits>
#include <optional>
#include <span>
#include <string_view>

namespace Tina::Core {

[[nodiscard]] constexpr std::optional<u32> countStrictUtf8CodepointsWithoutNul(std::string_view text) noexcept
{
    usize index = 0;
    u32 codepointCount = 0;
    while (index < text.size())
    {
        const auto first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7FU)
        {
            if (first == 0U)
            {
                return std::nullopt;
            }
            if (codepointCount == (std::numeric_limits<u32>::max)())
            {
                return std::nullopt;
            }
            ++index;
            ++codepointCount;
            continue;
        }

        usize continuationCount = 0;
        char32_t codepoint = 0;
        char32_t minimumCodepoint = 0;
        if ((first & 0xE0U) == 0xC0U)
        {
            continuationCount = 1;
            codepoint = first & 0x1FU;
            minimumCodepoint = 0x80U;
        } else if ((first & 0xF0U) == 0xE0U)
        {
            continuationCount = 2;
            codepoint = first & 0x0FU;
            minimumCodepoint = 0x800U;
        } else if ((first & 0xF8U) == 0xF0U)
        {
            continuationCount = 3;
            codepoint = first & 0x07U;
            minimumCodepoint = 0x10000U;
        } else
        {
            return std::nullopt;
        }

        if (continuationCount > text.size() - index - 1U)
        {
            return std::nullopt;
        }
        for (usize offset = 1; offset <= continuationCount; ++offset)
        {
            const auto next = static_cast<unsigned char>(text[index + offset]);
            if ((next & 0xC0U) != 0x80U)
            {
                return std::nullopt;
            }
            codepoint = (codepoint << 6U) | (next & 0x3FU);
        }
        if (codepoint < minimumCodepoint || codepoint > 0x10FFFFU || (codepoint >= 0xD800U && codepoint <= 0xDFFFU))
        {
            return std::nullopt;
        }
        index += continuationCount + 1U;
        if (codepointCount == (std::numeric_limits<u32>::max)())
        {
            return std::nullopt;
        }
        ++codepointCount;
    }
    return codepointCount;
}

[[nodiscard]] constexpr bool isStrictUtf8WithoutNul(std::string_view text) noexcept
{
    return countStrictUtf8CodepointsWithoutNul(text).has_value();
}

// Converts UTF-16 to strict UTF-8, writing into caller-provided storage.
//
// Exists because platform IMEs speak UTF-16 while every Tina text contract is strict UTF-8. Android's
// JNI GetStringChars gives UTF-16 directly; its GetStringUTFChars gives *modified* UTF-8 instead, which
// encodes NUL as two bytes and splits non-BMP characters into CESU-8 surrogate pairs -- so an emoji
// arrives as two invalid three-byte sequences that strict validation rightly rejects. Converting from
// UTF-16 is the only way to get those characters through intact.
//
// Returns the byte count written, or nullopt when the input cannot be represented: an unpaired
// surrogate, an embedded NUL, or output that does not fit.
//
// On failure the output span may hold partially converted bytes, so the returned count is the only
// authority on how much is valid -- a caller must not read the span after a nullopt. That is cheaper
// than clearing on every failure path, and callers already have to check the result to know the length.
//
// Not constexpr-only like the validators above: it writes through a span, and the caller owns the
// storage so this allocates nothing.
[[nodiscard]] constexpr std::optional<usize> convertUtf16ToStrictUtf8(std::u16string_view utf16,
                                                                     std::span<char> output) noexcept
{
    usize written = 0;
    for (usize index = 0; index < utf16.size(); ++index)
    {
        char32_t codepoint = utf16[index];

        // High surrogate: must be followed by a low surrogate. An unpaired one is not a character at
        // all, and passing it through would produce the same invalid bytes CESU-8 does.
        if (codepoint >= 0xD800U && codepoint <= 0xDBFFU)
        {
            if (index + 1 >= utf16.size())
            {
                return std::nullopt;
            }
            const char32_t low = utf16[index + 1];
            if (low < 0xDC00U || low > 0xDFFFU)
            {
                return std::nullopt;
            }
            codepoint = 0x10000U + ((codepoint - 0xD800U) << 10) + (low - 0xDC00U);
            ++index;
        }
        // A lone low surrogate can only be unpaired, since a valid pair consumes it above.
        else if (codepoint >= 0xDC00U && codepoint <= 0xDFFFU)
        {
            return std::nullopt;
        }

        // Rejected rather than encoded: every Tina text contract is NUL-free, and modified UTF-8's
        // two-byte NUL is exactly the encoding this function exists to avoid producing.
        if (codepoint == 0)
        {
            return std::nullopt;
        }

        // Shortest-form encoding only. Emitting an overlong sequence would pass this function's own
        // output to a validator that rejects it.
        usize sequenceBytes = 1;
        if (codepoint >= 0x10000U)
        {
            sequenceBytes = 4;
        } else if (codepoint >= 0x800U)
        {
            sequenceBytes = 3;
        } else if (codepoint >= 0x80U)
        {
            sequenceBytes = 2;
        }

        if (written + sequenceBytes > output.size())
        {
            return std::nullopt;
        }

        switch (sequenceBytes)
        {
        case 1:
            output[written++] = static_cast<char>(codepoint);
            break;
        case 2:
            output[written++] = static_cast<char>(0xC0U | (codepoint >> 6));
            output[written++] = static_cast<char>(0x80U | (codepoint & 0x3FU));
            break;
        case 3:
            output[written++] = static_cast<char>(0xE0U | (codepoint >> 12));
            output[written++] = static_cast<char>(0x80U | ((codepoint >> 6) & 0x3FU));
            output[written++] = static_cast<char>(0x80U | (codepoint & 0x3FU));
            break;
        default:
            output[written++] = static_cast<char>(0xF0U | (codepoint >> 18));
            output[written++] = static_cast<char>(0x80U | ((codepoint >> 12) & 0x3FU));
            output[written++] = static_cast<char>(0x80U | ((codepoint >> 6) & 0x3FU));
            output[written++] = static_cast<char>(0x80U | (codepoint & 0x3FU));
            break;
        }
    }
    return written;
}

} // namespace Tina::Core
