#include "UITextEditPaintEmitter.hpp"

#include "UIGraphemeBreak.hpp"
#include "UILayoutPrimitives.hpp"
#include "UIPaintPrimitives.hpp"
#include "UITextEditModel.hpp"
#include "UITextTruncation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tina::UI::Detail {
namespace {

constexpr float CaretWidth = 2.0F;

struct PreparedRaster final {
    std::span<const UITextGlyphRaster> glyphs{};
    std::span<const u8> coverage{};
    float baselineFromLineTop = 0.0F;
    bool available = false;
};

struct CompositionCursor final {
    float x = 0.0F;
    float top = 0.0F;
    bool caretPending = false;
    std::optional<UITextEditCaretGeometry> caret{};
};

[[nodiscard]] float fallbackAdvance(const UITextStyle& style) noexcept
{
    const float value = style.logicalSize * style.advanceScale;
    return std::isfinite(value) && value > 0.0F ? value : 1.0F;
}

[[nodiscard]] float resolvedLineHeight(const UITextStyle& style) noexcept
{
    const float value = style.logicalSize * style.lineHeightScale;
    return std::isfinite(value) && value > 0.0F ? value : 1.0F;
}

[[nodiscard]] u32 resolvedPixelSize(const UITextStyle& style) noexcept
{
    const float logicalSize = std::isfinite(style.logicalSize) && style.logicalSize > 0.0F
                                  ? style.logicalSize : 1.0F;
    // Convert through double before clamping.  The largest u32 is not exactly
    // representable as a float, so clamping in float space can round up to
    // 2^32 and make the subsequent cast implementation-defined.
    const double floored = std::floor(static_cast<double>(logicalSize));
    const double clamped = (std::min)(
        (std::max)(1.0, floored), static_cast<double>((std::numeric_limits<u32>::max)()));
    return static_cast<u32>(clamped);
}

[[nodiscard]] PreparedRaster prepareRaster(
    const UITextEditPaintState& state, std::string_view text, float lineHeight) noexcept
{
    if (state.rasterSource.rasterizer == nullptr || !state.rasterSource.face.hasValue())
    {
        return {};
    }
    auto batch = state.rasterSource.rasterizer->raster(
        state.rasterSource.face, text, state.style);
    if (!batch || batch->glyphs.size() < countDrawableTextCodepoints(text))
    {
        return {};
    }

    float baseline = batch->baselineFromLineTop;
    if (!(std::isfinite(baseline) && baseline >= 0.0F && baseline <= lineHeight))
    {
        baseline = lineHeight;
    }
    return PreparedRaster{
        .glyphs = batch->glyphs,
        .coverage = batch->coverage,
        .baselineFromLineTop = baseline,
        .available = true,
    };
}

[[nodiscard]] float advanceAtGlyph(usize glyphIndex, float fallback,
                                    std::span<const UITextGlyphRaster> glyphs) noexcept
{
    const float value = glyphIndex < glyphs.size() ? glyphs[glyphIndex].advance : fallback;
    return std::isfinite(value) && value >= 0.0F ? value : fallback;
}

[[nodiscard]] usize glyphIndexFor(const UITextEditVisualLine& line, u32 codepoint) noexcept
{
    return static_cast<usize>(line.beginGlyphIndex) +
           (codepoint >= line.beginCodepoint ? codepoint - line.beginCodepoint : 0U);
}

[[nodiscard]] float xAtCodepoint(const UITextEditVisualLine& line, u32 codepoint,
                                 float fallback, std::span<const UITextGlyphRaster> glyphs) noexcept
{
    float x = 0.0F;
    for (u32 index = line.beginCodepoint; index < codepoint && index < line.endCodepoint; ++index)
    {
        x += advanceAtGlyph(glyphIndexFor(line, index), fallback, glyphs);
    }
    return std::isfinite(x) ? x : 0.0F;
}

[[nodiscard]] usize selectionSegmentCount(UITextSelection selection,
                                          std::span<const UITextEditVisualLine> lines) noexcept
{
    const u32 begin = (std::min)(selection.anchorCodepoint, selection.caretCodepoint);
    const u32 end = (std::max)(selection.anchorCodepoint, selection.caretCodepoint);
    usize count = 0;
    for (const UITextEditVisualLine& line : lines)
    {
        if (begin < line.endCodepoint && end > line.beginCodepoint)
        {
            ++count;
        }
    }
    return count;
}

[[nodiscard]] usize countDrawableCodepoints(
    std::string_view text, u32 beginCodepoint, u32 endCodepoint) noexcept
{
    const usize beginByte = utf8ByteOffsetForCodepoint(text, beginCodepoint);
    const usize endByte = utf8ByteOffsetForCodepoint(text, endCodepoint);
    if (beginByte > endByte || endByte > text.size())
    {
        return 0;
    }
    return countDrawableTextCodepoints(text.substr(beginByte, endByte - beginByte));
}

// Single source of truth for the ellipsis decision. countEntries() reserves
// capacity from this plan and append() emits from it, so the two passes can
// never disagree about how many entries a truncated label produces.
//
// Truncation is limited to unfocused single-line text: a focused TextEdit needs
// the full run for caret/selection geometry, and a multiline box wraps instead
// of eliding.
[[nodiscard]] UITextTruncationPlan committedTextPlan(const UITextEditPaintState& state) noexcept
{
    if (state.focused || state.multilineEnabled)
    {
        return UITextTruncationPlan{.visibleText = state.committedText};
    }
    return resolveTextTruncation(
        state.rasterSource, state.committedText, state.style, state.overflow, state.availableWidth,
        state.intrinsicWidth);
}

[[nodiscard]] UIPremultipliedRgba8Color preeditColor() noexcept
{
    return premultiply(UIStraightSrgba8Color{
        .red = 0,
        .green = 180,
        .blue = 255,
        .alpha = 255,
    });
}

void appendFallbackGlyph(std::pmr::vector<UICommittedPaintEntry>& output,
                         const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                         UIPremultipliedRgba8Color color, float x, float y,
                         float fallback, float lineHeight) noexcept
{
    output.push_back(UICommittedPaintEntry{
        .node = layoutEntry.node,
        .worldRect = {
            .x = normalizeFloat(x),
            .y = normalizeFloat(y),
            .width = normalizeFloat(fallback),
            .height = normalizeFloat(lineHeight),
        },
        .effectiveClip = layoutEntry.effectiveClip,
        .paintOrdinal = nextPaintOrdinal++,
        .solidFill = color,
        .kind = UICommittedPaintKind::SolidQuad,
    });
}

void appendPreparedGlyph(std::pmr::vector<UICommittedPaintEntry>& output,
                         const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                         const UITextEditPaintState& state, const PreparedRaster& raster,
                         usize glyphIndex, UIPremultipliedRgba8Color color, float x, float y,
                         float fallback, float lineHeight) noexcept
{
    if (color.isTransparent())
    {
        return;
    }

    if (raster.available && glyphIndex < raster.glyphs.size())
    {
        const UITextGlyphRaster& glyph = raster.glyphs[glyphIndex];
        if (state.rasterSource.atlas != nullptr)
        {
            if (glyph.width == 0 || glyph.height == 0)
            {
                return;
            }
            const usize coverageBytes = static_cast<usize>(glyph.width) * glyph.height;
            if (glyph.coverageOffset <= raster.coverage.size() &&
                coverageBytes <= raster.coverage.size() - glyph.coverageOffset)
            {
                const std::span<const u8> coverage(
                    raster.coverage.data() + glyph.coverageOffset, coverageBytes);
                auto placed = state.rasterSource.atlas->insert(
                    UIGlyphKey{
                        .face = state.rasterSource.face,
                        .codepoint = glyph.codepoint,
                        .pixelSize = resolvedPixelSize(state.style),
                    },
                    glyph, coverage);
                if (placed)
                {
                    output.push_back(UICommittedPaintEntry{
                        .node = layoutEntry.node,
                        .worldRect = {
                            .x = normalizeFloat(x + glyph.bearingX),
                            .y = normalizeFloat(y + raster.baselineFromLineTop - glyph.bearingY),
                            .width = normalizeFloat(static_cast<float>(placed->width)),
                            .height = normalizeFloat(static_cast<float>(placed->height)),
                        },
                        .effectiveClip = layoutEntry.effectiveClip,
                        .paintOrdinal = nextPaintOrdinal++,
                        .solidFill = color,
                        .kind = UICommittedPaintKind::Glyph,
                        .atlasX = placed->atlasX,
                        .atlasY = placed->atlasY,
                        .atlasWidth = placed->width,
                        .atlasHeight = placed->height,
                        .atlasPage = 0,
                    });
                    return;
                }
            }
        }
    }

    appendFallbackGlyph(output, layoutEntry, nextPaintOrdinal, color, x, y, fallback, lineHeight);
}

[[nodiscard]] usize caretLineIndex(std::span<const UITextEditVisualLine> lines,
                                   u32 caretCodepoint,
                                   UITextEditCaretAffinity affinity) noexcept
{
    for (usize index = 0; index < lines.size(); ++index)
    {
        const UITextEditVisualLine& line = lines[index];
        if (caretCodepoint < line.beginCodepoint)
        {
            return index == 0 ? 0 : index - 1U;
        }
        if (caretCodepoint < line.endCodepoint || caretCodepoint == line.hardBreakCodepoint)
        {
            return index;
        }
        if (caretCodepoint == line.endCodepoint)
        {
            if (isTextEditSoftWrapBoundary(lines, index, caretCodepoint))
            {
                if (affinity == UITextEditCaretAffinity::Upstream)
                {
                    return index;
                }
                continue;
            }
            return index;
        }
    }
    return lines.empty() ? 0 : lines.size() - 1U;
}

void captureCompositionCaret(CompositionCursor& cursor, float originX, float originY,
                             float scrollY, float lineHeight) noexcept
{
    cursor.caret = UITextEditCaretGeometry{
        .worldRect = {
            .x = normalizeFloat(originX + cursor.x),
            .y = normalizeFloat(originY + cursor.top - scrollY),
            .width = CaretWidth,
            .height = lineHeight,
        },
    };
    cursor.caretPending = false;
}

void appendCompositionSegment(
    std::pmr::vector<UICommittedPaintEntry>& output,
    const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
    const UITextEditPaintState& state, std::string_view text,
    UIPremultipliedRgba8Color color, std::optional<u32> caretCodepoint,
    bool deferTerminalCaret, float originX, float originY, float scrollY,
    float viewportWidth, float lineHeight, float fallback, CompositionCursor& cursor) noexcept
{
    const PreparedRaster raster = prepareRaster(state, text, lineHeight);
    usize byteOffset = 0;
    u32 codepointOffset = 0;
    usize glyphIndex = 0;
    UIGraphemeCluster cluster{};
    while (nextGraphemeCluster(text, byteOffset, codepointOffset, cluster))
    {
        const bool hardBreak = cluster.endCodepoint == cluster.beginCodepoint + 1U &&
                               cluster.endByte == cluster.beginByte + 1U &&
                               text[cluster.beginByte] == '\n';
        const usize clusterGlyphBegin = glyphIndex;
        float clusterAdvance = 0.0F;
        if (!hardBreak)
        {
            for (u32 codepoint = cluster.beginCodepoint;
                 codepoint < cluster.endCodepoint; ++codepoint)
            {
                const float advance = advanceAtGlyph(
                    clusterGlyphBegin + (codepoint - cluster.beginCodepoint), fallback, raster.glyphs);
                const float nextAdvance = clusterAdvance + advance;
                clusterAdvance = std::isfinite(nextAdvance) ? nextAdvance : fallback;
            }
            if (state.multilineEnabled && state.wrapMode == UITextEditWrapMode::SoftWrap &&
                cursor.x > 0.0F && cursor.x + clusterAdvance > viewportWidth)
            {
                cursor.x = 0.0F;
                cursor.top = normalizeFloat(cursor.top + lineHeight);
            }
        }

        if (cursor.caretPending ||
            (caretCodepoint.has_value() && *caretCodepoint == cluster.beginCodepoint))
        {
            captureCompositionCaret(cursor, originX, originY, scrollY, lineHeight);
        }
        if (hardBreak)
        {
            cursor.x = 0.0F;
            cursor.top = normalizeFloat(cursor.top + lineHeight);
            continue;
        }

        for (u32 codepoint = cluster.beginCodepoint;
             codepoint < cluster.endCodepoint; ++codepoint)
        {
            if (caretCodepoint.has_value() && codepoint != cluster.beginCodepoint &&
                *caretCodepoint == codepoint)
            {
                captureCompositionCaret(cursor, originX, originY, scrollY, lineHeight);
            }
            appendPreparedGlyph(output, layoutEntry, nextPaintOrdinal, state, raster, glyphIndex, color,
                                originX + cursor.x, originY + cursor.top - scrollY,
                                fallback, lineHeight);
            cursor.x = normalizeFloat(cursor.x + advanceAtGlyph(glyphIndex, fallback, raster.glyphs));
            ++glyphIndex;
        }
    }

    if (caretCodepoint.has_value() && *caretCodepoint == codepointOffset)
    {
        if (deferTerminalCaret)
        {
            cursor.caretPending = true;
        }
        else
        {
            captureCompositionCaret(cursor, originX, originY, scrollY, lineHeight);
        }
    }
}

[[nodiscard]] std::optional<UITextEditCaretGeometry> appendPreeditComposition(
    std::pmr::vector<UICommittedPaintEntry>& output,
    const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
    const UITextEditPaintState& state) noexcept
{
    const float lineHeight = resolvedLineHeight(state.style);
    const float fallback = fallbackAdvance(state.style);
    const float originX = layoutEntry.contentPlacement.origin.x;
    const float originY = layoutEntry.contentPlacement.origin.y;
    const float scrollY = state.multilineEnabled && std::isfinite(state.scrollY) ? state.scrollY : 0.0F;
    const float viewportWidth = (std::max)(0.0F, layoutEntry.contentPlacement.contentBox.width);
    const u32 selectionBegin = (std::min)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
    const u32 selectionEnd = (std::max)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
    const usize selectionBeginByte = utf8ByteOffsetForCodepoint(state.committedText, selectionBegin);
    const usize selectionEndByte = utf8ByteOffsetForCodepoint(state.committedText, selectionEnd);
    if (selectionBeginByte > selectionEndByte || selectionEndByte > state.committedText.size())
    {
        return std::nullopt;
    }

    CompositionCursor cursor{};
    appendCompositionSegment(output, layoutEntry, nextPaintOrdinal, state,
                             state.committedText.substr(0, selectionBeginByte), state.textColor,
                             std::nullopt, false, originX, originY, scrollY, viewportWidth,
                             lineHeight, fallback, cursor);
    appendCompositionSegment(output, layoutEntry, nextPaintOrdinal, state, state.preeditText,
                             preeditColor(), state.preeditCursorCodepoint, true,
                             originX, originY, scrollY, viewportWidth, lineHeight, fallback, cursor);
    appendCompositionSegment(output, layoutEntry, nextPaintOrdinal, state,
                             state.committedText.substr(selectionEndByte), state.textColor,
                             std::nullopt, false, originX, originY, scrollY, viewportWidth,
                             lineHeight, fallback, cursor);
    if (!cursor.caret.has_value())
    {
        captureCompositionCaret(cursor, originX, originY, scrollY, lineHeight);
    }
    cursor.caret->effectiveClip = layoutEntry.effectiveClip;
    output.push_back(UICommittedPaintEntry{
        .node = layoutEntry.node,
        .worldRect = cursor.caret->worldRect,
        .effectiveClip = layoutEntry.effectiveClip,
        .paintOrdinal = nextPaintOrdinal++,
        .solidFill = state.caretColor,
        .kind = UICommittedPaintKind::SolidQuad,
    });
    return cursor.caret;
}

} // namespace

