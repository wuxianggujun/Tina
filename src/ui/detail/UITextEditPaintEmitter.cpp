#include "UITextEditPaintEmitter.hpp"
#include "UIGraphemeBreak.hpp"
#include "UILayoutPrimitives.hpp"
#include "UIPaintPrimitives.hpp"
#include "UITextTruncation.hpp"
#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

namespace Tina::UI::Detail {
namespace {
constexpr float CaretWidth = 2.0F;
struct DisplayText final {
    std::span<const UITextEditVisualLine> lines{};
    std::string_view text{};
    UITextSelection selection{};
    UITextPaintRangeTint tint{};
    u32 caret = 0;
    usize lineCount = 0;
};

Core::Result<UITextRasterBatch> raster(const UITextEditPaintState& state, std::string_view text)
{
    return state.rasterSource.rasterizer->raster(
        state.rasterSource.face, text, state.style, state.rasterSource.scale);
}

Core::Result<UITextShapeView> shape(const UITextEditPaintState& state, std::string_view text)
{
    return state.rasterSource.rasterizer->shape(state.rasterSource.face, text, state.style);
}

bool hasRaster(const UITextEditPaintState& state) noexcept
{
    return state.rasterSource.rasterizer != nullptr && state.rasterSource.face.hasValue();
}

Core::Result<UITextTruncationPlan> textPlan(
    UITextTruncationScratch& scratch, const UITextEditPaintState& state) noexcept
{
    if (state.focused || state.multilineEnabled) { return UITextTruncationPlan{.visibleText = state.committedText}; }
    return resolveTextTruncation(scratch, state.rasterSource, state.committedText, state.style,
                                state.overflow, state.availableWidth, state.intrinsicWidth);
}

Core::Status prepareDisplay(const UITextEditPaintState& state, UITextEditPaintScratch& scratch, DisplayText& display)
try
{
    display.text = state.committedText;
    display.selection = state.selection;
    display.caret = state.selection.caretCodepoint;
    if (state.preeditActive)
    {
        const u32 begin = (std::min)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
        const u32 end = (std::max)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
        const usize beginByte = utf8ByteOffsetForCodepoint(state.committedText, begin);
        const usize endByte = utf8ByteOffsetForCodepoint(state.committedText, end);
        const usize retainedBytes = beginByte + state.committedText.size() - endByte;
        if (retainedBytes > scratch.composition.max_size() ||
            state.preeditText.size() > scratch.composition.max_size() - retainedBytes)
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "IME display text exceeds addressable storage");
        }
        const usize bytes = retainedBytes + state.preeditText.size();
        scratch.composition.resize(bytes);
        // Shape the composed string once; never split an Arabic/Indic run at
        // selection or preedit boundaries merely to change its color.
        std::copy_n(state.committedText.data(), beginByte, scratch.composition.data());
        std::copy(state.preeditText.begin(), state.preeditText.end(), scratch.composition.data() + beginByte);
        std::copy(state.committedText.begin() + endByte, state.committedText.end(),
                  scratch.composition.data() + beginByte + state.preeditText.size());
        display.text = {scratch.composition.data(), bytes};
        display.caret = begin + state.preeditCursorCodepoint;
        display.selection = {display.caret, display.caret};
        display.tint = {beginByte, beginByte + state.preeditText.size(), premultiply(rgba8(0, 180, 255))};
    }
    if (state.multilineEnabled && !state.preeditActive && state.visualLayout.lineCount != 0 &&
        state.visualLayout.lineCount <= state.visualLines.size())
    {
        // Committed rows are immutable for this paint pass. Neither copying them
        // nor shaping the full document again is needed to paint its rows.
        display.lines = state.visualLines.first(state.visualLayout.lineCount);
        display.lineCount = display.lines.size();
        return Core::success();
    }
    const auto count = Core::countStrictUtf8CodepointsWithoutNul(display.text);
    if (!count) { return Core::failure(UIErrorCode::InvalidText, "TextEdit display text is not strict UTF-8"); }
    scratch.visualLines.resize(state.multilineEnabled ? static_cast<usize>(*count) + 1U : 1U);
    std::span<const UITextScalarMetrics> scalars{};
    if (hasRaster(state))
    {
        auto batch = shape(state, display.text);
        if (!batch) { return Core::failure(batch.error()); }
        scalars = batch->scalars;
    }
    if (state.multilineEnabled)
    {
        UITextEditVisualLayout layout{};
        if (!buildTextEditVisualLayout(display.text, state.availableWidth,
            (std::numeric_limits<float>::max)(), state.style.logicalSize * state.style.lineHeightScale,
            state.style.logicalSize * state.style.advanceScale, state.wrapMode, scalars, scratch.visualLines, layout))
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "IME/text display line budget exhausted");
        }
        display.lineCount = layout.lineCount;
    }
    else
    {
        scratch.visualLines[0] = {.beginCodepoint = 0, .endCodepoint = *count,
            .rightToLeft = scalars.empty() ? state.style.direction == UITextDirection::RightToLeft : scalars.front().paragraphRightToLeft};
        display.lineCount = 1;
    }
    display.lines = std::span<const UITextEditVisualLine>(scratch.visualLines).first(display.lineCount);
    return Core::success();
}
catch (const std::bad_alloc&)
{
    return Core::failure(Core::CoreErrorCode::OutOfMemory, "TextEdit display scratch allocation failed");
}
catch (const std::length_error&)
{
    return Core::failure(UIErrorCode::CapacityExceeded, "TextEdit display exceeds addressable storage");
}

