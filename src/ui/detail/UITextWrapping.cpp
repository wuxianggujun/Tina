#include "UITextWrapping.hpp"

#include "UIGraphemeBreak.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

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

void clampFinalVisibleLine(
    std::string_view text, float maximumWidth, float fallbackAdvance,
    float ellipsisAdvance, std::span<const UITextGlyphRaster> glyphs,
    UITextVisualLine& line) noexcept
{
    const float markerWidth =
        std::isfinite(ellipsisAdvance) && ellipsisAdvance >= 0.0F
            ? ellipsisAdvance
            : fallbackAdvance;
    float budget = std::numeric_limits<float>::infinity();
    if (std::isfinite(maximumWidth))
    {
        budget = (std::max)(0.0F, maximumWidth - markerWidth);
    }

    const std::string_view sourceLine =
        text.substr(line.byteBegin, line.byteEnd - line.byteBegin);
    usize byteOffset = 0;
    u32 codepointOffset = 0;
    UIGraphemeCluster cluster{};
    usize visibleByteCount = 0;
    usize visibleGlyphCount = 0;
    float visibleWidth = 0.0F;
    while (nextGraphemeCluster(
        sourceLine, byteOffset, codepointOffset, cluster))
    {
        const usize clusterGlyphCount =
            static_cast<usize>(cluster.endCodepoint - cluster.beginCodepoint);
        float clusterWidth = 0.0F;
        for (usize index = 0; index < clusterGlyphCount; ++index)
        {
            clusterWidth += glyphAdvance(
                line.glyphBegin + visibleGlyphCount + index,
                fallbackAdvance, glyphs);
        }
        if (visibleWidth + clusterWidth > budget)
        {
            break;
        }
        visibleWidth += clusterWidth;
        visibleByteCount = cluster.endByte;
        visibleGlyphCount += clusterGlyphCount;
    }

    line.byteEnd = line.byteBegin + visibleByteCount;
    line.glyphEnd = line.glyphBegin + visibleGlyphCount;
    line.width = visibleWidth + markerWidth;
    line.showEllipsis = true;
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
        usize clusterByteOffset = 0U;
        u32 clusterCodepointOffset = 0U;
        UIGraphemeCluster cluster{};
        if (!nextGraphemeCluster(
                text.substr(byte), clusterByteOffset,
                clusterCodepointOffset, cluster))
        {
            return false;
        }
        const usize clusterByteCount = cluster.endByte;
        const usize clusterGlyphCount =
            static_cast<usize>(cluster.endCodepoint);
        if (clusterByteCount == 1U && first == '\n')
        {
            line.byteEnd = byte;
            line.glyphEnd = glyph;
            line.width = width;
            cursor.byteOffset = byte + 1U;
            cursor.glyphOffset = glyph;
            cursor.trailingEmptyLinePending = cursor.byteOffset == text.size();
            return true;
        }

        const bool whitespace = isBreakWhitespace(first, clusterByteCount);
        float clusterWidth = 0.0F;
        for (usize index = 0; index < clusterGlyphCount; ++index)
        {
            clusterWidth += glyphAdvance(
                glyph + index, fallbackAdvance, glyphs);
        }
        const float nextWidth = width + clusterWidth;
        if (wraps && whitespace && acceptedCodepoints != 0U)
        {
            breakLineEndByte = byte;
            breakNextByte = byte + clusterByteCount;
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
        byte += clusterByteCount;
        glyph += clusterGlyphCount;
        acceptedCodepoints += clusterGlyphCount;
    }

    line.byteEnd = byte;
    line.glyphEnd = glyph;
    line.width = width;
    cursor.byteOffset = byte;
    cursor.glyphOffset = glyph;
    return true;
}