usize UITextEditPaintEmitter::countEntries(const UITextEditPaintState& state) noexcept
{
    const bool textVisible = !state.textColor.isTransparent();
    if (!state.focused)
    {
        if (!textVisible)
        {
            return 0;
        }
        const UITextTruncationPlan plan = committedTextPlan(state);
        usize count = UITextPaintEmitter::countEntries(
            plan.visibleText, state.style, state.rasterSource,
            state.availableWidth, state.textWrapMode, state.textLineClamp);
        if (plan.showEllipsis)
        {
            count += countDrawableTextCodepoints(UITextEllipsisUtf8);
        }
        return count;
    }

    if (state.preeditActive)
    {
        const u32 selectionBegin = (std::min)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
        const u32 selectionEnd = (std::max)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
        usize count = countDrawableTextCodepoints(state.preeditText);
        if (textVisible)
        {
            count += countDrawableCodepoints(state.committedText, 0, selectionBegin);
            const u32 textEnd = nextGraphemeBoundary(
                state.committedText, (std::numeric_limits<u32>::max)());
            count += countDrawableCodepoints(state.committedText, selectionEnd, textEnd);
        }
        return count + 1U;
    }

    const usize glyphCount = textVisible ? countDrawableTextCodepoints(state.committedText) : 0;
    if (state.multilineEnabled && state.visualLayout.lineCount != 0 &&
        state.visualLines.size() >= state.visualLayout.lineCount)
    {
        const std::span<const UITextEditVisualLine> lines =
            state.visualLines.first(state.visualLayout.lineCount);
        return glyphCount + selectionSegmentCount(state.selection, lines) + 1U;
    }
    return glyphCount + (state.selection.isCollapsed() ? 0U : 1U) + 1U;
}