template<class Visitor>
void selectionRanges(std::string_view lineText, const UITextEditVisualLine& line,
    UITextSelection selection, std::span<const UITextScalarMetrics> scalars, float fallback,
    Visitor&& visitor)
{
    const u32 selectionBegin = (std::min)(selection.anchorCodepoint, selection.caretCodepoint);
    const u32 selectionEnd = (std::max)(selection.anchorCodepoint, selection.caretCodepoint);
    usize byte = 0;
    u32 scalar = 0;
    UIGraphemeCluster cluster{};
    bool pending = false;
    float pendingLeft = 0.0F;
    float pendingRight = 0.0F;
    constexpr float JoinTolerance = 1.0F / 1024.0F;
    while (nextGraphemeCluster(lineText, byte, scalar, cluster))
    {
        if (line.beginCodepoint + cluster.endCodepoint <= selectionBegin ||
            line.beginCodepoint + cluster.beginCodepoint >= selectionEnd) { continue; }
        float left = (std::numeric_limits<float>::max)();
        float right = -(std::numeric_limits<float>::max)();
        for (u32 index = cluster.beginCodepoint; index < cluster.endCodepoint; ++index)
        {
            const float beginX = index < scalars.size() ? scalars[index].visualStartX : index * fallback;
            const float endX = index < scalars.size() ? scalars[index].visualEndX : (index + 1U) * fallback;
            left = (std::min)(left, (std::min)(beginX, endX));
            right = (std::max)(right, (std::max)(beginX, endX));
        }
        if (right <= left) { continue; }
        if (pending && left <= pendingRight + JoinTolerance && right >= pendingLeft - JoinTolerance)
        {
            pendingLeft = (std::min)(pendingLeft, left);
            pendingRight = (std::max)(pendingRight, right);
        }
        else
        {
            if (pending) { visitor(pendingLeft, pendingRight); }
            pending = true;
            pendingLeft = left;
            pendingRight = right;
        }
    }
    if (pending) { visitor(pendingLeft, pendingRight); }
}

usize caretLine(const DisplayText& display, UITextEditCaretAffinity affinity) noexcept
{
    for (usize index = 0; index < display.lineCount; ++index)
    {
        const auto& line = display.lines[index];
        if (display.caret < line.endCodepoint || display.caret == line.hardBreakCodepoint) { return index; }
        if (display.caret == line.endCodepoint &&
            (!isTextEditSoftWrapBoundary(std::span(display.lines).first(display.lineCount), index, display.caret) ||
             affinity == UITextEditCaretAffinity::Upstream)) { return index; }
    }
    return display.lineCount - 1U;
}

struct DisplayLineCursor final {
    usize byteOffset = 0;
    u32 codepointOffset = 0;
    usize lineBeginByte = 0;

    std::string_view next(const DisplayText& display, const UITextEditVisualLine& line) noexcept
    {
        // Rows are in logical order. Scan each UTF-8 byte once, not the document
        // prefix twice per row (quadratic for large multiline TextEdits).
        lineBeginByte = byteOffset + utf8ByteOffsetForCodepoint(
            display.text.substr(byteOffset), line.beginCodepoint - codepointOffset);
        byteOffset = lineBeginByte + utf8ByteOffsetForCodepoint(
            display.text.substr(lineBeginByte), line.endCodepoint - line.beginCodepoint);
        codepointOffset = line.endCodepoint;
        return display.text.substr(lineBeginByte, byteOffset - lineBeginByte);
    }
};
}

