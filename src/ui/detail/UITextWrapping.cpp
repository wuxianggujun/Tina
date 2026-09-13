#include "UITextWrapping.hpp"

#include "UITextShapingClusters.hpp"
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <stdexcept>

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

[[nodiscard]] float scalarAdvance(
    usize scalarIndex, float fallback,
    std::span<const UITextScalarMetrics> scalars) noexcept
{
    const float value = scalarIndex < scalars.size()
                            ? scalars[scalarIndex].advance
                            : fallback;
    return std::isfinite(value) && value >= 0.0F ? value : fallback;
}

void clampFinalVisibleLine(
    std::string_view text, float maximumWidth, float fallbackAdvance,
    float ellipsisAdvance, std::span<const UITextScalarMetrics> scalars,
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
    usize visibleScalarCount = 0;
    float visibleWidth = 0.0F;
    while (nextGraphemeCluster(
        sourceLine, byteOffset, codepointOffset, cluster))
    {
        extendTextShapingCluster(sourceLine, line.byteBegin, line.scalarBegin,
                             scalars, byteOffset, codepointOffset, cluster);
        const usize clusterScalarCount =
            static_cast<usize>(cluster.endCodepoint - cluster.beginCodepoint);
        float clusterWidth = 0.0F;
        for (usize index = 0; index < clusterScalarCount; ++index)
        {
            clusterWidth += scalarAdvance(
                line.scalarBegin + visibleScalarCount + index,
                fallbackAdvance, scalars);
        }
        if (visibleWidth + clusterWidth > budget)
        {
            break;
        }
        visibleWidth += clusterWidth;
        visibleByteCount = cluster.endByte;
        visibleScalarCount += clusterScalarCount;
    }

    line.byteEnd = line.byteBegin + visibleByteCount;
    line.scalarEnd = line.scalarBegin + visibleScalarCount;
    line.width = visibleWidth + markerWidth;
    line.showEllipsis = true;
}

} // namespace

bool nextWrappedTextLine(
    std::string_view text, float maximumWidth, UITextWrapMode wrapMode,
    float fallbackAdvance, std::span<const UITextScalarMetrics> scalars,
    UITextLineCursor& cursor, UITextVisualLine& line) noexcept
{
    line = {};
    if (cursor.trailingEmptyLinePending)
    {
        cursor.trailingEmptyLinePending = false;
        line.byteBegin = text.size();
        line.byteEnd = text.size();
        line.scalarBegin = cursor.scalarOffset;
        line.scalarEnd = cursor.scalarOffset;
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
            ++cursor.scalarOffset;
        }
        cursor.skipLeadingWhitespace = false;
        if (cursor.byteOffset >= text.size())
        {
            return false;
        }
    }

    line.byteBegin = cursor.byteOffset;
    line.scalarBegin = cursor.scalarOffset;
    usize byte = cursor.byteOffset;
    usize scalar = cursor.scalarOffset;
    usize acceptedCodepoints = 0;
    float width = 0.0F;
    usize breakLineEndByte = text.size() + 1U;
    usize breakNextByte = 0;
    usize breakScalar = 0;
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
        usize clusterByteCount = cluster.endByte;
        usize clusterScalarCount =
            static_cast<usize>(cluster.endCodepoint);
        if (clusterByteCount == 1U && first == '\n')
        {
            line.byteEnd = byte;
            line.scalarEnd = scalar;
            line.width = width;
            cursor.byteOffset = byte + 1U;
            cursor.scalarOffset = scalar;
            cursor.trailingEmptyLinePending = cursor.byteOffset == text.size();
            return true;
        }

        extendTextShapingCluster(text.substr(byte), byte, scalar, scalars,
                             clusterByteOffset, clusterCodepointOffset, cluster);
        clusterByteCount = cluster.endByte;
        clusterScalarCount = cluster.endCodepoint;

        const bool whitespace = isBreakWhitespace(first, clusterByteCount);
        float clusterWidth = 0.0F;
        for (usize index = 0; index < clusterScalarCount; ++index)
        {
            clusterWidth += scalarAdvance(
                scalar + index, fallbackAdvance, scalars);
        }
        const float nextWidth = width + clusterWidth;
        if (wraps && whitespace && acceptedCodepoints != 0U)
        {
            breakLineEndByte = byte;
            breakNextByte = byte + clusterByteCount;
            breakScalar = scalar;
            breakWidth = width;
        }
        if (wraps && nextWidth > maximumWidth && acceptedCodepoints != 0U)
        {
            if (breakLineEndByte <= text.size())
            {
                line.byteEnd = breakLineEndByte;
                line.scalarEnd = breakScalar;
                line.width = breakWidth;
                cursor.byteOffset = breakNextByte;
                cursor.scalarOffset = breakScalar + 1U;
                cursor.skipLeadingWhitespace = true;
            }
            else
            {
                line.byteEnd = byte;
                line.scalarEnd = scalar;
                line.width = width;
                cursor.byteOffset = byte;
                cursor.scalarOffset = scalar;
            }
            return true;
        }

        width = nextWidth;
        byte += clusterByteCount;
        scalar += clusterScalarCount;
        acceptedCodepoints += clusterScalarCount;
    }

    line.byteEnd = byte;
    line.scalarEnd = scalar;
    line.width = width;
    cursor.byteOffset = byte;
    cursor.scalarOffset = scalar;
    return true;
}