bool nextClampedTextLine(
    std::string_view text, float maximumWidth, UITextWrapMode wrapMode,
    UITextLineClamp lineClamp, float fallbackAdvance, float ellipsisAdvance,
    std::span<const UITextGlyphRaster> glyphs,
    UITextClampedLineCursor& cursor, UITextVisualLine& line) noexcept
{
    if (cursor.finished || !nextWrappedTextLine(
            text, maximumWidth, wrapMode, fallbackAdvance, glyphs,
            cursor.wrapped, line))
    {
        cursor.finished = true;
        return false;
    }

    ++cursor.emittedLineCount;
    if (!lineClamp.enabled() ||
        cursor.emittedLineCount < lineClamp.maximumLines)
    {
        return true;
    }

    UITextLineCursor probe = cursor.wrapped;
    UITextVisualLine hiddenLine{};
    if (nextWrappedTextLine(
        text, maximumWidth, wrapMode, fallbackAdvance, glyphs,
        probe, hiddenLine))
    {
        clampFinalVisibleLine(
            text, maximumWidth, fallbackAdvance, ellipsisAdvance, glyphs,
            line);
    }
    cursor.finished = true;
    return true;
}

UITextMetrics measureWrappedText(
    std::string_view text, const UITextStyle& style, float maximumWidth,
    UITextWrapMode wrapMode, std::span<const UITextGlyphRaster> glyphs,
    u32 codepointCount, UITextLineClamp lineClamp,
    float ellipsisAdvance) noexcept
{
    if (text.empty())
    {
        return {};
    }
    const float fallbackAdvance = style.logicalSize * style.advanceScale;
    const float lineHeight = style.logicalSize * style.lineHeightScale;
    const float resolvedEllipsisAdvance =
        std::isfinite(ellipsisAdvance) && ellipsisAdvance > 0.0F
            ? ellipsisAdvance
            : fallbackAdvance;
    UITextClampedLineCursor cursor{};
    UITextVisualLine line{};
    u32 lineCount = 0U;
    float widest = 0.0F;
    while (nextClampedTextLine(
        text, maximumWidth, wrapMode, lineClamp, fallbackAdvance,
        resolvedEllipsisAdvance, glyphs, cursor, line))
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

UITextIntrinsicWidths measureTextIntrinsicWidths(
    std::string_view text, const UITextStyle& style, UITextWrapMode wrapMode,
    std::span<const UITextGlyphRaster> glyphs) noexcept
{
    const float fallbackAdvance = style.logicalSize * style.advanceScale;
    float maximumLineWidth = 0.0F;
    float currentLineWidth = 0.0F;
    float maximumWordWidth = 0.0F;
    float currentWordWidth = 0.0F;
    usize byteOffset = 0U;
    usize glyphOffset = 0U;
    while (byteOffset < text.size())
    {
        const auto first = static_cast<unsigned char>(text[byteOffset]);
        const usize unitLength = utf8UnitLength(first);
        if (unitLength > text.size() - byteOffset)
        {
            break;
        }
        if (unitLength == 1U && first == '\n')
        {
            maximumLineWidth = (std::max)(maximumLineWidth, currentLineWidth);
            maximumWordWidth = (std::max)(maximumWordWidth, currentWordWidth);
            currentLineWidth = 0.0F;
            currentWordWidth = 0.0F;
            byteOffset += unitLength;
            continue;
        }

        const float advance = glyphAdvance(glyphOffset, fallbackAdvance, glyphs);
        currentLineWidth += advance;
        if (isBreakWhitespace(first, unitLength))
        {
            maximumWordWidth = (std::max)(maximumWordWidth, currentWordWidth);
            currentWordWidth = 0.0F;
        }
        else
        {
            currentWordWidth += advance;
        }
        byteOffset += unitLength;
        ++glyphOffset;
    }
    maximumLineWidth = (std::max)(maximumLineWidth, currentLineWidth);
    maximumWordWidth = (std::max)(maximumWordWidth, currentWordWidth);
    return UITextIntrinsicWidths{
        .minContent = wrapMode == UITextWrapMode::Words
                          ? maximumWordWidth
                          : maximumLineWidth,
        .maxContent = maximumLineWidth,
    };
}

} // namespace Tina::UI::Detail