Core::Result<usize> UITextEditPaintEmitter::countEntries(
    UITextEditPaintScratch& scratch, const UITextEditPaintState& state) noexcept
{
    if (!state.focused)
    {
        if (state.textColor.isTransparent()) { return usize{0}; }
        const auto planned = textPlan(scratch.truncation, state);
        if (!planned) { return Core::failure(planned.error()); }
        const auto& plan = *planned;
        UITextStyle lineStyle = state.style;
        if (plan.showEllipsis) { lineStyle.direction = plan.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight; }
        auto count = UITextPaintEmitter::countEntries(scratch.lineLayout, plan.visibleText, lineStyle, state.rasterSource,
                                                      state.availableWidth, state.textWrapMode, state.textLineClamp);
        if (!count) { return Core::failure(count.error()); }
        if (plan.showEllipsis)
        {
            auto marker = UITextPaintEmitter::countEntries(scratch.lineLayout, UITextEllipsisUtf8, state.style, state.rasterSource,
                                                          0, UITextWrapMode::NoWrap, {});
            if (!marker) { return Core::failure(marker.error()); }
            *count += *marker;
        }
        return *count;
    }
    DisplayText display;
    if (auto status = prepareDisplay(state, scratch, display); !status) { return Core::failure(status.error()); }
    usize count = 1; // caret
    DisplayLineCursor lineCursor;
    for (usize index = 0; index < display.lineCount; ++index)
    {
        const auto& line = display.lines[index];
        const auto text = lineCursor.next(display, line);
        auto lineState = state;
        lineState.style.direction = line.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight;
        if (!state.textColor.isTransparent())
        {
            auto glyphCount = UITextPaintEmitter::countEntries(scratch.lineLayout, text, lineState.style, state.rasterSource,
                                                               0, UITextWrapMode::NoWrap, {});
            if (!glyphCount) { return Core::failure(glyphCount.error()); }
            count += *glyphCount;
        }
        std::span<const UITextScalarMetrics> scalars{};
        if (hasRaster(state))
        {
            if (state.textColor.isTransparent() && state.preeditActive)
            {
                auto batch = raster(lineState, text);
                if (!batch) { return Core::failure(batch.error()); }
                scalars = batch->scalars;
                const usize beginByte = lineCursor.lineBeginByte;
                for (const auto& glyph : batch->glyphs)
                {
                    if (glyph.width != 0 && glyph.height != 0 &&
                        beginByte + glyph.clusterByteBegin < display.tint.byteEnd &&
                        beginByte + glyph.clusterByteEnd > display.tint.byteBegin) { ++count; }
                }
            }
            else
            {
                auto run = shape(lineState, text);
                if (!run) { return Core::failure(run.error()); }
                scalars = run->scalars;
            }
        }
        else if (state.textColor.isTransparent() && state.preeditActive)
        {
            const usize beginByte = lineCursor.lineBeginByte;
            const usize endByte = beginByte + text.size();
            const usize tintBegin = (std::max)(beginByte, display.tint.byteBegin);
            const usize tintEnd = (std::min)(endByte, display.tint.byteEnd);
            if (tintBegin < tintEnd) { count += countDrawableTextCodepoints(display.text.substr(tintBegin, tintEnd - tintBegin)); }
        }
        selectionRanges(text, line, display.selection, scalars, state.style.logicalSize * state.style.advanceScale,
                        [&](float, float) { ++count; });
    }
    return count;
}

