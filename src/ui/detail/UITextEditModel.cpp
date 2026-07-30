#include "UITextEditModel.hpp"

#include <algorithm>
#include <cmath>

namespace Tina::UI::Detail {

bool containsLineBreak(std::string_view text) noexcept
{
    return text.find('\r') != std::string_view::npos ||
           text.find('\n') != std::string_view::npos;
}

usize utf8ByteOffsetForCodepoint(std::string_view text,
                                 u32 codepointOffset) noexcept
{
    usize byteOffset = 0;
    u32 codepoint = 0;
    while (byteOffset < text.size() && codepoint < codepointOffset)
    {
        const auto first = static_cast<unsigned char>(text[byteOffset]);
        usize unitLength = 1;
        if (first > 0x7FU)
        {
            unitLength = (first & 0xE0U) == 0xC0U ? 2U
                         : (first & 0xF0U) == 0xE0U ? 3U
                                                   : 4U;
        }
        byteOffset += unitLength;
        ++codepoint;
    }
    return (std::min)(byteOffset, text.size());
}

u32 textEditCodepointFromHorizontalPosition(
    float relativeX, u32 codepointCount, float fallbackAdvance,
    std::span<const UITextGlyphRaster> glyphs) noexcept
{
    if (codepointCount == 0 || !(relativeX > 0.0F))
    {
        return 0;
    }
    if (!(std::isfinite(fallbackAdvance) && fallbackAdvance > 0.0F))
    {
        fallbackAdvance = 1.0F;
    }

    if (glyphs.size() >= codepointCount)
    {
        float cursorX = 0.0F;
        for (u32 codepointIndex = 0; codepointIndex < codepointCount;
             ++codepointIndex)
        {
            float glyphAdvance = glyphs[codepointIndex].advance;
            if (!(std::isfinite(glyphAdvance) && glyphAdvance > 0.0F))
            {
                glyphAdvance = fallbackAdvance;
            }
            const float midpoint = cursorX + glyphAdvance * 0.5F;
            if (!std::isfinite(midpoint) || relativeX < midpoint)
            {
                return codepointIndex;
            }
            cursorX += glyphAdvance;
            if (!std::isfinite(cursorX))
            {
                return codepointIndex + 1U;
            }
        }
        return codepointCount;
    }

    const float approximate =
        std::floor(relativeX / fallbackAdvance + 0.5F);
    if (!(std::isfinite(approximate) && approximate > 0.0F))
    {
        return 0;
    }
    return static_cast<u32>((std::min)(
        approximate, static_cast<float>(codepointCount)));
}

std::optional<UITextEditCommandPlan> planTextEditCommand(
    UITextSelection currentSelection, u32 codepointCount,
    UITextEditCommand command, bool extendSelection) noexcept
{
    const u32 selectionBegin =
        (std::min)(currentSelection.anchorCodepoint,
                   currentSelection.caretCodepoint);
    const u32 selectionEnd =
        (std::max)(currentSelection.anchorCodepoint,
                   currentSelection.caretCodepoint);
    UITextEditCommandPlan plan{
        .nextSelection = currentSelection,
        .deleteBeginCodepoint = selectionBegin,
        .deleteEndCodepoint = selectionEnd,
    };

    switch (command)
    {
    case UITextEditCommand::MoveLeft: {
        const u32 nextCaret =
            !extendSelection && !currentSelection.isCollapsed()
                ? selectionBegin
                : currentSelection.caretCodepoint > 0
                      ? currentSelection.caretCodepoint - 1U
                      : 0U;
        plan.nextSelection.caretCodepoint = nextCaret;
        if (!extendSelection)
        {
            plan.nextSelection.anchorCodepoint = nextCaret;
        }
        return plan;
    }
    case UITextEditCommand::MoveRight: {
        const u32 advancedCaret = currentSelection.caretCodepoint < codepointCount
                                      ? currentSelection.caretCodepoint + 1U
                                      : codepointCount;
        const u32 nextCaret = !extendSelection && !currentSelection.isCollapsed()
                                  ? selectionEnd
                                  : advancedCaret;
        plan.nextSelection.caretCodepoint = nextCaret;
        if (!extendSelection)
        {
            plan.nextSelection.anchorCodepoint = nextCaret;
        }
        return plan;
    }
    case UITextEditCommand::MoveHome:
        plan.nextSelection.caretCodepoint = 0;
        if (!extendSelection)
        {
            plan.nextSelection.anchorCodepoint = 0;
        }
        return plan;
    case UITextEditCommand::MoveEnd:
        plan.nextSelection.caretCodepoint = codepointCount;
        if (!extendSelection)
        {
            plan.nextSelection.anchorCodepoint = codepointCount;
        }
        return plan;
    case UITextEditCommand::SelectAll:
        plan.nextSelection = {
            .anchorCodepoint = 0,
            .caretCodepoint = codepointCount,
        };
        return plan;
    case UITextEditCommand::Backspace:
        plan.deletesText = true;
        if (currentSelection.isCollapsed())
        {
            plan.deleteBeginCodepoint = currentSelection.caretCodepoint > 0
                                            ? currentSelection.caretCodepoint - 1U
                                            : 0U;
            plan.deleteEndCodepoint = currentSelection.caretCodepoint;
        }
        return plan;
    case UITextEditCommand::Delete:
        plan.deletesText = true;
        if (currentSelection.isCollapsed())
        {
            plan.deleteBeginCodepoint = currentSelection.caretCodepoint;
            plan.deleteEndCodepoint = currentSelection.caretCodepoint < codepointCount
                                          ? currentSelection.caretCodepoint + 1U
                                          : codepointCount;
        }
        return plan;
    }
    return std::nullopt;
}

} // namespace Tina::UI::Detail