bool nextClampedTextLine(
    std::string_view text, float maximumWidth, UITextWrapMode wrapMode,
    UITextLineClamp lineClamp, float fallbackAdvance, float ellipsisAdvance,
    std::span<const UITextScalarMetrics> scalars,
    UITextClampedLineCursor& cursor, UITextVisualLine& line) noexcept
{
    if (cursor.finished || !nextWrappedTextLine(
            text, maximumWidth, wrapMode, fallbackAdvance, scalars,
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
        text, maximumWidth, wrapMode, fallbackAdvance, scalars,
        probe, hiddenLine))
    {
        clampFinalVisibleLine(
            text, maximumWidth, fallbackAdvance, ellipsisAdvance, scalars,
            line);
    }
    cursor.finished = true;
    return true;
}

Core::Result<std::span<const UITextVisualLine>> UITextLineLayout::build(
    std::string_view text, const UITextStyle& style, IUITextRasterizer* rasterizer,
    UIFontFaceId face, UITextMeasureOptions options, UITextIntrinsicWidths* intrinsicWidths) noexcept
{
    m_lines.clear();
    m_metrics = {};
    if (std::isnan(options.maximumWidth) || options.maximumWidth < 0.0F ||
        (options.wrapMode != UITextWrapMode::NoWrap && options.wrapMode != UITextWrapMode::Words) ||
        (options.lineClamp.enabled() && options.wrapMode != UITextWrapMode::Words))
    {
        return Core::failure(UIErrorCode::InvalidText, "Invalid text measurement width, wrap mode or line clamp");
    }
    const bool shaped = rasterizer != nullptr && face.hasValue();
    const float fallback = style.logicalSize * style.advanceScale;
    float ellipsis = fallback;
    if (options.lineClamp.enabled())
    {
        auto marker = shaped ? rasterizer->measure(face, UITextEllipsisUtf8, style)
                             : measurePlaceholderText(UITextEllipsisUtf8, style);
        if (!marker) { return Core::failure(marker.error()); }
        ellipsis = marker->measuredSize.width;
    }
    std::span<const UITextScalarMetrics> scalars{};
    if (shaped)
    {
        auto run = rasterizer->shape(face, text, style);
        if (!run) { return Core::failure(run.error()); }
        m_metrics = run->metrics;
        scalars = run->scalars;
    }
    else
    {
        auto measured = measurePlaceholderText(text, style);
        if (!measured) { return Core::failure(measured.error()); }
        m_metrics = *measured;
    }
    if (intrinsicWidths != nullptr)
    {
        *intrinsicWidths = measureTextIntrinsicWidths(text, style, options.wrapMode, scalars);
    }
    UITextClampedLineCursor cursor{};
    UITextVisualLine line{};
    try
    {
        while (nextClampedTextLine(text, options.maximumWidth, options.wrapMode, options.lineClamp,
                                   fallback, ellipsis, scalars, cursor, line))
        {
            line.rightToLeft = line.scalarBegin < scalars.size()
                ? scalars[line.scalarBegin].paragraphRightToLeft
                : style.direction == UITextDirection::RightToLeft;
            m_lines.push_back(line);
        }
    }
    catch (const std::bad_alloc&)
    {
        return Core::failure(Core::CoreErrorCode::OutOfMemory, "Text line layout allocation failed");
    }
    catch (const std::length_error&)
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "Text line count exceeds addressable storage");
    }
    return std::span<const UITextVisualLine>(m_lines);
}

Core::Result<UITextMetrics> UITextLineLayout::measure(
    std::string_view text, const UITextStyle& style, IUITextRasterizer* rasterizer,
    UIFontFaceId face, UITextMeasureOptions options, UITextIntrinsicWidths* intrinsicWidths) noexcept
{
    auto lines = build(text, style, rasterizer, face, options, intrinsicWidths);
    if (!lines) { return Core::failure(lines.error()); }
    if (m_lines.size() > (std::numeric_limits<u32>::max)())
    { return Core::failure(UIErrorCode::CapacityExceeded, "Text line count exceeds the metrics range"); }
    m_metrics.lineCount = static_cast<u32>(m_lines.size());
    m_metrics.measuredSize = {0.0F, static_cast<float>(m_metrics.lineCount) * style.logicalSize * style.lineHeightScale};
    for (auto& line : m_lines)
    {
        // A final visual row can shape differently from its paragraph (Arabic
        // joining, ligatures, kerning). Measure the exact slice/direction painted.
        if (rasterizer != nullptr && face.hasValue())
        {
            UITextStyle lineStyle = style;
            lineStyle.direction = line.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight;
            auto measured = rasterizer->measure(face, text.substr(line.byteBegin, line.byteEnd - line.byteBegin), lineStyle);
            if (!measured) { return Core::failure(measured.error()); }
            line.width = measured->measuredSize.width;
            if (line.showEllipsis)
            {
                auto marker = rasterizer->measure(face, UITextEllipsisUtf8, lineStyle);
                if (!marker) { return Core::failure(marker.error()); }
                line.width += marker->measuredSize.width;
            }
        }
        m_metrics.measuredSize.width = (std::max)(m_metrics.measuredSize.width, line.width);
    }
    if (!std::isfinite(m_metrics.measuredSize.width) || !std::isfinite(m_metrics.measuredSize.height))
    { return Core::failure(UIErrorCode::InvalidText, "Text measurement exceeds finite logical dimensions"); }
    return m_metrics;
}

UITextIntrinsicWidths measureTextIntrinsicWidths(
    std::string_view text, const UITextStyle& style, UITextWrapMode wrapMode,
    std::span<const UITextScalarMetrics> scalars) noexcept
{
    const float fallbackAdvance = style.logicalSize * style.advanceScale;
    float maximumLineWidth = 0.0F;
    float currentLineWidth = 0.0F;
    float maximumWordWidth = 0.0F;
    float currentWordWidth = 0.0F;
    usize byteOffset = 0U;
    usize scalarOffset = 0U;
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

        const float advance = scalarAdvance(scalarOffset, fallbackAdvance, scalars);
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
        ++scalarOffset;
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