Core::Result<std::optional<UITextEditCaretGeometry>> UITextEditPaintEmitter::append(
    UITextEditPaintScratch& scratch, std::pmr::vector<UICommittedPaintEntry>& output, const UICommittedLayoutEntry& layoutEntry,
    u32& ordinal, const UITextEditPaintState& state) noexcept
{
    UICommittedLayoutEntry layout = layoutEntry;
    layout.effectiveClip = intersectRects(layout.effectiveClip, layout.contentPlacement.contentBox);
    const float startX = layout.contentPlacement.origin.x;
    const float startY = layout.contentPlacement.origin.y;
    if (!state.focused)
    {
        const auto planned = textPlan(scratch.truncation, state);
        if (!planned) { return Core::failure(planned.error()); }
        const auto& plan = *planned;
        UITextPaintCursor cursor{startX, startY, state.style.logicalSize * state.style.lineHeightScale, startX};
        UITextStyle lineStyle = state.style;
        if (plan.showEllipsis) { lineStyle.direction = plan.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight; }
        if (plan.showEllipsis && plan.rightToLeft)
        {
            if (auto status = UITextPaintEmitter::append(scratch.lineLayout, output, layout, ordinal, UITextEllipsisUtf8, lineStyle,
                state.textColor, cursor.x, cursor.y, state.rasterSource, &cursor); !status)
            { return Core::failure(status.error()); }
        }
        if (auto status = UITextPaintEmitter::append(scratch.lineLayout, output, layout, ordinal, plan.visibleText, lineStyle,
            state.textColor, cursor.x, startY, state.rasterSource, &cursor, state.availableWidth,
            state.textWrapMode, state.textLineClamp); !status) { return Core::failure(status.error()); }
        if (plan.showEllipsis && !plan.rightToLeft)
        {
            if (auto status = UITextPaintEmitter::append(scratch.lineLayout, output, layout, ordinal, UITextEllipsisUtf8, state.style,
                state.textColor, cursor.x, cursor.y, state.rasterSource, &cursor); !status)
            { return Core::failure(status.error()); }
        }
        return std::optional<UITextEditCaretGeometry>{};
    }
    DisplayText display;
    if (auto status = prepareDisplay(state, scratch, display); !status) { return Core::failure(status.error()); }
    const float height = state.style.logicalSize * state.style.lineHeightScale;
    const float fallback = state.style.logicalSize * state.style.advanceScale;
    const float scroll = state.multilineEnabled && std::isfinite(state.scrollY) ? state.scrollY : 0.0F;
    const usize selectedLine = caretLine(display, state.preeditActive ? UITextEditCaretAffinity::Downstream : state.caretAffinity);
    std::optional<UITextEditCaretGeometry> caret;
    DisplayLineCursor lineCursor;
    for (usize index = 0; index < display.lineCount; ++index)
    {
        const auto& line = display.lines[index];
        const auto text = lineCursor.next(display, line);
        auto lineState = state;
        lineState.style.direction = line.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight;
        const float y = startY + line.top - scroll;
        std::span<const UITextScalarMetrics> scalars{};
        if (hasRaster(state))
        {
            auto batch = shape(lineState, text);
            if (!batch) { return Core::failure(batch.error()); }
            scalars = batch->scalars;
        }
        bool exhausted = false;
        selectionRanges(text, line, display.selection, scalars, fallback, [&](float left, float right) {
            if (output.size() == output.capacity()) { exhausted = true; return; }
            output.push_back(UICommittedPaintEntry{.node = layout.node,
                .worldRect = {startX + left, y, right - left, height}, .effectiveClip = layout.effectiveClip,
                .paintOrdinal = ordinal++, .solidFill = state.selectionColor, .kind = UICommittedPaintKind::SolidQuad});
        });
        if (exhausted) { return Core::failure(UIErrorCode::CapacityExceeded, "Text selection paint budget exhausted"); }
        if (index == selectedLine)
        {
            caret = UITextEditCaretGeometry{{startX + textEditCaretHorizontalPosition(display.caret - line.beginCodepoint, fallback, scalars),
                                             y, CaretWidth, height}, layout.effectiveClip};
        }
        const usize beginByte = lineCursor.lineBeginByte;
        const UITextPaintRangeTint localTint{
            display.tint.byteBegin > beginByte ? display.tint.byteBegin - beginByte : 0,
            display.tint.byteEnd > beginByte ? display.tint.byteEnd - beginByte : 0, display.tint.color};
        if (auto status = UITextPaintEmitter::append(scratch.lineLayout, output, layout, ordinal, text, lineState.style,
            state.textColor, startX, y, state.rasterSource, nullptr, 0, UITextWrapMode::NoWrap, {}, localTint); !status)
        { return Core::failure(status.error()); }
    }
    if (caret)
    {
        if (output.size() == output.capacity()) { return Core::failure(UIErrorCode::CapacityExceeded, "Text caret paint budget exhausted"); }
        output.push_back(UICommittedPaintEntry{.node = layout.node, .worldRect = caret->worldRect,
            .effectiveClip = caret->effectiveClip, .paintOrdinal = ordinal++, .solidFill = state.caretColor,
            .kind = UICommittedPaintKind::SolidQuad});
    }
    return caret;
}
} // namespace Tina::UI::Detail
