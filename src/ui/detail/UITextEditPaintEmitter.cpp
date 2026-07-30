#include "UITextEditPaintEmitter.hpp"

#include "UILayoutPrimitives.hpp"
#include "UIPaintPrimitives.hpp"
#include "UITextEditModel.hpp"

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {
namespace {

[[nodiscard]] UIPremultipliedRgba8Color preeditColor() noexcept
{
    return premultiply(UIStraightSrgba8Color{
        .red = 0,
        .green = 180,
        .blue = 255,
        .alpha = 255,
    });
}

[[nodiscard]] UIPremultipliedRgba8Color selectionColor() noexcept
{
    return premultiply(UIStraightSrgba8Color{
        .red = 42,
        .green = 112,
        .blue = 190,
        .alpha = 190,
    });
}

[[nodiscard]] UIPremultipliedRgba8Color caretColor() noexcept
{
    return premultiply(UIStraightSrgba8Color{
        .red = 255,
        .green = 255,
        .blue = 255,
        .alpha = 255,
    });
}

} // namespace

usize UITextEditPaintEmitter::countEntries(const UITextEditPaintState& state) noexcept
{
    usize paintEntryCount = 0;
    if (!state.committedText.empty() && state.style.color.alpha != 0)
    {
        const usize committedCodepoints = countDrawableTextCodepoints(state.committedText);
        if (state.focused && state.preeditActive)
        {
            const usize selectedCodepoints =
                static_cast<usize>((std::max)(state.selection.anchorCodepoint, state.selection.caretCodepoint) -
                                   (std::min)(state.selection.anchorCodepoint, state.selection.caretCodepoint));
            paintEntryCount += committedCodepoints - (std::min)(committedCodepoints, selectedCodepoints);
        } else
        {
            paintEntryCount += committedCodepoints;
        }
    }

    if (!state.focused)
    {
        return paintEntryCount;
    }
    if (state.preeditActive)
    {
        paintEntryCount += countDrawableTextCodepoints(state.preeditText);
    } else if (!state.selection.isCollapsed())
    {
        ++paintEntryCount;
    }
    return paintEntryCount + 1;
}

void UITextEditPaintEmitter::append(std::pmr::vector<UICommittedPaintEntry>& output,
                                    const UICommittedLayoutEntry& layoutEntry, u32& nextPaintOrdinal,
                                    const UITextEditPaintState& state) noexcept
{
    UICommittedLayoutEntry textLayoutEntry = layoutEntry;
    textLayoutEntry.effectiveClip =
        intersectRects(textLayoutEntry.effectiveClip, layoutEntry.contentPlacement.contentBox);
    const float textStartX = layoutEntry.contentPlacement.origin.x;
    const float textStartY = layoutEntry.contentPlacement.origin.y;
    UITextPaintCursor cursor{
        .x = textStartX,
        .y = textStartY,
        .lineHeight = state.style.logicalSize * state.style.lineHeightScale,
        .baseX = textStartX,
    };
    UITextPaintCursor caretCursor = cursor;
    const auto appendText = [&](std::string_view text, UIPremultipliedRgba8Color color) noexcept {
        UITextPaintEmitter::append(output, textLayoutEntry, nextPaintOrdinal, text, state.style, color, cursor.x,
                                   cursor.y, state.rasterSource, &cursor);
    };

    if (!state.focused)
    {
        appendText(state.committedText, state.textColor);
        return;
    }

    const u32 selectionBegin = (std::min)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
    const u32 selectionEnd = (std::max)(state.selection.anchorCodepoint, state.selection.caretCodepoint);
    const usize selectionBeginByte = utf8ByteOffsetForCodepoint(state.committedText, selectionBegin);
    const usize selectionEndByte = utf8ByteOffsetForCodepoint(state.committedText, selectionEnd);

    appendText(state.committedText.substr(0, selectionBeginByte), state.textColor);
    const UITextPaintCursor selectionStartCursor = cursor;

    if (state.preeditActive)
    {
        const usize preeditCursorByte = utf8ByteOffsetForCodepoint(state.preeditText, state.preeditCursorCodepoint);
        const UIPremultipliedRgba8Color color = preeditColor();
        appendText(state.preeditText.substr(0, preeditCursorByte), color);
        caretCursor = cursor;
        appendText(state.preeditText.substr(preeditCursorByte), color);
        appendText(state.committedText.substr(selectionEndByte), state.textColor);
    } else
    {
        const usize selectionPaintIndex = output.size();
        if (selectionBegin != selectionEnd)
        {
            output.push_back(UICommittedPaintEntry{
                .node = layoutEntry.node,
                .worldRect = {},
                .effectiveClip = textLayoutEntry.effectiveClip,
                .paintOrdinal = nextPaintOrdinal,
                .solidFill = selectionColor(),
                .isGlyph = false,
            });
            ++nextPaintOrdinal;
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
        caretCursor =
            state.selection.caretCodepoint == selectionBegin ? selectionStartCursor : selectionEndCursor;
        appendText(state.committedText.substr(selectionEndByte), state.textColor);
    }

    float lineHeight = caretCursor.lineHeight;
    if (!(std::isfinite(lineHeight) && lineHeight > 0.0F))
    {
        lineHeight = state.style.logicalSize * state.style.lineHeightScale;
    }
    if (!(std::isfinite(lineHeight) && lineHeight > 0.0F))
    {
        lineHeight = 19.2F;
    }
    constexpr float CaretWidth = 2.0F;
    output.push_back(UICommittedPaintEntry{
        .node = layoutEntry.node,
        .worldRect =
            UILogicalRect{
                .x = normalizeFloat(caretCursor.x),
                .y = normalizeFloat(caretCursor.y),
                .width = CaretWidth,
                .height = normalizeFloat(lineHeight),
            },
        .effectiveClip = textLayoutEntry.effectiveClip,
        .paintOrdinal = nextPaintOrdinal,
        .solidFill = caretColor(),
        .isGlyph = false,
    });
    ++nextPaintOrdinal;
}

} // namespace Tina::UI::Detail
