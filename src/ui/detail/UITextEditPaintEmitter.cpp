#include "UITextEditPaintEmitter.hpp"
#include "UIGraphemeBreak.hpp"
#include "UILayoutPrimitives.hpp"
#include "UIPaintPrimitives.hpp"
#include "UITextTruncation.hpp"
#include <tina/core/text/Utf8.hpp>
#include <tina/ui/UIErrors.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace Tina::UI::Detail {
namespace {
constexpr float CaretWidth = 2.0F;
constexpr usize MaximumDisplayBytes = 64U * 1024U;
constexpr usize MaximumDisplayLines = 4096;

struct DisplayText final {
    std::array<char, MaximumDisplayBytes> composition{};
    std::array<UITextEditVisualLine, MaximumDisplayLines> lines{};
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

bool hasRaster(const UITextEditPaintState& state) noexcept
{
    return state.rasterSource.rasterizer != nullptr && state.rasterSource.face.hasValue();
}

UITextTruncationPlan textPlan(const UITextEditPaintState& state) noexcept
{
    if (state.focused || state.multilineEnabled) { return {.visibleText = state.committedText}; }
    return resolveTextTruncation(state.rasterSource, state.committedText, state.style,
                                state.overflow, state.availableWidth, state.intrinsicWidth);
}

Core::Status prepareDisplay(const UITextEditPaintState& state, DisplayText& display)
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
        const usize bytes = beginByte + state.preeditText.size() + state.committedText.size() - endByte;
        if (bytes > display.composition.size())
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "IME display text byte budget exhausted");
        }
        // Shape the composed string once; never split an Arabic/Indic run at
        // selection or preedit boundaries merely to change its color.
        std::copy_n(state.committedText.data(), beginByte, display.composition.data());
        std::copy(state.preeditText.begin(), state.preeditText.end(), display.composition.data() + beginByte);
        std::copy(state.committedText.begin() + endByte, state.committedText.end(),
                  display.composition.data() + beginByte + state.preeditText.size());
        display.text = {display.composition.data(), bytes};
        display.caret = begin + state.preeditCursorCodepoint;
        display.selection = {display.caret, display.caret};
        display.tint = {beginByte, beginByte + state.preeditText.size(), premultiply(rgba8(0, 180, 255))};
    }
    std::span<const UITextScalarMetrics> scalars{};
    if (hasRaster(state))
    {
        auto batch = raster(state, display.text);
        if (!batch) { return Core::failure(batch.error()); }
        scalars = batch->scalars;
    }
    if (state.multilineEnabled)
    {
        if (!state.preeditActive && state.visualLayout.lineCount != 0 &&
            state.visualLayout.lineCount <= state.visualLines.size())
        {
            if (state.visualLayout.lineCount > display.lines.size())
            { return Core::failure(UIErrorCode::CapacityExceeded, "Committed text line budget exhausted"); }
            std::copy_n(state.visualLines.begin(), state.visualLayout.lineCount, display.lines.begin());
            display.lineCount = state.visualLayout.lineCount;
            return Core::success();
        }
        UITextEditVisualLayout layout{};
        if (!buildTextEditVisualLayout(display.text, state.availableWidth,
            (std::numeric_limits<float>::max)(), state.style.logicalSize * state.style.lineHeightScale,
            state.style.logicalSize * state.style.advanceScale, state.wrapMode, scalars, display.lines, layout))
        {
            return Core::failure(UIErrorCode::CapacityExceeded, "IME/text display line budget exhausted");
        }
        display.lineCount = layout.lineCount;
    }
    else
    {
        const auto count = Core::countStrictUtf8CodepointsWithoutNul(display.text);
        if (!count) { return Core::failure(UIErrorCode::InvalidText, "TextEdit display text is not strict UTF-8"); }
        display.lines[0] = {.beginCodepoint = 0, .endCodepoint = *count,
            .rightToLeft = scalars.empty() ? state.style.direction == UITextDirection::RightToLeft : scalars.front().paragraphRightToLeft};
        display.lineCount = 1;
    }
    return Core::success();
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

std::string_view lineText(const DisplayText& display, const UITextEditVisualLine& line) noexcept
{
    const usize begin = utf8ByteOffsetForCodepoint(display.text, line.beginCodepoint);
    const usize end = utf8ByteOffsetForCodepoint(display.text, line.endCodepoint);
    return display.text.substr(begin, end - begin);
}
}

