#include "UITextPaintEmitter.hpp"
#include "UILayoutPrimitives.hpp"
#include "UIPaintPrimitives.hpp"
#include "UITextWrapping.hpp"
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {
namespace {
bool hasRaster(const UITextPaintRasterSource& source) noexcept
{
    return source.rasterizer != nullptr && source.face.hasValue();
}

Core::Result<UITextRasterBatch> rasterLine(
    const UITextPaintRasterSource& source, std::string_view text, UITextStyle style)
{
    return source.rasterizer->raster(source.face, text, style, source.scale);
}

usize drawableGlyphs(const UITextRasterBatch& batch) noexcept
{
    return static_cast<usize>(std::count_if(batch.glyphs.begin(), batch.glyphs.end(),
        [](const UITextGlyphRaster& glyph) { return glyph.width != 0 && glyph.height != 0; }));
}

Core::Status appendBatch(std::pmr::vector<UICommittedPaintEntry>& output,
    const UICommittedLayoutEntry& layout, u32& ordinal,
    const UITextRasterBatch& batch, UITextStyle style, UIPremultipliedRgba8Color color,
    float startX, float startY, const UITextPaintRasterSource& source,
    UITextPaintRangeTint tint)
{
    if (source.atlas == nullptr)
    {
        return Core::failure(UIErrorCode::InvalidContextConfig, "Text rendering requires a glyph atlas");
    }
    const usize visibleCount = static_cast<usize>(std::count_if(batch.glyphs.begin(), batch.glyphs.end(),
        [&](const UITextGlyphRaster& glyph) {
            const auto glyphColor = glyph.clusterByteBegin < tint.byteEnd && glyph.clusterByteEnd > tint.byteBegin ? tint.color : color;
            return glyph.width != 0 && glyph.height != 0 && !glyphColor.isTransparent();
        }));
    if (visibleCount > output.capacity() - output.size())
    {
        return Core::failure(UIErrorCode::CapacityExceeded, "Shaped glyph paint capacity exhausted");
    }
    for (const UITextGlyphRaster& glyph : batch.glyphs)
    {
        if (glyph.width == 0 || glyph.height == 0) { continue; }
        const auto glyphColor = glyph.clusterByteBegin < tint.byteEnd && glyph.clusterByteEnd > tint.byteBegin ? tint.color : color;
        if (glyphColor.isTransparent()) { continue; }
        const u64 bytes = static_cast<u64>(glyph.width) * glyph.height * 4U;
        if (glyph.coverageOffset > batch.coverage.size() || bytes > batch.coverage.size() - glyph.coverageOffset)
        {
            return Core::failure(UIErrorCode::InvalidText, "Shaped glyph pixel view is out of range");
        }
        auto placed = source.atlas->insert({glyph.face, glyph.glyphIndex, glyph.rasterSize, glyph.imageKind},
            glyph, batch.coverage.subspan(glyph.coverageOffset, static_cast<usize>(bytes)));
        if (!placed) { return Core::failure(placed.error()); }
        output.push_back(UICommittedPaintEntry{
            .node = layout.node,
            .worldRect = {startX + glyph.originX + glyph.bearingX,
                          startY + glyph.originY + batch.baselineFromLineTop - glyph.bearingY,
                          glyph.logicalWidth, glyph.logicalHeight},
            .effectiveClip = layout.effectiveClip,
            .paintOrdinal = ordinal++,
            .solidFill = glyphColor,
            .kind = UICommittedPaintKind::Glyph,
            .atlasX = placed->atlasX, .atlasY = placed->atlasY,
            .atlasWidth = placed->width, .atlasHeight = placed->height, .atlasPage = 0,
            .glyphImageKind = glyph.imageKind, .glyphDistanceRange = glyph.distanceRange,
            .glyphRunOrigin = {startX, startY + static_cast<float>(glyph.line) * style.logicalSize *
                                               style.lineHeightScale + batch.baselineFromLineTop},
            .glyphPixelSnap = style.pixelSnap,
        });
    }
    return Core::success();
}

Core::Status appendPlaceholder(std::pmr::vector<UICommittedPaintEntry>& output,
    const UICommittedLayoutEntry& layout, u32& ordinal, std::string_view text,
    UITextStyle style, UIPremultipliedRgba8Color color, float startX, float startY,
    UITextPaintCursor& cursor, UITextPaintRangeTint tint)
{
    const float advance = style.logicalSize * style.advanceScale;
    const float height = style.logicalSize * style.lineHeightScale;
    float x = startX;
    float y = startY;
    for (usize byte = 0; byte < text.size();)
    {
        const u8 first = static_cast<u8>(text[byte]);
        const usize length = first < 0x80 ? 1U : first < 0xE0 ? 2U : first < 0xF0 ? 3U : 4U;
        if (length > text.size() - byte) { return Core::failure(UIErrorCode::InvalidText, "Invalid UTF-8 placeholder text"); }
        if (first == '\n') { x = cursor.baseX; y += height; }
        else
        {
            const auto glyphColor = byte < tint.byteEnd && byte + length > tint.byteBegin ? tint.color : color;
            if (!glyphColor.isTransparent())
            {
                if (output.size() == output.capacity())
                { return Core::failure(UIErrorCode::CapacityExceeded, "Placeholder paint capacity exhausted"); }
                output.push_back(UICommittedPaintEntry{.node = layout.node,
                    .worldRect = {x, y, advance, height}, .effectiveClip = layout.effectiveClip,
                    .paintOrdinal = ordinal++, .solidFill = glyphColor,
                    .kind = UICommittedPaintKind::SolidQuad});
            }
            x += advance;
        }
        byte += length;
    }
    cursor.x = x;
    cursor.y = y;
    cursor.lineHeight = height;
    return Core::success();
}

Core::Status appendLine(std::pmr::vector<UICommittedPaintEntry>& output,
    const UICommittedLayoutEntry& layout, u32& ordinal, std::string_view text,
    UITextStyle style, UIPremultipliedRgba8Color color, float startX, float startY,
    const UITextPaintRasterSource& source, UITextPaintCursor& cursor,
    UITextPaintRangeTint tint = {})
{
    if (!hasRaster(source)) { return appendPlaceholder(output, layout, ordinal, text, style, color, startX, startY, cursor, tint); }
    auto batch = rasterLine(source, text, style);
    if (!batch) { return Core::failure(batch.error()); }
    if (auto status = appendBatch(output, layout, ordinal, *batch, style, color, startX, startY, source, tint); !status) { return status; }
    cursor.lineHeight = style.logicalSize * style.lineHeightScale;
    cursor.y = startY + (batch->metrics.lineCount > 0 ? batch->metrics.lineCount - 1U : 0U) * cursor.lineHeight;
    float lastWidth = 0;
    for (const UITextScalarMetrics& scalar : batch->scalars)
    {
        if (scalar.line + 1U == batch->metrics.lineCount) { lastWidth += scalar.advance; }
    }
    cursor.x = startX + lastWidth;
    return Core::success();
}
}

Core::Result<usize> UITextPaintEmitter::countEntries(
    UITextLineLayout& lineLayout, std::string_view text, const UITextStyle& style, const UITextPaintRasterSource& source,
    float maximumWidth, UITextWrapMode wrapMode, UITextLineClamp lineClamp) noexcept
{
    if (text.empty()) { return usize{0}; }
    if (wrapMode == UITextWrapMode::NoWrap)
    {
        if (!hasRaster(source)) { return countDrawableTextCodepoints(text); }
        auto batch = rasterLine(source, text, style);
        if (!batch) { return Core::failure(batch.error()); }
        return drawableGlyphs(*batch);
    }
    auto lines = lineLayout.build(text, style, source.rasterizer, source.face,
                                  {maximumWidth, wrapMode, lineClamp});
    if (!lines) { return Core::failure(lines.error()); }
    usize count = 0;
    for (const auto& line : *lines)
    {
        UITextStyle lineStyle = style;
        lineStyle.direction = line.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight;
        auto glyphs = countEntries(lineLayout, text.substr(line.byteBegin, line.byteEnd - line.byteBegin), lineStyle, source,
                                  0, UITextWrapMode::NoWrap, {});
        if (!glyphs) { return Core::failure(glyphs.error()); }
        count += *glyphs;
        if (line.showEllipsis)
        {
            auto marker = countEntries(lineLayout, UITextEllipsisUtf8, lineStyle, source, 0, UITextWrapMode::NoWrap, {});
            if (!marker) { return Core::failure(marker.error()); }
            count += *marker;
        }
    }
    return count;
}

Core::Status UITextPaintEmitter::append(UITextLineLayout& lineLayout, std::pmr::vector<UICommittedPaintEntry>& output,
    const UICommittedLayoutEntry& layout, u32& ordinal, std::string_view text,
    const UITextStyle& style, UIPremultipliedRgba8Color color, float startX, float startY,
    const UITextPaintRasterSource& source, UITextPaintCursor* outCursor,
    float maximumWidth, UITextWrapMode wrapMode, UITextLineClamp lineClamp,
    UITextPaintRangeTint rangeTint) noexcept
{
    UITextPaintCursor cursor{startX, startY, style.logicalSize * style.lineHeightScale,
                             outCursor != nullptr ? outCursor->baseX : startX};
    if (text.empty() || (color.isTransparent() && rangeTint.byteBegin == rangeTint.byteEnd))
    {
        if (outCursor != nullptr) { *outCursor = cursor; }
        return Core::success();
    }
    const usize outputBase = output.size();
    const u32 ordinalBase = ordinal;
    Core::Status status = Core::success();
    if (wrapMode == UITextWrapMode::NoWrap)
    {
        status = appendLine(output, layout, ordinal, text, style, color, startX, startY, source, cursor, rangeTint);
    }
    else
    {
        // Snapshot only line boundaries before re-shaping each visual line.
        // No borrowed glyph/scalar span survives the next raster call.
        auto lines = lineLayout.build(text, style, source.rasterizer, source.face,
                                      {maximumWidth, wrapMode, lineClamp});
        if (!lines) { return Core::failure(lines.error()); }
        for (usize index = 0; index < lines->size() && status; ++index)
        {
            const auto& line = (*lines)[index];
            cursor.y = startY + static_cast<float>(index) * cursor.lineHeight;
            cursor.x = cursor.baseX;
            const UITextPaintRangeTint localTint{
                rangeTint.byteBegin > line.byteBegin ? rangeTint.byteBegin - line.byteBegin : 0,
                rangeTint.byteEnd > line.byteBegin ? rangeTint.byteEnd - line.byteBegin : 0, rangeTint.color};
            UITextStyle lineStyle = style;
            lineStyle.direction = line.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight;
            float textX = cursor.baseX;
            if (line.showEllipsis && line.rightToLeft)
            {
                status = appendLine(output, layout, ordinal, UITextEllipsisUtf8, lineStyle, color,
                                    textX, cursor.y, source, cursor);
                textX = cursor.x;
            }
            if (status)
            {
                status = appendLine(output, layout, ordinal, text.substr(line.byteBegin, line.byteEnd - line.byteBegin),
                                    lineStyle, color, textX, cursor.y, source, cursor, localTint);
            }
            if (status && line.showEllipsis && !line.rightToLeft)
            {
                status = appendLine(output, layout, ordinal, UITextEllipsisUtf8, lineStyle, color,
                                    cursor.x, cursor.y, source, cursor);
            }
        }
    }
    if (!status)
    {
        output.resize(outputBase);
        ordinal = ordinalBase;
        return status;
    }
    if (outCursor != nullptr) { *outCursor = cursor; }
    return Core::success();
}
} // namespace Tina::UI::Detail