std::optional<UITextEditCaretGeometry>
UITextEditPaintEmitter::append(std::pmr::vector<UICommittedPaintEntry>& output,
                               const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                               const UITextEditPaintState& state) noexcept
{
    UICommittedLayoutEntry textLayoutEntry = layoutEntry;
    textLayoutEntry.effectiveClip =
        intersectRects(textLayoutEntry.effectiveClip, layoutEntry.contentPlacement.contentBox);
    if (state.focused && state.preeditActive)
    {
        return appendPreeditComposition(output, textLayoutEntry, nextPaintOrdinal, state);
    }

    const float textStartX = layoutEntry.contentPlacement.origin.x;
    const float textStartY = layoutEntry.contentPlacement.origin.y;
    if (state.multilineEnabled && state.visualLayout.lineCount != 0 &&
        state.visualLines.size() >= state.visualLayout.lineCount)
    {
        const std::span<const UITextEditVisualLine> lines =
            state.visualLines.first(state.visualLayout.lineCount);
        const float lineHeight = resolvedLineHeight(state.style);
        const float fallback = fallbackAdvance(state.style);
        const PreparedRaster raster = prepareRaster(state, state.committedText, lineHeight);
        const float scrollY = std::isfinite(state.scrollY) ? state.scrollY : 0.0F;

        if (state.focused)
        {
            const u32 selectionBegin = (std::min)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
            const u32 selectionEnd = (std::max)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
            for (const UITextEditVisualLine& line : lines)
            {
                const u32 begin = (std::max)(selectionBegin, line.beginCodepoint);
                const u32 end = (std::min)(selectionEnd, line.endCodepoint);
                if (begin >= end)
                {
                    continue;
                }
                output.push_back(UICommittedPaintEntry{
                    .node = layoutEntry.node,
                    .worldRect = {
                        .x = normalizeFloat(textStartX + xAtCodepoint(line, begin, fallback, raster.glyphs)),
                        .y = normalizeFloat(textStartY + line.top - scrollY),
                        .width = normalizeFloat((std::max)(0.0F,
                            xAtCodepoint(line, end, fallback, raster.glyphs) -
                            xAtCodepoint(line, begin, fallback, raster.glyphs))),
                        .height = lineHeight,
                    },
                    .effectiveClip = textLayoutEntry.effectiveClip,
                    .paintOrdinal = nextPaintOrdinal++,
                    .solidFill = state.selectionColor,
                    .kind = UICommittedPaintKind::SolidQuad,
                });
            }
        }

        for (const UITextEditVisualLine& line : lines)
        {
            float x = 0.0F;
            usize glyphIndex = line.beginGlyphIndex;
            const float rowY = normalizeFloat(textStartY + line.top - scrollY);
            for (u32 codepoint = line.beginCodepoint; codepoint < line.endCodepoint; ++codepoint)
            {
                appendPreparedGlyph(output, textLayoutEntry, nextPaintOrdinal, state, raster, glyphIndex,
                                    state.textColor, textStartX + x, rowY, fallback, lineHeight);
                x = normalizeFloat(x + advanceAtGlyph(glyphIndex, fallback, raster.glyphs));
                ++glyphIndex;
            }
        }

        if (!state.focused)
        {
            return std::nullopt;
        }
        const usize caretLine = caretLineIndex(
            lines, state.selection.caretCodepoint, state.caretAffinity);
        const UITextEditVisualLine& line = lines[caretLine];
        const u32 caretColumn =
            (std::min)((std::max)(state.selection.caretCodepoint, line.beginCodepoint), line.endCodepoint);
        const UILogicalRect caretRect{
            .x = normalizeFloat(textStartX + xAtCodepoint(line, caretColumn, fallback, raster.glyphs)),
            .y = normalizeFloat(textStartY + line.top - scrollY),
            .width = CaretWidth,
            .height = lineHeight,
        };
        output.push_back(UICommittedPaintEntry{
            .node = layoutEntry.node,
            .worldRect = caretRect,
            .effectiveClip = textLayoutEntry.effectiveClip,
            .paintOrdinal = nextPaintOrdinal++,
            .solidFill = state.caretColor,
            .kind = UICommittedPaintKind::SolidQuad,
        });
        return UITextEditCaretGeometry{
            .worldRect = caretRect,
            .effectiveClip = textLayoutEntry.effectiveClip,
        };
    }

    UITextPaintCursor cursor{
        .x = textStartX,
        .y = textStartY,
        .lineHeight = state.style.logicalSize * state.style.lineHeightScale,
        .baseX = textStartX,
    };
    const auto appendText = [&](std::string_view text, UIPremultipliedRgba8Color color) noexcept {
        UITextPaintEmitter::append(output, textLayoutEntry, nextPaintOrdinal, text, state.style, color,
                                   cursor.x, cursor.y, state.rasterSource, &cursor,
                                   state.availableWidth, state.textWrapMode,
                                   state.textLineClamp);
    };

    if (!state.focused)
    {
        const UITextTruncationPlan plan = committedTextPlan(state);
        appendText(plan.visibleText, state.textColor);
        if (plan.showEllipsis)
        {
            appendText(UITextEllipsisUtf8, state.textColor);
        }
        return std::nullopt;
    }

    const u32 selectionBegin = (std::min)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
    const u32 selectionEnd = (std::max)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
    const usize selectionBeginByte = utf8ByteOffsetForCodepoint(state.committedText, selectionBegin);
    const usize selectionEndByte = utf8ByteOffsetForCodepoint(state.committedText, selectionEnd);
    appendText(state.committedText.substr(0, selectionBeginByte), state.textColor);
    const UITextPaintCursor selectionStartCursor = cursor;

    const usize selectionPaintIndex = output.size();
    if (selectionBegin != selectionEnd)
    {
        output.push_back(UICommittedPaintEntry{
            .node = layoutEntry.node,
            .worldRect = {},
            .effectiveClip = textLayoutEntry.effectiveClip,
            .paintOrdinal = nextPaintOrdinal++,
            .solidFill = state.selectionColor,
            .kind = UICommittedPaintKind::SolidQuad,
        });
    }
    appendText(state.committedText.substr(selectionBeginByte, selectionEndByte - selectionBeginByte),
               state.textColor);
    const UITextPaintCursor selectionEndCursor = cursor;
    if (selectionBegin != selectionEnd)
    {
        output[selectionPaintIndex].worldRect = UILogicalRect{
            .x = normalizeFloat(selectionStartCursor.x),
            .y = normalizeFloat(selectionStartCursor.y),
            .width = normalizeFloat((std::max)(0.0F, selectionEndCursor.x - selectionStartCursor.x)),
            .height = normalizeFloat((std::max)(1.0F, selectionEndCursor.lineHeight)),
        };
    }
    const UITextPaintCursor caretCursor =
        state.selection.caretCodepoint == selectionBegin ? selectionStartCursor : selectionEndCursor;
    appendText(state.committedText.substr(selectionEndByte), state.textColor);

    float lineHeight = caretCursor.lineHeight;
    if (!(std::isfinite(lineHeight) && lineHeight > 0.0F))
    {
        lineHeight = resolvedLineHeight(state.style);
    }
    const UILogicalRect caretRect{
        .x = normalizeFloat(caretCursor.x),
        .y = normalizeFloat(caretCursor.y),
        .width = CaretWidth,
        .height = normalizeFloat(lineHeight),
    };
    output.push_back(UICommittedPaintEntry{
        .node = layoutEntry.node,
        .worldRect = caretRect,
        .effectiveClip = textLayoutEntry.effectiveClip,
        .paintOrdinal = nextPaintOrdinal++,
        .solidFill = state.caretColor,
        .kind = UICommittedPaintKind::SolidQuad,
    });
    return UITextEditCaretGeometry{
        .worldRect = caretRect,
        .effectiveClip = textLayoutEntry.effectiveClip,
    };
}

} // namespace Tina::UI::Detail