Core::Result<usize> UITextEditPaintEmitter::countEntries(const UITextEditPaintState& state) noexcept
{
    if (!state.focused)
    {
        if (state.textColor.isTransparent()) { return usize{0}; }
        const auto plan = textPlan(state);
        UITextStyle lineStyle = state.style;
        if (plan.showEllipsis) { lineStyle.direction = plan.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight; }
        auto count = UITextPaintEmitter::countEntries(plan.visibleText, lineStyle, state.rasterSource,
                                                      state.availableWidth, state.textWrapMode, state.textLineClamp);
        if (!count) { return Core::failure(count.error()); }
        if (plan.showEllipsis)
        {
            auto marker = UITextPaintEmitter::countEntries(UITextEllipsisUtf8, state.style, state.rasterSource,
                                                          0, UITextWrapMode::NoWrap, {});
            if (!marker) { return Core::failure(marker.error()); }
            *count += *marker;
        }
        return *count;
    }
    DisplayText display;
    if (auto status = prepareDisplay(state, display); !status) { return Core::failure(status.error()); }
    usize count = 1; // caret
    for (usize index = 0; index < display.lineCount; ++index)
    {
        const auto& line = display.lines[index];
        const auto text = lineText(display, line);
        auto lineState = state;
        lineState.style.direction = line.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight;
        if (!state.textColor.isTransparent())
        {
            auto glyphCount = UITextPaintEmitter::countEntries(text, lineState.style, state.rasterSource,
                                                               0, UITextWrapMode::NoWrap, {});
            if (!glyphCount) { return Core::failure(glyphCount.error()); }
            count += *glyphCount;
        }
        std::span<const UITextScalarMetrics> scalars{};
        if (hasRaster(state))
        {
            auto batch = raster(lineState, text);
            if (!batch) { return Core::failure(batch.error()); }
            scalars = batch->scalars;
            if (state.textColor.isTransparent() && state.preeditActive)
            {
                const usize beginByte = utf8ByteOffsetForCodepoint(display.text, line.beginCodepoint);
                for (const auto& glyph : batch->glyphs)
                {
                    if (glyph.width != 0 && glyph.height != 0 &&
                        beginByte + glyph.clusterByteBegin < display.tint.byteEnd &&
                        beginByte + glyph.clusterByteEnd > display.tint.byteBegin) { ++count; }
                }
            }
        }
        else if (state.textColor.isTransparent() && state.preeditActive)
        {
            const usize beginByte = utf8ByteOffsetForCodepoint(display.text, line.beginCodepoint);
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
    std::pmr::vector<UICommittedPaintEntry>& output, const UICommittedLayoutEntry& layoutEntry,
    u32& ordinal, const UITextEditPaintState& state) noexcept
{
    UICommittedLayoutEntry layout = layoutEntry;
    layout.effectiveClip = intersectRects(layout.effectiveClip, layout.contentPlacement.contentBox);
    const float startX = layout.contentPlacement.origin.x;
    const float startY = layout.contentPlacement.origin.y;
    if (!state.focused)
    {
        const auto plan = textPlan(state);
        UITextPaintCursor cursor{startX, startY, state.style.logicalSize * state.style.lineHeightScale, startX};
        UITextStyle lineStyle = state.style;
        if (plan.showEllipsis) { lineStyle.direction = plan.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight; }
        if (plan.showEllipsis && plan.rightToLeft)
        {
            if (auto status = UITextPaintEmitter::append(output, layout, ordinal, UITextEllipsisUtf8, lineStyle,
                state.textColor, cursor.x, cursor.y, state.rasterSource, &cursor); !status)
            { return Core::failure(status.error()); }
        }
        if (auto status = UITextPaintEmitter::append(output, layout, ordinal, plan.visibleText, lineStyle,
            state.textColor, cursor.x, startY, state.rasterSource, &cursor, state.availableWidth,
            state.textWrapMode, state.textLineClamp); !status) { return Core::failure(status.error()); }
        if (plan.showEllipsis && !plan.rightToLeft)
        {
            if (auto status = UITextPaintEmitter::append(output, layout, ordinal, UITextEllipsisUtf8, state.style,
                state.textColor, cursor.x, cursor.y, state.rasterSource, &cursor); !status)
            { return Core::failure(status.error()); }
        }
        return std::optional<UITextEditCaretGeometry>{};
    }
    DisplayText display;
    if (auto status = prepareDisplay(state, display); !status) { return Core::failure(status.error()); }
    const float height = state.style.logicalSize * state.style.lineHeightScale;
    const float fallback = state.style.logicalSize * state.style.advanceScale;
    const float scroll = state.multilineEnabled && std::isfinite(state.scrollY) ? state.scrollY : 0.0F;
    const usize selectedLine = caretLine(display, state.preeditActive ? UITextEditCaretAffinity::Downstream : state.caretAffinity);
    std::optional<UITextEditCaretGeometry> caret;
    for (usize index = 0; index < display.lineCount; ++index)
    {
        const auto& line = display.lines[index];
        const auto text = lineText(display, line);
        auto lineState = state;
        lineState.style.direction = line.rightToLeft ? UITextDirection::RightToLeft : UITextDirection::LeftToRight;
        const float y = startY + line.top - scroll;
        std::span<const UITextScalarMetrics> scalars{};
        if (hasRaster(state))
        {
            auto batch = raster(lineState, text);
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
        const usize beginByte = utf8ByteOffsetForCodepoint(display.text, line.beginCodepoint);
        const UITextPaintRangeTint localTint{
            display.tint.byteBegin > beginByte ? display.tint.byteBegin - beginByte : 0,
            display.tint.byteEnd > beginByte ? display.tint.byteEnd - beginByte : 0, display.tint.color};
        if (auto status = UITextPaintEmitter::append(output, layout, ordinal, text, lineState.style,
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
