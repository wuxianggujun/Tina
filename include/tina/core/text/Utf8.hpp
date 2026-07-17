#pragma once

#include <tina/core/base/Types.hpp>

#include <limits>
#include <optional>
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

} // namespace Tina::Core
