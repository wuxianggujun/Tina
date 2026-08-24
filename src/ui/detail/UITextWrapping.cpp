#include "UITextWrapping.hpp"

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] constexpr usize utf8UnitLength(unsigned char first) noexcept
{
    if (first <= 0x7FU)
    {
        return 1U;
    }
    if ((first & 0xE0U) == 0xC0U)
    {
        return 2U;
    }
    if ((first & 0xF0U) == 0xE0U)
    {
        return 3U;
    }
    return 4U;
}

[[nodiscard]] constexpr bool isBreakWhitespace(
    unsigned char first, usize unitLength) noexcept
{
    return unitLength == 1U &&
           (first == static_cast<unsigned char>(' ') || first == '\t');
}

[[nodiscard]] float glyphAdvance(
    usize glyphIndex, float fallback,
    std::span<const UITextGlyphRaster> glyphs) noexcept
{
    const float value = glyphIndex < glyphs.size()
                            ? glyphs[glyphIndex].advance
                            : fallback;
    return std::isfinite(value) && value >= 0.0F ? value : fallback;
}

} // namespace

bool nextWrappedTextLine(
    std::string_view text, float maximumWidth, UITextWrapMode wrapMode,
    float fallbackAdvance, std::span<const UITextGlyphRaster> glyphs,
    UITextLineCursor& cursor, UITextVisualLine& line) noexcept
{
    line = {};
    if (cursor.trailingEmptyLinePending)
    {
        cursor.trailingEmptyLinePending = false;
        line.byteBegin = text.size();
        line.byteEnd = text.size();
        line.glyphBegin = cursor.glyphOffset;
        line.glyphEnd = cursor.glyphOffset;
        return true;
    }
    if (cursor.byteOffset >= text.size())
    {
        return false;
    }

    if (cursor.skipLeadingWhitespace)
    {
        while (cursor.byteOffset < text.size())
        {
            const auto first = static_cast<unsigned char>(text[cursor.byteOffset]);
            const usize unitLength = utf8UnitLength(first);
            if (!isBreakWhitespace(first, unitLength))
            {
                break;
            }
            cursor.byteOffset += unitLength;
            ++cursor.glyphOffset;
        }
        cursor.skipLeadingWhitespace = false;
        if (cursor.byteOffset >= text.size())
        {
            return false;
        }
    }

    line.byteBegin = cursor.byteOffset;
    line.glyphBegin = cursor.glyphOffset;
    usize byte = cursor.byteOffset;
    usize glyph = cursor.glyphOffset;
    usize acceptedCodepoints = 0;
    float width = 0.0F;
    usize breakLineEndByte = text.size() + 1U;
    usize breakNextByte = 0;
    usize breakGlyph = 0;
    float breakWidth = 0.0F;
    const bool wraps = wrapMode == UITextWrapMode::Words &&
                       std::isfinite(maximumWidth) && maximumWidth >= 0.0F;

    while (byte < text.size())
    {
        const auto first = static_cast<unsigned char>(text[byte]);
        const usize unitLength = utf8UnitLength(first);
        if (unitLength > text.size() - byte)
        {
            return false;
        }
        if (unitLength == 1U && first == '\n')
        {
            line.byteEnd = byte;
            line.glyphEnd = glyph;
            line.width = width;
            cursor.byteOffset = byte + 1U;
            cursor.glyphOffset = glyph;
            cursor.trailingEmptyLinePending = cursor.byteOffset == text.size();
            return true;
        }

        const bool whitespace = isBreakWhitespace(first, unitLength);
        const float advance = glyphAdvance(glyph, fallbackAdvance, glyphs);
        const float nextWidth = width + advance;
        if (wraps && whitespace && acceptedCodepoints != 0U)
        {
            breakLineEndByte = byte;
            breakNextByte = byte + unitLength;
            breakGlyph = glyph;
            breakWidth = width;
        }
        if (wraps && nextWidth > maximumWidth && acceptedCodepoints != 0U)
        {
            if (breakLineEndByte <= text.size())
            {
                line.byteEnd = breakLineEndByte;
                line.glyphEnd = breakGlyph;
                line.width = breakWidth;
                cursor.byteOffset = breakNextByte;
                cursor.glyphOffset = breakGlyph + 1U;
                cursor.skipLeadingWhitespace = true;
            }
            else
            {
                line.byteEnd = byte;
                line.glyphEnd = glyph;
                line.width = width;
                cursor.byteOffset = byte;
                cursor.glyphOffset = glyph;
            }
            return true;
        }

        width = nextWidth;
        byte += unitLength;
        ++glyph;
        ++acceptedCodepoints;
    }

    line.byteEnd = byte;
    line.glyphEnd = glyph;
    line.width = width;
    cursor.byteOffset = byte;
    cursor.glyphOffset = glyph;
    return true;
}

UITextMetrics measureWrappedText(
    std::string_view text, const UITextStyle& style, float maximumWidth,
    UITextWrapMode wrapMode, std::span<const UITextGlyphRaster> glyphs,
    u32 codepointCount) noexcept
{
    if (text.empty())
    {
        return {};
    }
    const float fallbackAdvance = style.logicalSize * style.advanceScale;
    const float lineHeight = style.logicalSize * style.lineHeightScale;
    UITextLineCursor cursor{};
    UITextVisualLine line{};
    u32 lineCount = 0U;
    float widest = 0.0F;
    while (nextWrappedTextLine(
        text, maximumWidth, wrapMode, fallbackAdvance, glyphs, cursor, line))
    {
        ++lineCount;
        widest = (std::max)(widest, line.width);
    }
    return UITextMetrics{
        .measuredSize = {
            .width = widest,
            .height = static_cast<float>(lineCount) * lineHeight,
        },
        .codepointCount = codepointCount,
        .lineCount = lineCount,
    };
}

} // namespace Tina::UI::Detail
